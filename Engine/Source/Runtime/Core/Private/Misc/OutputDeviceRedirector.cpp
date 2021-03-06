// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/OutputDeviceRedirector.h"

#include "Containers/DepletableMpscQueue.h"
#include "Experimental/ConcurrentLinearAllocator.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "HAL/Thread.h"
#include "Misc/App.h"
#include "Misc/ScopeRWLock.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include <atomic>

/*-----------------------------------------------------------------------------
	FOutputDeviceRedirector.
-----------------------------------------------------------------------------*/

FBufferedLine::FBufferedLine(const TCHAR* InData, const FName& InCategory, ELogVerbosity::Type InVerbosity, double InTime)
	: Category(InCategory)
	, Time(InTime)
	, Verbosity(InVerbosity)
{
	int32 NumChars = FCString::Strlen(InData) + 1;
	void* Dest = FMemory::Malloc(sizeof(TCHAR) * NumChars);
	Data = (TCHAR*)FMemory::Memcpy(Dest, InData, sizeof(TCHAR) * NumChars);
}

FBufferedLine::~FBufferedLine()
{
	FMemory::Free(const_cast<TCHAR*>(Data));
}

namespace UE::Private
{

struct FOutputDeviceBlockAllocationTag : FDefaultBlockAllocationTag
{
	static constexpr const TCHAR* TagName = TEXT("OutputDeviceLinear");

	struct Allocator
	{
		static constexpr bool SupportsAlignment = false;

		FORCEINLINE static void* Malloc(SIZE_T Size, uint32 Alignment)
		{
			return FMemory::Malloc(Size, Alignment);
		}

		FORCEINLINE static void Free(void* Pointer, SIZE_T Size)
		{
			return FMemory::Free(Pointer);
		}
	};
};

struct FOutputDeviceLinearAllocator
{
	FORCEINLINE static void* Malloc(SIZE_T Size, uint32 Alignment)
	{
		return TConcurrentLinearAllocator<FOutputDeviceBlockAllocationTag>::Malloc(Size, Alignment);
	}

	FORCEINLINE static void Free(void* Pointer)
	{
		TConcurrentLinearAllocator<FOutputDeviceBlockAllocationTag>::Free(Pointer);
	}
};

struct FOutputDeviceLine
{
	const double Time;
	const TCHAR* Data;
	const FName Category;
	const ELogVerbosity::Type Verbosity;

	FOutputDeviceLine(const FBufferedLine&) = delete;
	FOutputDeviceLine& operator=(const FBufferedLine&) = delete;

	FORCEINLINE FOutputDeviceLine(const TCHAR* const InData, const FName InCategory, const ELogVerbosity::Type InVerbosity, const double InTime)
		: Time(InTime)
		, Data(CopyData(InData))
		, Category(InCategory)
		, Verbosity(InVerbosity)
	{
	}

	FORCEINLINE ~FOutputDeviceLine()
	{
		FOutputDeviceLinearAllocator::Free(const_cast<TCHAR*>(Data));
	}

private:
	FORCEINLINE static const TCHAR* CopyData(const TCHAR* const InData)
	{
		const int32 Len = FCString::Strlen(InData) + 1;
		void* const Dest = FOutputDeviceLinearAllocator::Malloc(sizeof(TCHAR) * Len, alignof(TCHAR));
		return static_cast<TCHAR*>(FMemory::Memcpy(Dest, InData, sizeof(TCHAR) * Len));
	}
};

static constexpr uint64 CalculateRedirectorCacheLinePadding(const uint64 Size)
{
	return PLATFORM_CACHE_LINE_SIZE * FMath::DivideAndRoundUp<uint64>(Size, PLATFORM_CACHE_LINE_SIZE) - Size;
}

struct FOutputDeviceRedirectorState
{
	/** A custom lock to guard access to both buffered and unbuffered output devices. */
	FRWLock OutputDevicesLock;
	std::atomic<uint32> OutputDevicesLockState = 0;
	uint8 OutputDevicesLockPadding[CalculateRedirectorCacheLinePadding(sizeof(OutputDevicesLock) + sizeof(OutputDevicesLockState))]{};

	/** A queue of lines logged by non-master threads. */
	TDepletableMpscQueue<FOutputDeviceLine, FOutputDeviceLinearAllocator> BufferedLines;
	uint8 BufferedLinesPadding[CalculateRedirectorCacheLinePadding(sizeof(BufferedLines))]{};

	/** Array of output devices to redirect to from the master thread. */
	TArray<FOutputDevice*> BufferedOutputDevices;

	/** Array of output devices to redirect to from the calling thread. */
	TArray<FOutputDevice*> UnbufferedOutputDevices;

	/** A queue of lines logged before the editor added its output device. */
	TArray<FBufferedLine> BacklogLines;
	FRWLock BacklogLock;

	/** An optional dedicated master thread for logging to buffered output devices. */
	FThread Thread;

	/** A lock to synchronize access to the thread. */
	FRWLock ThreadLock;

	/** An event to wake the dedicated master thread to process buffered lines. */
	std::atomic<FEvent*> ThreadWakeEvent = nullptr;

	/** A queue of events to trigger when the dedicated master thread is idle. */
	TDepletableMpscQueue<FEvent*, FOutputDeviceLinearAllocator> ThreadIdleEvents;

	/** The ID of the master thread. Logging from other threads will be buffered for processing by the master thread. */
	std::atomic<uint32> MasterThreadId = FPlatformTLS::GetCurrentThreadId();

	/** The ID of the panic thread, which is only set by Panic(). */
	std::atomic<uint32> PanicThreadId = MAX_uint32;

	/** Whether the backlog is enabled. */
	bool bEnableBacklog = false;

	bool IsMasterThread(const uint32 ThreadId) const
	{
		return ThreadId == MasterThreadId.load(std::memory_order_relaxed);
	}

	bool IsPanicThread(const uint32 ThreadId) const
	{
		return ThreadId == PanicThreadId.load(std::memory_order_relaxed);
	}

	bool CanLockFromThread(const uint32 ThreadId) const
	{
		const uint32 LocalPanicThreadId = PanicThreadId.load(std::memory_order_relaxed);
		return LocalPanicThreadId == MAX_uint32 || LocalPanicThreadId == ThreadId;
	}

	bool TryStartThread();
	bool TryStopThread();

	void ThreadLoop();

	void FlushBufferedLines();

	template <typename OutputDevicesType, typename FunctionType, typename... ArgTypes>
	FORCEINLINE void BroadcastTo(const uint32 ThreadId, const OutputDevicesType& OutputDevices, FunctionType&& Function, ArgTypes&&... Args)
	{
		const bool bIsPanicThread = IsPanicThread(ThreadId);
		for (FOutputDevice* OutputDevice : OutputDevices)
		{
			if (!bIsPanicThread || OutputDevice->CanBeUsedOnPanicThread())
			{
				Invoke(Function, OutputDevice, Forward<ArgTypes>(Args)...);
			}
		}
	}
};

/**
 * A scoped lock for readers of the OutputDevices arrays.
 *
 * The read lock:
 * - Must be locked to read the OutputDevices arrays.
 * - Must be locked to write to unbuffered output devices.
 * - Must not be entered when the thread holds a write or master lock.
 */
class FOutputDevicesReadScopeLock
{
public:
	FORCEINLINE explicit FOutputDevicesReadScopeLock(FOutputDeviceRedirectorState& InState)
		: State(InState)
	{
		// Read locks add/sub by 2 to keep the LSB free for write locks to use.
		if (State.OutputDevicesLockState.fetch_add(2, std::memory_order_acquire) & 1)
		{
			WaitForWriteLock();
		}
	}

	FORCENOINLINE void WaitForWriteLock()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FOutputDevicesReadScopeLock);
		// A write lock has set the LSB. Cancel this read lock and wait for the write.
		State.OutputDevicesLockState.fetch_sub(2, std::memory_order_relaxed);
		// This read lock will wait until the write lock exits.
		FReadScopeLock ScopeLock(State.OutputDevicesLock);
		// Acquire on this read lock because the write may have mutated state that we read.
		uint32 LockState = State.OutputDevicesLockState.fetch_add(2, std::memory_order_acquire);
		check((LockState & 1) == 0);
	}

	FORCEINLINE ~FOutputDevicesReadScopeLock()
	{
		State.OutputDevicesLockState.fetch_sub(2, std::memory_order_relaxed);
	}

private:
	FOutputDeviceRedirectorState& State;
};

/**
 * A scoped lock for writers of the OutputDevices arrays.
 *
 * The write lock has the same access as the master lock, and:
 * - Must be locked to add or remove output devices.
 * - Must not be entered when the thread holds a read, write, or master lock.
 */
class FOutputDevicesWriteScopeLock
{
public:
	FORCEINLINE explicit FOutputDevicesWriteScopeLock(FOutputDeviceRedirectorState& InState)
		: State(InState)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FOutputDevicesWriteScopeLock);
		// Take the lock before modifying the state, to avoid contention on the LSB.
		State.OutputDevicesLock.WriteLock();
		// Set the LSB to flag to read locks that a write lock is waiting.
		uint32 LockState = State.OutputDevicesLockState.fetch_or(uint32(1), std::memory_order_relaxed);
		check((LockState & 1) == 0);
		if (LockState > 1)
		{
			// Wait for read locks to be cleared.
			do
			{
				FPlatformProcess::Sleep(0);
				LockState = State.OutputDevicesLockState.load(std::memory_order_relaxed);
			}
			while (LockState > 1);
		}
	}

	FORCEINLINE ~FOutputDevicesWriteScopeLock()
	{
		// Clear the LSB to allow read locks after the unlock below.
		uint32 LockState = State.OutputDevicesLockState.fetch_and(~uint32(1), std::memory_order_release);
		check((LockState & 1) == 1);
		State.OutputDevicesLock.WriteUnlock();
	}

private:
	FOutputDeviceRedirectorState& State;
};

/**
 * A scoped lock for readers of the OutputDevices arrays that need to access master thread state.
 *
 * The master lock has the same access as the read lock, and:
 * - Must not be entered when the thread holds a write lock or master lock.
 * - Must check IsLocked() before performing restricted operations.
 * - Must be locked to write to buffered output devices.
 * - Must be locked while calling FlushBufferedLines().
 * - May be locked when the thread holds a read lock.
 * - When a panic thread is active, may only be locked from the panic thread.
 */
class FOutputDevicesMasterScope
{
public:
	explicit FOutputDevicesMasterScope(FOutputDeviceRedirectorState& InState)
		: State(InState)
	{
		const uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();
		if (State.CanLockFromThread(ThreadId))
		{
			if (State.IsPanicThread(ThreadId))
			{
				bLocked = true;
			}
			else
			{
				State.OutputDevicesLock.WriteLock();
				if (!State.CanLockFromThread(ThreadId))
				{
					State.OutputDevicesLock.WriteUnlock();
				}
				else
				{
					bNeedsUnlock = true;
					bLocked = true;
				}
			}
		}
	}

	FORCEINLINE ~FOutputDevicesMasterScope()
	{
		if (bNeedsUnlock)
		{
			State.OutputDevicesLock.WriteUnlock();
		}
	}

	FORCEINLINE bool IsLocked() const { return bLocked; }

private:
	FOutputDeviceRedirectorState& State;
	bool bNeedsUnlock = false;
	bool bLocked = false;
};

bool FOutputDeviceRedirectorState::TryStartThread()
{
	if (FWriteScopeLock ThreadScopeLock(ThreadLock); !ThreadWakeEvent.load(std::memory_order_relaxed))
	{
		FEvent* WakeEvent = FPlatformProcess::GetSynchEventFromPool();
		WakeEvent->Trigger();
		ThreadWakeEvent.store(WakeEvent, std::memory_order_release);
		Thread = FThread(TEXT("OutputDeviceRedirector"), [this] { ThreadLoop(); });
	}
	return true;
}

bool FOutputDeviceRedirectorState::TryStopThread()
{
	if (FWriteScopeLock ThreadScopeLock(ThreadLock); FEvent* WakeEvent = ThreadWakeEvent.exchange(nullptr, std::memory_order_acquire))
	{
		WakeEvent->Trigger();
		Thread.Join();
		FOutputDevicesWriteScopeLock Lock(*this);
		FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
	}
	return true;
}

void FOutputDeviceRedirectorState::ThreadLoop()
{
	const uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();

	if (FOutputDevicesMasterScope Lock(*this); Lock.IsLocked())
	{
		MasterThreadId.store(ThreadId, std::memory_order_relaxed);
	}

	while (FEvent* WakeEvent = ThreadWakeEvent.load(std::memory_order_acquire))
	{
		WakeEvent->Wait();
		while (!BufferedLines.IsEmpty() && IsMasterThread(ThreadId))
		{
			if (FOutputDevicesMasterScope Lock(*this); Lock.IsLocked())
			{
				FlushBufferedLines();
			}
		}
		ThreadIdleEvents.Deplete([](FEvent* Event) { Event->Trigger(); });
	}
}

void FOutputDeviceRedirectorState::FlushBufferedLines()
{
	if (BufferedLines.IsEmpty())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FOutputDeviceRedirector::FlushBufferedLines);

	const uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();
	BufferedLines.Deplete([this, ThreadId](UE::Private::FOutputDeviceLine&& Line)
	{
		BroadcastTo(ThreadId, BufferedOutputDevices, UE_PROJECTION_MEMBER(FOutputDevice, Serialize),
			Line.Data, Line.Verbosity, Line.Category, Line.Time);
	});
}

} // UE::Private

FOutputDeviceRedirector::FOutputDeviceRedirector()
	: State(MakePimpl<UE::Private::FOutputDeviceRedirectorState>())
{
}

FOutputDeviceRedirector* FOutputDeviceRedirector::Get()
{
	static FOutputDeviceRedirector Singleton;
	return &Singleton;
}

void FOutputDeviceRedirector::AddOutputDevice(FOutputDevice* OutputDevice)
{
	if (OutputDevice)
	{
		UE::Private::FOutputDevicesWriteScopeLock ScopeLock(*State);
		if (OutputDevice->CanBeUsedOnMultipleThreads())
		{
			State->UnbufferedOutputDevices.AddUnique(OutputDevice);
		}
		else
		{
			State->BufferedOutputDevices.AddUnique(OutputDevice);
		}
	}
}

void FOutputDeviceRedirector::RemoveOutputDevice(FOutputDevice* OutputDevice)
{
	if (OutputDevice)
	{
		UE::Private::FOutputDevicesWriteScopeLock ScopeLock(*State);
		State->BufferedOutputDevices.Remove(OutputDevice);
		State->UnbufferedOutputDevices.Remove(OutputDevice);
	}
}

bool FOutputDeviceRedirector::IsRedirectingTo(FOutputDevice* OutputDevice)
{
	UE::Private::FOutputDevicesReadScopeLock Lock(*State);
	return State->BufferedOutputDevices.Contains(OutputDevice) || State->UnbufferedOutputDevices.Contains(OutputDevice);
}

void FOutputDeviceRedirector::FlushThreadedLogs(EOutputDeviceRedirectorFlushOptions Options)
{
	if (FReadScopeLock ThreadLock(State->ThreadLock); FEvent* WakeEvent = State->ThreadWakeEvent.load(std::memory_order_acquire))
	{
		if (!EnumHasAnyFlags(Options, EOutputDeviceRedirectorFlushOptions::Async))
		{
			FEventRef IdleEvent(EEventMode::ManualReset);
			if (State->ThreadIdleEvents.EnqueueAndReturnWasEmpty(IdleEvent.Get()))
			{
				WakeEvent->Trigger();
			}
			IdleEvent->Wait();
		}
		return;
	}

	if (UE::Private::FOutputDevicesMasterScope Lock(*State); Lock.IsLocked())
	{
		State->FlushBufferedLines();
	}
}

void FOutputDeviceRedirector::SerializeBacklog(FOutputDevice* OutputDevice)
{
	FReadScopeLock ScopeLock(State->BacklogLock);
	for (const FBufferedLine& BacklogLine : State->BacklogLines)
	{
		OutputDevice->Serialize(BacklogLine.Data, BacklogLine.Verbosity, BacklogLine.Category, BacklogLine.Time);
	}
}

void FOutputDeviceRedirector::EnableBacklog(bool bEnable)
{
	FWriteScopeLock ScopeLock(State->BacklogLock);
	State->bEnableBacklog = bEnable;
	if (!bEnable)
	{
		State->BacklogLines.Empty();
	}
}

void FOutputDeviceRedirector::SetCurrentThreadAsMasterThread()
{
	const uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();

	if (UE::Private::FOutputDevicesMasterScope Lock(*State); !Lock.IsLocked() || State->MasterThreadId.load(std::memory_order_relaxed) == ThreadId)
	{
		return;
	}
	else
	{
		State->MasterThreadId.store(ThreadId, std::memory_order_relaxed);
		State->FlushBufferedLines();
	}

	State->TryStopThread();
}

bool FOutputDeviceRedirector::TryStartDedicatedMasterThread()
{
	return FApp::ShouldUseThreadingForPerformance() && State->TryStartThread();
}

void FOutputDeviceRedirector::Serialize(const TCHAR* const Data, const ELogVerbosity::Type Verbosity, const FName& Category, const double Time)
{
	const double RealTime = Time == -1.0f ? FPlatformTime::Seconds() - GStartTime : Time;

	UE::Private::FOutputDevicesReadScopeLock Lock(*State);

#if PLATFORM_DESKTOP
	// Print anything that arrives after logging has shut down to at least have it in stdout.
	if (UNLIKELY(State->BufferedOutputDevices.IsEmpty() && IsEngineExitRequested()))
	{
	#if PLATFORM_WINDOWS
		_tprintf(_T("%s\n"), Data);
	#endif
		FGenericPlatformMisc::LocalPrint(Data);
		return;
	}
#endif

	const uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();

	// Serialize directly to any output devices which don't require buffering
	State->BroadcastTo(ThreadId, State->UnbufferedOutputDevices, UE_PROJECTION_MEMBER(FOutputDevice, Serialize),
		Data, Verbosity, Category, RealTime);

	// Serialize to the backlog when not in panic mode. This will deadlock in panic mode when the
	// FPlatformMallocCrash allocator has been enabled and logging occurs on a non-panic thread.
	if (UNLIKELY(State->bEnableBacklog && !State->IsPanicThread(ThreadId)))
	{
		FWriteScopeLock ScopeLock(State->BacklogLock);
		State->BacklogLines.Emplace(Data, Category, Verbosity, RealTime);
	}

	// Serialize to buffered output devices from the master thread.
	// Lines are queued until buffered output devices are added to avoid missing early log lines.
	if (State->IsMasterThread(ThreadId) && !State->BufferedOutputDevices.IsEmpty())
	{
		// Verify that this is the master thread again because another thread may have become
		// the master thread between the previous check and the lock.
		if (UE::Private::FOutputDevicesMasterScope MasterLock(*State); MasterLock.IsLocked() && State->IsMasterThread(ThreadId))
		{
			State->FlushBufferedLines();
			State->BroadcastTo(ThreadId, State->BufferedOutputDevices, UE_PROJECTION_MEMBER(FOutputDevice, Serialize),
				Data, Verbosity, Category, RealTime);
			if (UNLIKELY(State->IsPanicThread(ThreadId)))
			{
				Flush();
			}
			return;
		}
	}

	// Queue the line to serialize to buffered output devices from the master thread.
	if (State->BufferedLines.EnqueueAndReturnWasEmpty(Data, Category, Verbosity, RealTime))
	{
		if (FEvent* WakeEvent = State->ThreadWakeEvent.load(std::memory_order_acquire))
		{
			WakeEvent->Trigger();
		}
	}
}

void FOutputDeviceRedirector::Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category)
{
	Serialize(Data, Verbosity, Category, -1.0);
}

void FOutputDeviceRedirector::RedirectLog(const FName& Category, ELogVerbosity::Type Verbosity, const TCHAR* Data)
{
	Serialize(Data, Verbosity, Category, -1.0);
}

void FOutputDeviceRedirector::RedirectLog(const FLazyName& Category, ELogVerbosity::Type Verbosity, const TCHAR* Data)
{
	Serialize(Data, Verbosity, Category, -1.0);
}

void FOutputDeviceRedirector::Flush()
{
	if (UE::Private::FOutputDevicesMasterScope Lock(*State); Lock.IsLocked())
	{
		State->FlushBufferedLines();
		const uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();
		State->BroadcastTo(ThreadId, State->BufferedOutputDevices, &FOutputDevice::Flush);
		State->BroadcastTo(ThreadId, State->UnbufferedOutputDevices, &FOutputDevice::Flush);
	}
}

void FOutputDeviceRedirector::Panic()
{
	uint32 PreviousThreadId = MAX_uint32;
	const uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();
	if (State->PanicThreadId.compare_exchange_strong(PreviousThreadId, ThreadId, std::memory_order_relaxed))
	{
		// Another thread may be holding the lock. Wait a while for it, but avoid waiting forever
		// because the thread holding the lock may be unable to progress. After the timeout is
		// reached, assume that it is safe enough to continue on the panic thread. There is a
		// chance that the thread holding the lock has left an output device in an unusable state
		// or will resume and crash due to a race with the panic thread. Executing on this thread
		// and having logging for most panic situations with a chance of a crash is preferable to
		// the alternative of missing logging in a panic situation.
		TRACE_CPUPROFILER_EVENT_SCOPE(FOutputDeviceRedirector::PanicWait);
		constexpr double WaitTime = 1.0;
		for (const double EndTime = FPlatformTime::Seconds() + WaitTime; FPlatformTime::Seconds() < EndTime;)
		{
			if (State->OutputDevicesLock.TryWriteLock())
			{
				State->OutputDevicesLock.WriteUnlock();
				break;
			}
			FPlatformProcess::Yield();
		}

		// Make the panic thread the master thread. Neither thread can be changed after this point.
		State->MasterThreadId.exchange(ThreadId, std::memory_order_relaxed);

		// Flush. Every log from the panic thread after this point will also flush.
		Flush();
	}
	else if (PreviousThreadId == ThreadId)
	{
		// Calling Panic() multiple times from the panic thread is equivalent to calling Flush().
		Flush();
	}
}

void FOutputDeviceRedirector::TearDown()
{
	SetCurrentThreadAsMasterThread();

	Flush();

	State->TryStopThread();

	TArray<FOutputDevice*> LocalBufferedDevices;
	TArray<FOutputDevice*> LocalUnbufferedDevices;

	{
		UE::Private::FOutputDevicesWriteScopeLock Lock(*State);
		LocalBufferedDevices = MoveTemp(State->BufferedOutputDevices);
		LocalUnbufferedDevices = MoveTemp(State->UnbufferedOutputDevices);
		State->BufferedOutputDevices.Empty();
		State->UnbufferedOutputDevices.Empty();
	}

	for (FOutputDevice* OutputDevice : LocalBufferedDevices)
	{
		OutputDevice->TearDown();
	}

	for (FOutputDevice* OutputDevice : LocalUnbufferedDevices)
	{
		OutputDevice->TearDown();
	}
}

bool FOutputDeviceRedirector::IsBacklogEnabled() const
{
	FReadScopeLock Lock(State->BacklogLock);
	return State->bEnableBacklog;
}

CORE_API FOutputDeviceRedirector* GetGlobalLogSingleton()
{
	return FOutputDeviceRedirector::Get();
}
