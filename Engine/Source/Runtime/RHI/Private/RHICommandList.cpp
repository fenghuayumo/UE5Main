// Copyright Epic Games, Inc. All Rights Reserved.


#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Async/TaskGraphInterfaces.h"
#include "RHI.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeExit.h"
#include "PipelineStateCache.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Trace/Trace.inl"
#include "GenericPlatform/GenericPlatformCrashContext.h"

CSV_DEFINE_CATEGORY_MODULE(RHI_API, RHITStalls, false);
CSV_DEFINE_CATEGORY_MODULE(RHI_API, RHITFlushes, false);

DECLARE_CYCLE_STAT(TEXT("Nonimmed. Command List Execute"), STAT_NonImmedCmdListExecuteTime, STATGROUP_RHICMDLIST);
DECLARE_DWORD_COUNTER_STAT(TEXT("Nonimmed. Command List memory"), STAT_NonImmedCmdListMemory, STATGROUP_RHICMDLIST);
DECLARE_DWORD_COUNTER_STAT(TEXT("Nonimmed. Command count"), STAT_NonImmedCmdListCount, STATGROUP_RHICMDLIST);

DECLARE_CYCLE_STAT(TEXT("All Command List Execute"), STAT_ImmedCmdListExecuteTime, STATGROUP_RHICMDLIST);
DECLARE_DWORD_COUNTER_STAT(TEXT("Immed. Command List memory"), STAT_ImmedCmdListMemory, STATGROUP_RHICMDLIST);
DECLARE_DWORD_COUNTER_STAT(TEXT("Immed. Command count"), STAT_ImmedCmdListCount, STATGROUP_RHICMDLIST);

UE_TRACE_CHANNEL_DEFINE(RHICommandsChannel);

#if VALIDATE_UNIFORM_BUFFER_STATIC_BINDINGS
bool FScopedUniformBufferStaticBindings::bRecursionGuard = false;
#endif

#if !PLATFORM_USES_FIXED_RHI_CLASS
#include "RHICommandListCommandExecutes.inl"
#endif

#ifndef NEEDS_DEBUG_INFO_ON_PRESENT_HANG
#define NEEDS_DEBUG_INFO_ON_PRESENT_HANG 0
#endif

static TAutoConsoleVariable<int32> CVarRHICmdBypass(
	TEXT("r.RHICmdBypass"),
	FRHICommandListExecutor::DefaultBypass,
	TEXT("Whether to bypass the rhi command list and send the rhi commands immediately.\n")
	TEXT("0: Disable (required for the multithreaded renderer)\n")
	TEXT("1: Enable (convenient for debugging low level graphics API calls, can suppress artifacts from multithreaded renderer code)"));

static TAutoConsoleVariable<int32> CVarRHIRenderPassValidation(
	TEXT("r.RenderPass.Validation"),
	0,
	TEXT(""));

static TAutoConsoleVariable<int32> CVarRHICmdUseParallelAlgorithms(
	TEXT("r.RHICmdUseParallelAlgorithms"),
	1,
	TEXT("True to use parallel algorithms. Ignored if r.RHICmdBypass is 1."));

TAutoConsoleVariable<int32> CVarRHICmdWidth(
	TEXT("r.RHICmdWidth"), 
	8,
	TEXT("Controls the task granularity of a great number of things in the parallel renderer."));

static TAutoConsoleVariable<int32> CVarRHICmdUseDeferredContexts(
	TEXT("r.RHICmdUseDeferredContexts"),
	1,
	TEXT("True to use deferred contexts to parallelize command list execution. Only available on some RHIs."));

TAutoConsoleVariable<int32> CVarRHICmdFlushRenderThreadTasks(
	TEXT("r.RHICmdFlushRenderThreadTasks"),
	0,
	TEXT("If true, then we flush the render thread tasks every pass. For issue diagnosis. This is a master switch for more granular cvars."));

static TAutoConsoleVariable<int32> CVarRHICmdFlushOnQueueParallelSubmit(
	TEXT("r.RHICmdFlushOnQueueParallelSubmit"),
	0,
	TEXT("Wait for completion of parallel commandlists immediately after submitting. For issue diagnosis. Only available on some RHIs."));

static TAutoConsoleVariable<int32> CVarRHICmdMergeSmallDeferredContexts(
	TEXT("r.RHICmdMergeSmallDeferredContexts"),
	1,
	TEXT("When it can be determined, merge small parallel translate tasks based on r.RHICmdMinDrawsPerParallelCmdList."));

static TAutoConsoleVariable<int32> CVarRHICmdBufferWriteLocks(
	TEXT("r.RHICmdBufferWriteLocks"),
	1,
	TEXT("Only relevant with an RHI thread. Debugging option to diagnose problems with buffered locks."));

static TAutoConsoleVariable<int32> CVarRHICmdAsyncRHIThreadDispatch(
	TEXT("r.RHICmdAsyncRHIThreadDispatch"),
	1,
	TEXT("Experiemental option to do RHI dispatches async. This keeps data flowing to the RHI thread faster and avoid a block at the end of the frame."));

static TAutoConsoleVariable<int32> CVarRHICmdCollectRHIThreadStatsFromHighLevel(
	TEXT("r.RHICmdCollectRHIThreadStatsFromHighLevel"),
	1,
	TEXT("This pushes stats on the RHI thread executes so you can determine which high level pass they came from. This has an adverse effect on framerate. This is on by default."));

static TAutoConsoleVariable<int32> CVarRHICmdUseThread(
	TEXT("r.RHICmdUseThread"),
	1,
	TEXT("Uses the RHI thread. For issue diagnosis."));

static TAutoConsoleVariable<int32> CVarRHICmdForceRHIFlush(
	TEXT("r.RHICmdForceRHIFlush"),
	0,
	TEXT("Force a flush for every task sent to the RHI thread. For issue diagnosis."));

static TAutoConsoleVariable<int32> CVarRHICmdBalanceTranslatesAfterTasks(
	TEXT("r.RHICmdBalanceTranslatesAfterTasks"),
	0,
	TEXT("Experimental option to balance the parallel translates after the render tasks are complete. This minimizes the number of deferred contexts, but adds latency to starting the translates. r.RHICmdBalanceParallelLists overrides and disables this option"));

static TAutoConsoleVariable<int32> CVarRHICmdMinCmdlistForParallelTranslate(
	TEXT("r.RHICmdMinCmdlistForParallelTranslate"),
	2,
	TEXT("If there are fewer than this number of parallel translates, they just run on the RHI thread and immediate context. Only relevant if r.RHICmdBalanceTranslatesAfterTasks is on."));

static TAutoConsoleVariable<int32> CVarRHICmdMinCmdlistSizeForParallelTranslate(
	TEXT("r.RHICmdMinCmdlistSizeForParallelTranslate"),
	32,
	TEXT("In kilobytes. Cmdlists are merged into one parallel translate until we have at least this much memory to process. For a given pass, we won't do more translates than we have task threads. Only relevant if r.RHICmdBalanceTranslatesAfterTasks is on."));

RHI_API int32 GRHICmdTraceEvents = 0;
static FAutoConsoleVariableRef CVarRHICmdTraceEvents(
	TEXT("r.RHICmdTraceEvents"),
	GRHICmdTraceEvents,
	TEXT("Enable tracing profiler events for every RHI command. (default = 0)")
);

static TAutoConsoleVariable<int32> CVarRHICmdMaxOutstandingMemoryBeforeFlush(
	TEXT("r.RHICmdMaxOutstandingMemoryBeforeFlush"),
	256,
	TEXT("In kilobytes. The amount of outstanding memory before the RHI will force a flush. This should generally be set high enough that it doesn't happen on typical frames."));


bool GUseRHIThread_InternalUseOnly = false;
bool GUseRHITaskThreads_InternalUseOnly = false;
bool GIsRunningRHIInSeparateThread_InternalUseOnly = false;
bool GIsRunningRHIInDedicatedThread_InternalUseOnly = false;
bool GIsRunningRHIInTaskThread_InternalUseOnly = false;

uint32 GWorkingRHIThreadTime = 0;
uint32 GWorkingRHIThreadStallTime = 0;
uint32 GWorkingRHIThreadStartCycles = 0;

/** How many cycles the from sampling input to the frame being flipped. */
uint64 GInputLatencyTime = 0;

RHI_API bool GEnableAsyncCompute = true;
RHI_API FRHICommandListExecutor GRHICommandList;

static FGraphEventArray AllOutstandingTasks;
static FGraphEventArray WaitOutstandingTasks;
static FGraphEventRef RHIThreadTask;
static FGraphEventRef PrevRHIThreadTask;
static FGraphEventRef RenderThreadSublistDispatchTask;
static FGraphEventRef RHIThreadBufferLockFence;

static FGraphEventRef GRHIThreadEndDrawingViewportFences[2];
static uint32 GRHIThreadEndDrawingViewportFenceIndex = 0;

// Used by AsyncCompute
RHI_API FRHICommandListFenceAllocator GRHIFenceAllocator;

DECLARE_CYCLE_STAT(TEXT("RHI Thread Execute"), STAT_RHIThreadExecute, STATGROUP_RHICMDLIST);

static TStatId GCurrentExecuteStat;

RHI_API FAutoConsoleTaskPriority CPrio_SceneRenderingTask(
	TEXT("TaskGraph.TaskPriorities.SceneRenderingTask"),
	TEXT("Task and thread priority for various scene rendering tasks."),
	ENamedThreads::NormalThreadPriority, 
	ENamedThreads::HighTaskPriority 
	);

#if NEEDS_DEBUG_INFO_ON_PRESENT_HANG
static bool bRenderThreadSublistDispatchTaskClearedOnRT = false;
static bool bRenderThreadSublistDispatchTaskClearedOnGT = false;
static FGraphEventArray RenderThreadSublistDispatchTaskPrereqs;

void GetRenderThreadSublistDispatchTaskDebugInfo(bool& bIsNull, bool& bIsComplete, bool& bClearedOnGT, bool& bClearedOnRT, int32& NumIncompletePrereqs)
{
	bIsNull = !RenderThreadSublistDispatchTask;
	bIsComplete = true;
	bClearedOnGT = bRenderThreadSublistDispatchTaskClearedOnGT;
	bClearedOnRT = bRenderThreadSublistDispatchTaskClearedOnRT;
	NumIncompletePrereqs = 0;

	if (!bIsNull)
	{
		bIsComplete = RenderThreadSublistDispatchTask->IsComplete();
		if (!bIsComplete)
		{
			for (int32 Idx = 0; Idx < RenderThreadSublistDispatchTaskPrereqs.Num(); ++Idx)
			{
				const FGraphEvent* Prereq = RenderThreadSublistDispatchTaskPrereqs[Idx];
				if (Prereq && !Prereq->IsComplete())
				{
					++NumIncompletePrereqs;
				}
			}
		}
	}
}
#endif

FRHICOMMAND_MACRO(FRHICommandStat)
{
	TStatId CurrentExecuteStat;
	FORCEINLINE_DEBUGGABLE FRHICommandStat(TStatId InCurrentExecuteStat)
		: CurrentExecuteStat(InCurrentExecuteStat)
	{
	}
	void Execute(FRHICommandListBase& CmdList)
	{
		GCurrentExecuteStat = CurrentExecuteStat;
	}
};

void FRHICommandListBase::SetCurrentStat(TStatId Stat)
{
	if (!Bypass())
	{
		ALLOC_COMMAND(FRHICommandStat)(Stat);
	}
}

DECLARE_CYCLE_STAT(TEXT("FNullGraphTask.RenderThreadTaskFence"), STAT_RenderThreadTaskFence, STATGROUP_TaskGraphTasks);
DECLARE_CYCLE_STAT(TEXT("Render thread task fence wait"), STAT_RenderThreadTaskFenceWait, STATGROUP_TaskGraphTasks);
FGraphEventRef FRHICommandListImmediate::RenderThreadTaskFence()
{
	FGraphEventRef Result;
	check(IsInRenderingThread());
	//@todo optimize, if there is only one outstanding, then return that instead
	if (WaitOutstandingTasks.Num())
	{
		Result = TGraphTask<FNullGraphTask>::CreateTask(&WaitOutstandingTasks, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(GET_STATID(STAT_RenderThreadTaskFence), ENamedThreads::GetRenderThread_Local());
	}
	return Result;
}

FGraphEventArray& FRHICommandListImmediate::GetRenderThreadTaskArray()
{
	check(IsInRenderingThread());
	return WaitOutstandingTasks;
}


void FRHICommandListImmediate::WaitOnRenderThreadTaskFence(FGraphEventRef& Fence)
{
	if (Fence.GetReference() && !Fence->IsComplete())
	{
		SCOPE_CYCLE_COUNTER(STAT_RenderThreadTaskFenceWait);
		ENamedThreads::Type RenderThread_Local = ENamedThreads::GetRenderThread_Local();
		check(IsInRenderingThread() && !FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local));
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(Fence, RenderThread_Local);
	}
}

bool FRHICommandListImmediate::AnyRenderThreadTasksOutstanding()
{
	return !!WaitOutstandingTasks.Num();
}


void FRHIAsyncComputeCommandListImmediate::ImmediateDispatch(FRHIAsyncComputeCommandListImmediate& RHIComputeCmdList)
{
	check(IsInRenderingThread());

	//queue a final command to submit all the async compute commands up to this point to the GPU.
	RHIComputeCmdList.SubmitCommandsHint();

	if (!RHIComputeCmdList.Bypass())
	{
		FRHIAsyncComputeCommandListImmediate* SwapCmdList;
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FRHICommandListExecutor_SwapCmdLists);
			SwapCmdList = new FRHIAsyncComputeCommandListImmediate();

			//hack stolen from Gfx commandlist.  transfer
			static_assert(sizeof(FRHICommandList) == sizeof(FRHIAsyncComputeCommandListImmediate), "We are memswapping FRHICommandList and FRHICommandListImmediate; they need to be swappable.");
			check(RHIComputeCmdList.IsImmediateAsyncCompute());
			SwapCmdList->ExchangeCmdList(RHIComputeCmdList);
			RHIComputeCmdList.CopyContext(*SwapCmdList);
			RHIComputeCmdList.GPUMask = SwapCmdList->GPUMask;
		#if RHI_WANT_BREADCRUMB_EVENTS
			FRHIBreadcrumbState BreadcrumbState;

			// Once executed, the memory containing the breadcrumbs will be freed, so any open markers are popped and stored into BreadcrumbState
			SwapCmdList->ExportBreadcrumbState(BreadcrumbState);
			SwapCmdList->ResetBreadcrumbs();

			// And then pushed into the newly opened list.
			RHIComputeCmdList.ImportBreadcrumbState(BreadcrumbState);
		#endif // RHI_WANT_BREADCRUMB_EVENTS

			// NB: InitialGPUMask set to GPUMask since exchanging the list
			// is equivalent to a Reset.
			RHIComputeCmdList.InitialGPUMask = SwapCmdList->GPUMask;
			RHIComputeCmdList.PSOContext = SwapCmdList->PSOContext;

			//queue the execution of this async commandlist amongst other commands in the immediate gfx list.
			//this guarantees resource update commands made on the gfx commandlist will be executed before the async compute.
			FRHICommandListImmediate& RHIImmCmdList = FRHICommandListExecutor::GetImmediateCommandList();
			RHIImmCmdList.QueueAsyncCompute(*SwapCmdList);

			//dispatch immediately to RHI Thread so we can get the async compute on the GPU ASAP.
			RHIImmCmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
		}
	}
}

FRHICommandBase* GCurrentCommand = nullptr;

DECLARE_CYCLE_STAT(TEXT("BigList"), STAT_BigList, STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("SmallList"), STAT_SmallList, STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("PTrans"), STAT_PTrans, STATGROUP_RHICMDLIST);

#if WITH_ADDITIONAL_CRASH_CONTEXTS && RHI_WANT_BREADCRUMB_EVENTS
static void WriteRenderBreadcrumbs(FCrashContextExtendedWriter& Writer, const FRHIBreadcrumb** BreadcrumbStack, uint32 BreadcrumbStackIndex, const TCHAR* ThreadName)
{
	enum
	{
		MAX_BREADCRUMBS = 64,
		MAX_BREADCRUMB_STRING = 4096,
		MAX_BREADCRUMB_NAME_STRING = 128,
	};
	static int BreadcrumbId = 0;

	TCHAR StaticBreadcrumbStackString[MAX_BREADCRUMB_STRING];
	size_t BreadcrumbStackStringSize = 0;

	auto WriteBreadcrumbLine = [&](const TCHAR* InFormat, ...)
	{
		if (BreadcrumbStackStringSize < MAX_BREADCRUMB_STRING)
		{
			int32 WrittenLength = 0;
			GET_VARARGS_RESULT(
				&StaticBreadcrumbStackString[BreadcrumbStackStringSize],
				MAX_BREADCRUMB_STRING - BreadcrumbStackStringSize,
				MAX_BREADCRUMB_STRING - BreadcrumbStackStringSize - 1,
				InFormat, InFormat, WrittenLength);

			BreadcrumbStackStringSize += WrittenLength;
		}
	};

	WriteBreadcrumbLine(TEXT("Breadcrumbs '%s'\n"), ThreadName);

	const uint32 NumBreadcrumbStacks = BreadcrumbStackIndex + 1;

	for (uint32 BreadcrumbIndex = 0; BreadcrumbIndex < NumBreadcrumbStacks; BreadcrumbIndex++)
	{
		if (const FRHIBreadcrumb* CurrentBreadcrumb = BreadcrumbStack[BreadcrumbStackIndex - BreadcrumbIndex])
		{
			const TCHAR* BreadcrumbNames[MAX_BREADCRUMBS];

			const FRHIBreadcrumb* Breadcrumb = CurrentBreadcrumb;
			int32 NameIndex = 0;
			while (Breadcrumb && NameIndex < MAX_BREADCRUMBS)
			{
				BreadcrumbNames[NameIndex++] = Breadcrumb->Name;
				Breadcrumb = Breadcrumb->Parent;
			}

			WriteBreadcrumbLine(TEXT("Context %d/%d\n"), BreadcrumbIndex + 1, NumBreadcrumbStacks);

			uint32 StackPos = 0;
			for (int32 i = NameIndex - 1; i >= 0; --i)
			{
				WriteBreadcrumbLine(TEXT("\t%02d %s\n"), StackPos++, BreadcrumbNames[i]);
			}
		}
	}

	TCHAR StaticBreadcrumbName[MAX_BREADCRUMB_NAME_STRING];
	FCString::Snprintf(StaticBreadcrumbName, MAX_BREADCRUMB_NAME_STRING, TEXT("Breadcrumbs_%s_%d"), ThreadName, BreadcrumbId++);
	Writer.AddString(StaticBreadcrumbName, StaticBreadcrumbStackString);
	UE_LOG(LogRHI, Error, StaticBreadcrumbStackString);
}
#endif

void FRHICommandListExecutor::ExecuteInner_DoExecute(FRHICommandListBase& CmdList)
{
	FScopeCycleCounter ScopeOuter(CmdList.ExecuteStat);

	CmdList.bExecuting = true;
	check(CmdList.Context || CmdList.ComputeContext);

	FMemMark Mark(FMemStack::Get());

#if WITH_ADDITIONAL_CRASH_CONTEXTS && RHI_WANT_BREADCRUMB_EVENTS
	IRHIComputeContext* LocalContext = CmdList.Context ? CmdList.Context : CmdList.ComputeContext;

	// Need a struct because Crash context scope only allows 1 argument
	struct
	{
		const TCHAR* ThreadName;
		const FRHIBreadcrumb** BreadcrumbStack;
		uint32 BreadcrumbStackIndex;
	} CrashState{};
	CrashState.ThreadName = TEXT("Parallel");
	if (IsInRenderingThread())
	{
		CrashState.ThreadName = TEXT("RenderingThread");
	}
	else if (IsInRHIThread())
	{
		CrashState.ThreadName = TEXT("RHIThread");
	}

	bool PopBreadcrumbStack = false;

	if (LocalContext)
	{
		if (LocalContext->BreadcrumbStackIndex < IRHIComputeContext::MaxBreadcrumbStacks - 1)
		{
			LocalContext->BreadcrumbStackIndex++;
			PopBreadcrumbStack = true;
		}

		// if we can't fit a next stack in, we have to stomp the top one, the show must go on.

		LocalContext->RHISetBreadcrumbStackTop(CmdList.BreadcrumbStack.PopFirstUnsubmittedBreadcrumb());

		CrashState.BreadcrumbStack = &LocalContext->BreadcrumbStackTop[0];
		CrashState.BreadcrumbStackIndex = LocalContext->BreadcrumbStackIndex;
	}

	UE_ADD_CRASH_CONTEXT_SCOPE([&CrashState](FCrashContextExtendedWriter& Writer)
	{
		WriteRenderBreadcrumbs(Writer, CrashState.BreadcrumbStack, CrashState.BreadcrumbStackIndex, CrashState.ThreadName);
	});

	ON_SCOPE_EXIT
	{
		if (PopBreadcrumbStack)
		{
			LocalContext->BreadcrumbStackIndex--;
		}
	};
#endif

#if WITH_MGPU
	// Set the initial GPU mask on the contexts before executing any commands.
    // This avoids having to ensure that every command list has an initial
    // FRHICommandSetGPUMask at the root.
	if (CmdList.Context != nullptr)
	{
		CmdList.Context->RHISetGPUMask(CmdList.InitialGPUMask);
	}
	if (CmdList.ComputeContext != nullptr && CmdList.ComputeContext != CmdList.Context)
	{
		CmdList.ComputeContext->RHISetGPUMask(CmdList.InitialGPUMask);
	}
#endif

	FRHICommandListDebugContext DebugContext;
	FRHICommandListIterator Iter(CmdList);
#if STATS
	bool bDoStats =  CVarRHICmdCollectRHIThreadStatsFromHighLevel.GetValueOnRenderThread() > 0 && FThreadStats::IsCollectingData() && (IsInRenderingThread() || IsInRHIThread());
	if (bDoStats)
	{
		while (Iter.HasCommandsLeft())
		{
			TStatIdData const* Stat = GCurrentExecuteStat.GetRawPointer();
			FScopeCycleCounter Scope(GCurrentExecuteStat);
			while (Iter.HasCommandsLeft() && Stat == GCurrentExecuteStat.GetRawPointer())
			{
				FRHICommandBase* Cmd = Iter.NextCommand();
				//FPlatformMisc::Prefetch(Cmd->Next);
				Cmd->ExecuteAndDestruct(CmdList, DebugContext);
			}
		}
	}
	else
#elif ENABLE_STATNAMEDEVENTS
	bool bDoStats = CVarRHICmdCollectRHIThreadStatsFromHighLevel.GetValueOnRenderThread() > 0 && GCycleStatsShouldEmitNamedEvents && (IsInRenderingThread() || IsInRHIThread());
	if (bDoStats)
	{
		while (Iter.HasCommandsLeft())
		{
			PROFILER_CHAR const* Stat = GCurrentExecuteStat.StatString;
			FScopeCycleCounter Scope(GCurrentExecuteStat);
			while (Iter.HasCommandsLeft() && Stat == GCurrentExecuteStat.StatString)
			{
				FRHICommandBase* Cmd = Iter.NextCommand();
				//FPlatformMisc::Prefetch(Cmd->Next);
				Cmd->ExecuteAndDestruct(CmdList, DebugContext);
			}
		}
	}
	else
#endif
	{
		while (Iter.HasCommandsLeft())
		{
			FRHICommandBase* Cmd = Iter.NextCommand();
			GCurrentCommand = Cmd;
			//FPlatformMisc::Prefetch(Cmd->Next);
			Cmd->ExecuteAndDestruct(CmdList, DebugContext);
		}
	}
	CmdList.Reset();
}


static FAutoConsoleTaskPriority CPrio_RHIThreadOnTaskThreads(
	TEXT("TaskGraph.TaskPriorities.RHIThreadOnTaskThreads"),
	TEXT("Task and thread priority for when we are running 'RHI thread' tasks on any thread."),
	ENamedThreads::NormalThreadPriority,
	ENamedThreads::NormalTaskPriority
	);


static FCriticalSection GRHIThreadOnTasksCritical;
static std::atomic<int32> GRHIThreadStallRequestCount;

class FExecuteRHIThreadTask
{
	FRHICommandListBase* RHICmdList;

public:

	FExecuteRHIThreadTask(FRHICommandListBase* InRHICmdList)
		: RHICmdList(InRHICmdList)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FExecuteRHIThreadTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		check(IsRunningRHIInSeparateThread()); // this should never be used on a platform that doesn't support the RHI thread
		return IsRunningRHIInDedicatedThread() ? ENamedThreads::RHIThread : CPrio_RHIThreadOnTaskThreads.Get();
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FTaskTagScope Scope(ETaskTag::ERhiThread);
		SCOPE_CYCLE_COUNTER(STAT_RHIThreadExecute);
		if (IsRunningRHIInTaskThread())
		{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			GRHIThreadId = FPlatformTLS::GetCurrentThreadId();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
		{
			FScopeLock Lock(&GRHIThreadOnTasksCritical);
			GWorkingRHIThreadStartCycles = FPlatformTime::Cycles();

			FRHICommandListExecutor::ExecuteInner_DoExecute(*RHICmdList);
			delete RHICmdList;

			GWorkingRHIThreadTime += (FPlatformTime::Cycles() - GWorkingRHIThreadStartCycles); // this subtraction often wraps and the math stuff works
		}
		if (IsRunningRHIInTaskThread())
		{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			GRHIThreadId = 0;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
	}
};

class FDispatchRHIThreadTask
{
	FRHICommandListBase* RHICmdList;
	bool bRHIThread;

public:

	FDispatchRHIThreadTask(FRHICommandListBase* InRHICmdList, bool bInRHIThread)
		: RHICmdList(InRHICmdList)
		, bRHIThread(bInRHIThread)
	{		
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FDispatchRHIThreadTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		// If we are using async dispatch, this task is somewhat redundant, but it does allow things to wait for dispatch without waiting for execution. 
		// since in that case we will be queuing an rhithread task from an rhithread task, the overhead is minor.
		check(IsRunningRHIInSeparateThread()); // this should never be used on a platform that doesn't support the RHI thread
		return bRHIThread ? (IsRunningRHIInDedicatedThread() ? ENamedThreads::RHIThread : CPrio_RHIThreadOnTaskThreads.Get()) : ENamedThreads::GetRenderThread_Local();
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		check(bRHIThread || IsInRenderingThread());
		FGraphEventArray Prereq;
		if (RHIThreadTask.GetReference())
		{
			Prereq.Add(RHIThreadTask);
		}
		PrevRHIThreadTask = RHIThreadTask;
		RHIThreadTask = TGraphTask<FExecuteRHIThreadTask>::CreateTask(&Prereq, CurrentThread).ConstructAndDispatchWhenReady(RHICmdList);
	}
};

void FRHICommandListExecutor::ExecuteInner(FRHICommandListBase& CmdList)
{
	check(CmdList.HasCommands());

	FRHIComputeCommandList& ComputeCommandList = (FRHIComputeCommandList&)CmdList;

#if RHI_WANT_BREADCRUMB_EVENTS
	FRHIBreadcrumbState BreadcrumbState;
	// Once executed, the memory containing the breadcrumbs will be freed, so any open markers are popped and stored into BreadcrumbState
	ComputeCommandList.ExportBreadcrumbState(BreadcrumbState);
	ComputeCommandList.ResetBreadcrumbs();

	ON_SCOPE_EXIT
	{
		// And then pushed into the newly opened list.
		ComputeCommandList.ImportBreadcrumbState(BreadcrumbState);
	};
#endif // RHI_WANT_BREADCRUMB_EVENTS

	bool bIsInRenderingThread = IsInRenderingThread();
	bool bIsInGameThread = IsInGameThread();
	if (IsRunningRHIInSeparateThread())
	{
		bool bAsyncSubmit = false;
		ENamedThreads::Type RenderThread_Local = ENamedThreads::GetRenderThread_Local();
		if (bIsInRenderingThread)
		{
			if (!bIsInGameThread && !FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local))
			{
				// move anything down the pipe that needs to go
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(RenderThread_Local);
			}
			bAsyncSubmit = CVarRHICmdAsyncRHIThreadDispatch.GetValueOnRenderThread() > 0;
			if (RenderThreadSublistDispatchTask.GetReference() && RenderThreadSublistDispatchTask->IsComplete())
			{
#if NEEDS_DEBUG_INFO_ON_PRESENT_HANG
				bRenderThreadSublistDispatchTaskClearedOnRT = IsInActualRenderingThread();
				bRenderThreadSublistDispatchTaskClearedOnGT = bIsInGameThread;
#endif
				RenderThreadSublistDispatchTask = nullptr;
				if (bAsyncSubmit && RHIThreadTask.GetReference() && RHIThreadTask->IsComplete())
			{
				RHIThreadTask = nullptr;
				PrevRHIThreadTask = nullptr;

			}
			}
			if (!bAsyncSubmit && RHIThreadTask.GetReference() && RHIThreadTask->IsComplete())
			{
				RHIThreadTask = nullptr;
				PrevRHIThreadTask = nullptr;
			}
		}
		if (CVarRHICmdUseThread.GetValueOnRenderThread() > 0 && bIsInRenderingThread && !bIsInGameThread)
		{
			FRHICommandList* SwapCmdList;
			FGraphEventArray Prereq;
			Exchange(Prereq, CmdList.RTTasks); 
			{
				SwapCmdList = new FRHICommandList(CmdList.GetGPUMask());

				// Super scary stuff here, but we just want the swap command list to inherit everything and leave the immediate command list wiped.
				// we should make command lists virtual and transfer ownership rather than this devious approach
				static_assert(sizeof(FRHICommandList) == sizeof(FRHICommandListImmediate), "We are memswapping FRHICommandList and FRHICommandListImmediate; they need to be swappable.");
				SwapCmdList->ExchangeCmdList(CmdList);
				CmdList.CopyContext(*SwapCmdList);
				CmdList.GPUMask = SwapCmdList->GPUMask;
				// NB: InitialGPUMask set to GPUMask since exchanging the list
                // is equivalent to a Reset.
				CmdList.InitialGPUMask = SwapCmdList->GPUMask;
				CmdList.PSOContext = SwapCmdList->PSOContext;
				CmdList.Data.bInsideRenderPass = SwapCmdList->Data.bInsideRenderPass;
				CmdList.Data.bInsideComputePass = SwapCmdList->Data.bInsideComputePass;
			}

			//if we use a FDispatchRHIThreadTask, we must have it pass an event along to the FExecuteRHIThreadTask it will spawn so that fences can know which event to wait on for execution completion
			//before the dispatch completes.
			//if we use a FExecuteRHIThreadTask directly we pass the same event just to keep things consistent.
			if (AllOutstandingTasks.Num() || RenderThreadSublistDispatchTask.GetReference())
			{
				Prereq.Append(AllOutstandingTasks);
				AllOutstandingTasks.Reset();
				if (RenderThreadSublistDispatchTask.GetReference())
				{
					Prereq.Add(RenderThreadSublistDispatchTask);
				}
#if NEEDS_DEBUG_INFO_ON_PRESENT_HANG
				RenderThreadSublistDispatchTaskPrereqs = Prereq;
#endif
				RenderThreadSublistDispatchTask = TGraphTask<FDispatchRHIThreadTask>::CreateTask(&Prereq, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(SwapCmdList, bAsyncSubmit);
			}
			else
			{
				check(!RenderThreadSublistDispatchTask.GetReference()); // if we are doing submits, there better not be any of these in flight since then the RHIThreadTask would get out of order.
				if (RHIThreadTask.GetReference())
				{
					Prereq.Add(RHIThreadTask);
				}
				PrevRHIThreadTask = RHIThreadTask;
				RHIThreadTask = TGraphTask<FExecuteRHIThreadTask>::CreateTask(&Prereq, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(SwapCmdList);
			}
			if (CVarRHICmdForceRHIFlush.GetValueOnRenderThread() > 0 )
			{
				if (FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local))
				{
					// this is a deadlock. RT tasks must be done by now or they won't be done. We could add a third queue...
					UE_LOG(LogRHI, Fatal, TEXT("Deadlock in FRHICommandListExecutor::ExecuteInner 2."));
				}
				if (RenderThreadSublistDispatchTask.GetReference())
				{
					FTaskGraphInterface::Get().WaitUntilTaskCompletes(RenderThreadSublistDispatchTask, RenderThread_Local);
#if NEEDS_DEBUG_INFO_ON_PRESENT_HANG
					bRenderThreadSublistDispatchTaskClearedOnRT = IsInActualRenderingThread();
					bRenderThreadSublistDispatchTaskClearedOnGT = bIsInGameThread;
#endif
					RenderThreadSublistDispatchTask = nullptr;
				}
				while (RHIThreadTask.GetReference())
				{
					FTaskGraphInterface::Get().WaitUntilTaskCompletes(RHIThreadTask, RenderThread_Local);
					if (RHIThreadTask.GetReference() && RHIThreadTask->IsComplete())
					{
						RHIThreadTask = nullptr;
						PrevRHIThreadTask = nullptr;
					}
				}
			}
			return;
		}
		if (bIsInRenderingThread)
		{
			if (CmdList.RTTasks.Num())
			{
				if (FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local))
				{
					// this is a deadlock. RT tasks must be done by now or they won't be done. We could add a third queue...
					UE_LOG(LogRHI, Fatal, TEXT("Deadlock in FRHICommandListExecutor::ExecuteInner (RTTasks)."));
				}
				FTaskGraphInterface::Get().WaitUntilTasksComplete(CmdList.RTTasks, RenderThread_Local);
				CmdList.RTTasks.Reset();

			}
			if (RenderThreadSublistDispatchTask.GetReference())
			{
				if (FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local))
				{
					// this is a deadlock. RT tasks must be done by now or they won't be done. We could add a third queue...
					UE_LOG(LogRHI, Fatal, TEXT("Deadlock in FRHICommandListExecutor::ExecuteInner (RenderThreadSublistDispatchTask)."));
				}
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(RenderThreadSublistDispatchTask, RenderThread_Local);
#if NEEDS_DEBUG_INFO_ON_PRESENT_HANG
				bRenderThreadSublistDispatchTaskClearedOnRT = IsInActualRenderingThread();
				bRenderThreadSublistDispatchTaskClearedOnGT = bIsInGameThread;
#endif
				RenderThreadSublistDispatchTask = nullptr;
			}
			while (RHIThreadTask.GetReference())
			{
				if (FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local))
				{
					// this is a deadlock. RT tasks must be done by now or they won't be done. We could add a third queue...
					UE_LOG(LogRHI, Fatal, TEXT("Deadlock in FRHICommandListExecutor::ExecuteInner (RHIThreadTask)."));
				}
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(RHIThreadTask, RenderThread_Local);
				if (RHIThreadTask.GetReference() && RHIThreadTask->IsComplete())
				{
					RHIThreadTask = nullptr;
					PrevRHIThreadTask = nullptr;
				}
			}
		}
	}
	else
	{
		if (bIsInRenderingThread && CmdList.RTTasks.Num())
		{
			ENamedThreads::Type RenderThread_Local = ENamedThreads::GetRenderThread_Local();
			if (FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local))
			{
				// this is a deadlock. RT tasks must be done by now or they won't be done. We could add a third queue...
				UE_LOG(LogRHI, Fatal, TEXT("Deadlock in FRHICommandListExecutor::ExecuteInner (RTTasks)."));
			}
			FTaskGraphInterface::Get().WaitUntilTasksComplete(CmdList.RTTasks, RenderThread_Local);
			CmdList.RTTasks.Reset();
		}
	}

	ExecuteInner_DoExecute(CmdList);
}


static FORCEINLINE bool IsInRenderingOrRHIThread()
{
	return IsInRenderingThread() || IsInRHIThread();
}

void FRHICommandListExecutor::ExecuteList(FRHICommandListBase& CmdList)
{
	LLM_SCOPE_BYNAME(TEXT("FRHICommandListExecutorExecuteList"));

	check(&CmdList != &GetImmediateCommandList() && (GRHISupportsParallelRHIExecute || IsInRenderingOrRHIThread()));

	if (IsInRenderingThread() && !GetImmediateCommandList().IsExecuting()) // don't flush if this is a recursive call and we are already executing the immediate command list
	{
		GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	}

	INC_MEMORY_STAT_BY(STAT_NonImmedCmdListMemory, CmdList.GetUsedMemory());
	INC_DWORD_STAT_BY(STAT_NonImmedCmdListCount, CmdList.NumCommands);

	SCOPE_CYCLE_COUNTER(STAT_NonImmedCmdListExecuteTime);
	ExecuteInner(CmdList);
}

void FRHICommandListExecutor::ExecuteList(FRHICommandListImmediate& CmdList)
{
	check(IsInRenderingOrRHIThread() && &CmdList == &GetImmediateCommandList());

	INC_MEMORY_STAT_BY(STAT_ImmedCmdListMemory, CmdList.GetUsedMemory());
	INC_DWORD_STAT_BY(STAT_ImmedCmdListCount, CmdList.NumCommands);
#if 0
	static TAutoConsoleVariable<int32> CVarRHICmdMemDump(
		TEXT("r.RHICmdMemDump"),
		0,
		TEXT("dumps callstacks and sizes of the immediate command lists to the console.\n")
		TEXT("0: Disable, 1: Enable"),
		ECVF_Cheat);
	if (CVarRHICmdMemDump.GetValueOnRenderThread() > 0)
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Mem %d\n"), CmdList.GetUsedMemory());
		if (CmdList.GetUsedMemory() > 300)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("big\n"));
		}
	}
#endif
	{
		SCOPE_CYCLE_COUNTER(STAT_ImmedCmdListExecuteTime);
		ExecuteInner(CmdList);
	}
}

void FRHICommandListExecutor::LatchBypass()
{
#if CAN_TOGGLE_COMMAND_LIST_BYPASS
	if (IsRunningRHIInSeparateThread())
	{
		if (bLatchedBypass)
		{
			check((GRHICommandList.OutstandingCmdListCount.GetValue() == 2 && !GRHICommandList.GetImmediateCommandList().HasCommands()) && !GRHICommandList.GetImmediateAsyncComputeCommandList().HasCommands());
			bLatchedBypass = false;
		}
	}
	else
	{
		GRHICommandList.GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);		

		static bool bOnce = false;
		if (!bOnce)
		{
			bOnce = true;
			if (FParse::Param(FCommandLine::Get(),TEXT("forcerhibypass")) && CVarRHICmdBypass.GetValueOnRenderThread() == 0)
			{
				IConsoleVariable* BypassVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RHICmdBypass"));
				BypassVar->Set(1, ECVF_SetByCommandline);
			}
			else if (FParse::Param(FCommandLine::Get(),TEXT("parallelrendering")) && CVarRHICmdBypass.GetValueOnRenderThread() >= 1)
			{
				IConsoleVariable* BypassVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RHICmdBypass"));
				BypassVar->Set(0, ECVF_SetByCommandline);
			}
		}

		check((GRHICommandList.OutstandingCmdListCount.GetValue() == 2 && !GRHICommandList.GetImmediateCommandList().HasCommands() && !GRHICommandList.GetImmediateAsyncComputeCommandList().HasCommands()));

		check(!GDynamicRHI || IsInRenderingThread());
		bool NewBypass = IsInGameThread() || (CVarRHICmdBypass.GetValueOnAnyThread() >= 1);

		if (NewBypass && !bLatchedBypass)
		{
			FRHIResource::FlushPendingDeletes(FRHICommandListExecutor::GetImmediateCommandList());
		}
		bLatchedBypass = NewBypass;
	}
#endif
	if (bLatchedBypass || (!GSupportsParallelRenderingTasksWithSeparateRHIThread && IsRunningRHIInSeparateThread()))
	{
		bLatchedUseParallelAlgorithms = false;
	}
	else
	{
		bLatchedUseParallelAlgorithms = FApp::ShouldUseThreadingForPerformance() 
#if CAN_TOGGLE_COMMAND_LIST_BYPASS
			&& (CVarRHICmdUseParallelAlgorithms.GetValueOnAnyThread() >= 1)
#endif
			;
	}
}

void FRHICommandListExecutor::CheckNoOutstandingCmdLists()
{
	// else we are attempting to delete resources while there is still a live cmdlist (other than the immediate cmd list) somewhere.
	checkf(GRHICommandList.OutstandingCmdListCount.GetValue() == 2, TEXT("Oustanding: %i"), GRHICommandList.OutstandingCmdListCount.GetValue());
}

bool FRHICommandListExecutor::IsRHIThreadActive()
{
	checkSlow(IsInRenderingThread());
	bool bAsyncSubmit = CVarRHICmdAsyncRHIThreadDispatch.GetValueOnRenderThread() > 0;
	if (bAsyncSubmit)
	{
		if (RenderThreadSublistDispatchTask.GetReference() && RenderThreadSublistDispatchTask->IsComplete())
		{
#if NEEDS_DEBUG_INFO_ON_PRESENT_HANG
			bRenderThreadSublistDispatchTaskClearedOnRT = IsInActualRenderingThread();
			bRenderThreadSublistDispatchTaskClearedOnGT = IsInGameThread();
#endif
			RenderThreadSublistDispatchTask = nullptr;
		}
		if (RenderThreadSublistDispatchTask.GetReference())
		{
			return true; // it might become active at any time
		}
		// otherwise we can safely look at RHIThreadTask
	}

	if (RHIThreadTask.GetReference() && RHIThreadTask->IsComplete())
	{
		RHIThreadTask = nullptr;
		PrevRHIThreadTask = nullptr;
	}
	return !!RHIThreadTask.GetReference();
}

bool FRHICommandListExecutor::IsRHIThreadCompletelyFlushed()
{
	if (IsRHIThreadActive() || GetImmediateCommandList().HasCommands())
	{
		return false;
	}
	if (RenderThreadSublistDispatchTask.GetReference() && RenderThreadSublistDispatchTask->IsComplete())
	{
#if NEEDS_DEBUG_INFO_ON_PRESENT_HANG
		bRenderThreadSublistDispatchTaskClearedOnRT = IsInActualRenderingThread();
		bRenderThreadSublistDispatchTaskClearedOnGT = IsInGameThread();
#endif
		RenderThreadSublistDispatchTask = nullptr;
	}
	return !RenderThreadSublistDispatchTask;
}

FRHICOMMAND_MACRO(FRHICommandRHIThreadFence)
{
	FGraphEventRef Fence;
	FORCEINLINE_DEBUGGABLE FRHICommandRHIThreadFence()
		: Fence(FGraphEvent::CreateGraphEvent())
{
	}
	void Execute(FRHICommandListBase& CmdList)
	{
		check(IsInRHIThread());
		Fence->DispatchSubsequents(IsRunningRHIInDedicatedThread() ? ENamedThreads::RHIThread : ENamedThreads::AnyThread);
		Fence = nullptr;
	}
};


FGraphEventRef FRHICommandListImmediate::RHIThreadFence(bool bSetLockFence)
{
	check(IsInRenderingThread());

	if (IsRunningRHIInSeparateThread())
	{
		FRHICommandRHIThreadFence* Cmd = ALLOC_COMMAND(FRHICommandRHIThreadFence)();
		if (bSetLockFence)
		{
			RHIThreadBufferLockFence = Cmd->Fence;
		}
		return Cmd->Fence;
	}

	return nullptr;
}

DECLARE_CYCLE_STAT(TEXT("Async Compute CmdList Execute"), STAT_AsyncComputeExecute, STATGROUP_RHICMDLIST);
FRHICOMMAND_MACRO(FRHIAsyncComputeSubmitList)
{
	FRHIComputeCommandList* RHICmdList;
	FORCEINLINE_DEBUGGABLE FRHIAsyncComputeSubmitList(FRHIComputeCommandList* InRHICmdList)
		: RHICmdList(InRHICmdList)
	{
	}
	void Execute(FRHICommandListBase& CmdList)
	{
		SCOPE_CYCLE_COUNTER(STAT_AsyncComputeExecute);
		delete RHICmdList;
	}
};

void FRHICommandListImmediate::QueueAsyncCompute(FRHIComputeCommandList& RHIComputeCmdList)
{
	if (Bypass())
	{
		SCOPE_CYCLE_COUNTER(STAT_AsyncComputeExecute);
		delete &RHIComputeCmdList;
		return;
	}
	ALLOC_COMMAND(FRHIAsyncComputeSubmitList)(&RHIComputeCmdList);
}
	
void FRHICommandListExecutor::WaitOnRHIThreadFence(FGraphEventRef& Fence)
{
	check(IsInRenderingThread());
	if (Fence.GetReference() && !Fence->IsComplete())
	{
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_WaitOnRHIThreadFence_Dispatch);
			GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread); // necessary to prevent deadlock
		}
		check(IsRunningRHIInSeparateThread());
		QUICK_SCOPE_CYCLE_COUNTER(STAT_WaitOnRHIThreadFence_Wait);
		ENamedThreads::Type RenderThread_Local = ENamedThreads::GetRenderThread_Local();
		if (FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local))
		{
			// this is a deadlock. RT tasks must be done by now or they won't be done. We could add a third queue...
			UE_LOG(LogRHI, Fatal, TEXT("Deadlock in WaitOnRHIThreadFence."));
		}
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(Fence, RenderThread_Local);
	}
}

void FRHICommandListExecutor::Transition(TArrayView<const FRHITransitionInfo> Infos, ERHIPipeline SrcPipelines, ERHIPipeline DstPipelines)
{
	check(IsInRenderingThread());

	FRHIAsyncComputeCommandListImmediate& RHICmdListAsyncCompute = GetImmediateAsyncComputeCommandList();

#if DO_CHECK
	for (const FRHITransitionInfo& Info : Infos)
	{
		checkf(Info.IsWholeResource(), TEXT("Only whole resource transitions are allowed in FRHICommandListExecutor::Transition."));
	}
#endif

	if (!GSupportsEfficientAsyncCompute || RHICmdListAsyncCompute.Bypass())
	{
		checkf(SrcPipelines != ERHIPipeline::AsyncCompute, TEXT("Async compute is disabled. Cannot transition from it."));
		checkf(DstPipelines != ERHIPipeline::AsyncCompute, TEXT("Async compute is disabled. Cannot transition to it."));

		EnumRemoveFlags(SrcPipelines, ERHIPipeline::AsyncCompute);
		EnumRemoveFlags(DstPipelines, ERHIPipeline::AsyncCompute);
	}

	TRHIPipelineArray<FRHIComputeCommandList*> CommandLists;
	CommandLists[ERHIPipeline::Graphics]     = &GetImmediateCommandList();
	CommandLists[ERHIPipeline::AsyncCompute] = &RHICmdListAsyncCompute;

	const FRHITransition* Transition = RHICreateTransition({ SrcPipelines, DstPipelines, ERHITransitionCreateFlags::None, Infos });

	EnumerateRHIPipelines(SrcPipelines, [&](ERHIPipeline Pipeline)
	{
		CommandLists[Pipeline]->BeginTransition(Transition);
	});

	EnumerateRHIPipelines(DstPipelines, [&](ERHIPipeline Pipeline)
	{
		CommandLists[Pipeline]->EndTransition(Transition);
	});

	if (EnumHasAnyFlags(SrcPipelines | DstPipelines, ERHIPipeline::AsyncCompute))
	{
		FRHIAsyncComputeCommandListImmediate::ImmediateDispatch(RHICmdListAsyncCompute);
	}

	if (EnumHasAnyFlags(SrcPipelines | DstPipelines, ERHIPipeline::Graphics))
	{
		CommandLists[ERHIPipeline::Graphics]->SetTrackedAccess(Infos);
	}
}

FRHICommandListBase::FRHICommandListBase(FRHIGPUMask InGPUMask)
	: Root(nullptr)
	, CommandLink(nullptr)
	, bExecuting(false)
	, NumCommands(0)
	, UID(UINT32_MAX)
	, Context(nullptr)
	, ComputeContext(nullptr)
	, MemManager()
	, bAsyncPSOCompileAllowed(true)
	, GPUMask(InGPUMask)
	, InitialGPUMask(InGPUMask)
	, BoundComputeShaderRHI(nullptr)
{
	GRHICommandList.OutstandingCmdListCount.Increment();
	Reset();
}

FRHICommandListBase::~FRHICommandListBase()
{
	Flush();
	GRHICommandList.OutstandingCmdListCount.Decrement();
}

const int32 FRHICommandListBase::GetUsedMemory() const
{
	return MemManager.GetByteCount();
}

void FRHICommandListBase::Reset()
{
	bExecuting = false;
	check(!RTTasks.Num());
	MemManager.Flush();
	NumCommands = 0;
	Root = nullptr;
	CommandLink = &Root;
	UID = GRHICommandList.UIDCounter.Increment();
	ExecuteStat = TStatId();

	InitialGPUMask = GPUMask;

#if RHI_WANT_BREADCRUMB_EVENTS
	BreadcrumbStack.ValidateEmpty();
	BreadcrumbStack.Reset();
#endif
}

void FRHICommandListBase::MaybeDispatchToRHIThreadInner()
{
	if (!PrevRHIThreadTask.GetReference() || PrevRHIThreadTask->IsComplete())
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	}
}

DECLARE_CYCLE_STAT(TEXT("Parallel Async Chain Translate"), STAT_ParallelChainTranslate, STATGROUP_RHICMDLIST);

FAutoConsoleTaskPriority CPrio_FParallelTranslateCommandList(
	TEXT("TaskGraph.TaskPriorities.ParallelTranslateCommandList"),
	TEXT("Task and thread priority for FParallelTranslateCommandList."),
	ENamedThreads::NormalThreadPriority,
	ENamedThreads::NormalTaskPriority
	);

FAutoConsoleTaskPriority CPrio_FParallelTranslateCommandListPrepass(
	TEXT("TaskGraph.TaskPriorities.ParallelTranslateCommandListPrepass"),
	TEXT("Task and thread priority for FParallelTranslateCommandList for the prepass, which we would like to get to the GPU asap."),
	ENamedThreads::NormalThreadPriority,
	ENamedThreads::HighTaskPriority
	);

class FParallelTranslateCommandList
{
	FRHICommandListBase** RHICmdLists;
	int32 NumCommandLists;
	IRHICommandContextContainer* ContextContainer;
	bool bIsPrepass;
public:

	FParallelTranslateCommandList(FRHICommandListBase** InRHICmdLists, int32 InNumCommandLists, IRHICommandContextContainer* InContextContainer, bool bInIsPrepass)
		: RHICmdLists(InRHICmdLists)
		, NumCommandLists(InNumCommandLists)
		, ContextContainer(InContextContainer)
		, bIsPrepass(bInIsPrepass)
	{
		check(RHICmdLists && ContextContainer && NumCommandLists);
	}

	static FORCEINLINE TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FParallelTranslateCommandList, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return bIsPrepass ? CPrio_FParallelTranslateCommandListPrepass.Get() : CPrio_FParallelTranslateCommandList.Get();
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FOptionalTaskTagScope Scope(ETaskTag::EParallelRhiThread);
		SCOPE_CYCLE_COUNTER(STAT_ParallelChainTranslate);
		SCOPED_NAMED_EVENT(FParallelTranslateCommandList_DoTask, FColor::Magenta);
		check(ContextContainer && RHICmdLists);

		IRHICommandContext* Context = ContextContainer->GetContext();
		check(Context);
		for (int32 Index = 0; Index < NumCommandLists; Index++)
		{
			RHICmdLists[Index]->SetContext(Context);
			delete RHICmdLists[Index];
		}
		ContextContainer->FinishContext();
	}
};

DECLARE_DWORD_COUNTER_STAT(TEXT("Num Parallel Async Chains Links"), STAT_ParallelChainLinkCount, STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("Wait for Parallel Async CmdList"), STAT_ParallelChainWait, STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("Parallel Async Chain Execute"), STAT_ParallelChainExecute, STATGROUP_RHICMDLIST);

FRHICOMMAND_MACRO(FRHICommandWaitForAndSubmitSubListParallel)
{
	FGraphEventRef TranslateCompletionEvent;
	IRHICommandContextContainer* ContextContainer;
	int32 Num;
	int32 Index;

	FORCEINLINE_DEBUGGABLE FRHICommandWaitForAndSubmitSubListParallel(FGraphEventRef& InTranslateCompletionEvent, IRHICommandContextContainer* InContextContainer, int32 InNum, int32 InIndex)
		: TranslateCompletionEvent(InTranslateCompletionEvent)
		, ContextContainer(InContextContainer)
		, Num(InNum)
		, Index(InIndex)
	{
		check(ContextContainer && Num);
	}
	void Execute(FRHICommandListBase& CmdList)
	{
		check(ContextContainer && Num && IsInRHIThread());
		INC_DWORD_STAT_BY(STAT_ParallelChainLinkCount, 1);

		if (TranslateCompletionEvent.GetReference() && !TranslateCompletionEvent->IsComplete())
		{
			SCOPE_CYCLE_COUNTER(STAT_ParallelChainWait);
			if (IsInRenderingThread())
			{
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(TranslateCompletionEvent, ENamedThreads::GetRenderThread_Local());
			}
			else if (IsInRHIThread())
			{
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(TranslateCompletionEvent, IsRunningRHIInDedicatedThread() ? ENamedThreads::RHIThread : ENamedThreads::AnyThread);
			}
			else
			{
				check(0);
			}
		}
		{
			SCOPE_CYCLE_COUNTER(STAT_ParallelChainExecute);
			ContextContainer->SubmitAndFreeContextContainer(Index, Num);
		}
	}
};



DECLARE_DWORD_COUNTER_STAT(TEXT("Num Async Chains Links"), STAT_ChainLinkCount, STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("Wait for Async CmdList"), STAT_ChainWait, STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("Async Chain Execute"), STAT_ChainExecute, STATGROUP_RHICMDLIST);

FGraphEvent* GEventToWaitFor = nullptr;

FRHICOMMAND_MACRO(FRHICommandWaitForAndSubmitSubList)
{
	FGraphEventRef EventToWaitFor;
	FRHICommandListBase* RHICmdList;
	FORCEINLINE_DEBUGGABLE FRHICommandWaitForAndSubmitSubList(FGraphEventRef& InEventToWaitFor, FRHICommandListBase* InRHICmdList)
		: EventToWaitFor(InEventToWaitFor)
		, RHICmdList(InRHICmdList)
	{
	}
	void Execute(FRHICommandListBase& CmdList)
	{
		INC_DWORD_STAT_BY(STAT_ChainLinkCount, 1);
		if (EventToWaitFor.GetReference() && !EventToWaitFor->IsComplete() && !(!IsRunningRHIInSeparateThread() || !IsInRHIThread()))
		{
			GEventToWaitFor = EventToWaitFor.GetReference();
			UE_DEBUG_BREAK();
			check(EventToWaitFor->IsComplete());
		}
		if (EventToWaitFor.GetReference() && !EventToWaitFor->IsComplete())
		{
			check(!IsRunningRHIInSeparateThread() || !IsInRHIThread()); // things should not be dispatched if they can't complete without further waits
			SCOPE_CYCLE_COUNTER(STAT_ChainWait);
			if (IsInRenderingThread())
			{
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(EventToWaitFor, ENamedThreads::GetRenderThread_Local());
			}
			else
			{
				check(0);
			}
		}
		{
			SCOPE_CYCLE_COUNTER(STAT_ChainExecute);
			RHICmdList->CopyContext(CmdList);
			delete RHICmdList;
		}
	}
};


DECLARE_CYCLE_STAT(TEXT("Parallel Setup Translate"), STAT_ParallelSetupTranslate, STATGROUP_RHICMDLIST);

FAutoConsoleTaskPriority CPrio_FParallelTranslateSetupCommandList(
	TEXT("TaskGraph.TaskPriorities.ParallelTranslateSetupCommandList"),
	TEXT("Task and thread priority for FParallelTranslateSetupCommandList."),
	ENamedThreads::HighThreadPriority, // if we have high priority task threads, then use them...
	ENamedThreads::HighTaskPriority, // .. at high task priority
	ENamedThreads::HighTaskPriority // if we don't have hi pri threads, then use normal priority threads at high task priority instead
	);

class FParallelTranslateSetupCommandList
{
	FRHICommandList* RHICmdList;
	FRHICommandListBase** RHICmdLists;
	int32 NumCommandLists;
	bool bIsPrepass;
	int32 MinSize;
	int32 MinCount;
public:

	FParallelTranslateSetupCommandList(FRHICommandList* InRHICmdList, FRHICommandListBase** InRHICmdLists, int32 InNumCommandLists, bool bInIsPrepass)
		: RHICmdList(InRHICmdList)
		, RHICmdLists(InRHICmdLists)
		, NumCommandLists(InNumCommandLists)
		, bIsPrepass(bInIsPrepass)
	{
		check(RHICmdList && RHICmdLists && NumCommandLists);
		MinSize = CVarRHICmdMinCmdlistSizeForParallelTranslate.GetValueOnRenderThread() * 1024;
		MinCount = CVarRHICmdMinCmdlistForParallelTranslate.GetValueOnRenderThread();
	}

	static FORCEINLINE TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FParallelTranslateSetupCommandList, STATGROUP_TaskGraphTasks);
	}

	static FORCEINLINE ENamedThreads::Type GetDesiredThread()
	{
		return CPrio_FParallelTranslateSetupCommandList.Get();
	}

	static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		SCOPE_CYCLE_COUNTER(STAT_ParallelSetupTranslate);

		TArray<int32, TInlineAllocator<64> > Sizes;
		Sizes.Reserve(NumCommandLists);
		for (int32 Index = 0; Index < NumCommandLists; Index++)
		{
			Sizes.Add(RHICmdLists[Index]->GetUsedMemory());
		}

		int32 EffectiveThreads = 0;
		int32 Start = 0;
		// this is pretty silly but we need to know the number of jobs in advance, so we run the merge logic twice
		while (Start < NumCommandLists)
		{
			int32 Last = Start;
			int32 DrawCnt = Sizes[Start];

			while (Last < NumCommandLists - 1 && DrawCnt + Sizes[Last + 1] <= MinSize)
			{
				Last++;
				DrawCnt += Sizes[Last];
			}
			check(Last >= Start);
			Start = Last + 1;
			EffectiveThreads++;
		} 

		if (EffectiveThreads < MinCount)
		{
			FGraphEventRef Nothing;
			for (int32 Index = 0; Index < NumCommandLists; Index++)
			{
				FRHICommandListBase* CmdList = RHICmdLists[Index];
				ALLOC_COMMAND_CL(*RHICmdList, FRHICommandWaitForAndSubmitSubList)(Nothing, CmdList);

#if WITH_MGPU
				// This will restore the context GPU masks to whatever they were set to
				// before the sub-list executed.
				ALLOC_COMMAND_CL(*RHICmdList, FRHICommandSetGPUMask)(RHICmdList->GetGPUMask());
#endif
			}
		}
		else
		{
			Start = 0;
			int32 ThreadIndex = 0;

			while (Start < NumCommandLists)
			{
				int32 Last = Start;
				int32 DrawCnt = Sizes[Start];

				while (Last < NumCommandLists - 1 && DrawCnt + Sizes[Last + 1] <= MinSize)
				{
					Last++;
					DrawCnt += Sizes[Last];
				}
				check(Last >= Start);

				IRHICommandContextContainer* ContextContainer =  RHIGetCommandContextContainer(ThreadIndex, EffectiveThreads, RHICmdList->GetGPUMask());
				check(ContextContainer);

				FGraphEventRef TranslateCompletionEvent = TGraphTask<FParallelTranslateCommandList>::CreateTask(nullptr, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(&RHICmdLists[Start], 1 + Last - Start, ContextContainer, bIsPrepass);
				MyCompletionGraphEvent->DontCompleteUntil(TranslateCompletionEvent);
				ALLOC_COMMAND_CL(*RHICmdList, FRHICommandWaitForAndSubmitSubListParallel)(TranslateCompletionEvent, ContextContainer, EffectiveThreads, ThreadIndex++);
				Start = Last + 1;
			}
			check(EffectiveThreads == ThreadIndex);
		}
	}
};

void FRHICommandListBase::QueueParallelAsyncCommandListSubmit(FGraphEventRef* AnyThreadCompletionEvents, bool bIsPrepass, FRHICommandList** CmdLists, int32* NumDrawsIfKnown, int32 Num, int32 MinDrawsPerTranslate, bool bSpewMerge)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FRHICommandListBase_QueueParallelAsyncCommandListSubmit);
	check(IsInRenderingThread() && IsImmediate() && Num);

	if (IsRunningRHIInSeparateThread())
	{
		// Execute everything that is queued up on the immediate command list before submitting sublists built in parallel.
		FRHICommandListImmediate& ImmediateCommandList = FRHICommandListExecutor::GetImmediateCommandList();
		ImmediateCommandList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread); // we should start on the stuff before this async list

		// as good a place as any to clear this
		if (RHIThreadBufferLockFence.GetReference() && RHIThreadBufferLockFence->IsComplete())
		{
			RHIThreadBufferLockFence = nullptr;
		}
	}
#if !UE_BUILD_SHIPPING
	// do a flush before hand so we can tell if it was this parallel set that broke something, or what came before.
	if (CVarRHICmdFlushOnQueueParallelSubmit.GetValueOnRenderThread())
	{
		CSV_SCOPED_TIMING_STAT(RHITFlushes, QueueParallelAsyncCommandListSubmit);
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}
#endif

	if (Num && IsRunningRHIInSeparateThread())
	{
		static const auto ICVarRHICmdBalanceParallelLists = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.RHICmdBalanceParallelLists"));

		if (ICVarRHICmdBalanceParallelLists->GetValueOnRenderThread() == 0 && CVarRHICmdBalanceTranslatesAfterTasks.GetValueOnRenderThread() > 0 && GRHISupportsParallelRHIExecute && CVarRHICmdUseDeferredContexts.GetValueOnAnyThread() > 0)
		{
			FGraphEventArray Prereq;
			FRHICommandListBase** RHICmdLists = (FRHICommandListBase**)Alloc(sizeof(FRHICommandListBase*) * Num, alignof(FRHICommandListBase*));
			for (int32 Index = 0; Index < Num; Index++)
			{
				FGraphEventRef& AnyThreadCompletionEvent = AnyThreadCompletionEvents[Index];
				FRHICommandList* CmdList = CmdLists[Index];
				RHICmdLists[Index] = CmdList;
				if (AnyThreadCompletionEvent.GetReference())
				{
					Prereq.Add(AnyThreadCompletionEvent);
					WaitOutstandingTasks.Add(AnyThreadCompletionEvent);
				}
			}
			// this is used to ensure that any old buffer locks are completed before we start any parallel translates
			if (RHIThreadBufferLockFence.GetReference())
			{
				Prereq.Add(RHIThreadBufferLockFence);
			}
			FRHICommandList* CmdList = new FRHICommandList(GetGPUMask());
			FGraphEventRef TranslateSetupCompletionEvent = TGraphTask<FParallelTranslateSetupCommandList>::CreateTask(&Prereq, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(CmdList, &RHICmdLists[0], Num, bIsPrepass);
			QueueCommandListSubmit(CmdList);
			AllOutstandingTasks.Add(TranslateSetupCompletionEvent);
			if (IsRunningRHIInSeparateThread())
			{
				FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread); // we don't want stuff after the async cmd list to be bundled with it
			}
#if !UE_BUILD_SHIPPING
			if (CVarRHICmdFlushOnQueueParallelSubmit.GetValueOnRenderThread())
			{
				CSV_SCOPED_TIMING_STAT(RHITFlushes, QueueParallelAsyncCommandListSubmit);
				FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			}
#endif
			return;
		}
		IRHICommandContextContainer* ContextContainer = nullptr;
		bool bMerge = !!CVarRHICmdMergeSmallDeferredContexts.GetValueOnRenderThread();
		int32 EffectiveThreads = 0;
		int32 Start = 0;
		int32 ThreadIndex = 0;
		if (GRHISupportsParallelRHIExecute && CVarRHICmdUseDeferredContexts.GetValueOnAnyThread() > 0)
		{
			// this is pretty silly but we need to know the number of jobs in advance, so we run the merge logic twice
			while (Start < Num)
			{
				int32 Last = Start;
				int32 DrawCnt = NumDrawsIfKnown[Start];

				if (bMerge && DrawCnt >= 0)
				{
					while (Last < Num - 1 && NumDrawsIfKnown[Last + 1] >= 0 && DrawCnt + NumDrawsIfKnown[Last + 1] <= MinDrawsPerTranslate)
					{
						Last++;
						DrawCnt += NumDrawsIfKnown[Last];
					}
				}
				check(Last >= Start);
				Start = Last + 1;
				EffectiveThreads++;
			}

			Start = 0;
			ContextContainer = RHIGetCommandContextContainer(ThreadIndex, EffectiveThreads, GetGPUMask());
		}
		if (ContextContainer)
		{
			while (Start < Num)
			{
				int32 Last = Start;
				int32 DrawCnt = NumDrawsIfKnown[Start];
				int32 TotalMem = bSpewMerge ? CmdLists[Start]->GetUsedMemory() : 0;   // the memory is only accurate if we are spewing because otherwise it isn't done yet!

				if (bMerge && DrawCnt >= 0)
				{
					while (Last < Num - 1 && NumDrawsIfKnown[Last + 1] >= 0 && DrawCnt + NumDrawsIfKnown[Last + 1] <= MinDrawsPerTranslate)
					{
						Last++;
						DrawCnt += NumDrawsIfKnown[Last];
						TotalMem += bSpewMerge ? CmdLists[Start]->GetUsedMemory() : 0;   // the memory is only accurate if we are spewing because otherwise it isn't done yet!
					}
				}

				check(Last >= Start);

				if (!ContextContainer)
				{
					ContextContainer = RHIGetCommandContextContainer(ThreadIndex, EffectiveThreads, GetGPUMask());
				}
				check(ContextContainer);

				FGraphEventArray Prereq;
				FRHICommandListBase** RHICmdLists = (FRHICommandListBase**)Alloc(sizeof(FRHICommandListBase*) * (1 + Last - Start), alignof(FRHICommandListBase*));
				for (int32 Index = Start; Index <= Last; Index++)
				{
					FGraphEventRef& AnyThreadCompletionEvent = AnyThreadCompletionEvents[Index];
					FRHICommandList* CmdList = CmdLists[Index];
					RHICmdLists[Index - Start] = CmdList;
					if (AnyThreadCompletionEvent.GetReference())
					{
						Prereq.Add(AnyThreadCompletionEvent);
						AllOutstandingTasks.Add(AnyThreadCompletionEvent);
						WaitOutstandingTasks.Add(AnyThreadCompletionEvent);
					}
				}
				UE_CLOG(bSpewMerge, LogTemp, Display, TEXT("Parallel translate %d->%d    %dKB mem   %d draws (-1 = unknown)"), Start, Last, FMath::DivideAndRoundUp(TotalMem, 1024), DrawCnt);

				// this is used to ensure that any old buffer locks are completed before we start any parallel translates
				if (RHIThreadBufferLockFence.GetReference())
				{
					Prereq.Add(RHIThreadBufferLockFence);
				}

				FGraphEventRef TranslateCompletionEvent = TGraphTask<FParallelTranslateCommandList>::CreateTask(&Prereq, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(&RHICmdLists[0], 1 + Last - Start, ContextContainer, bIsPrepass);

				AllOutstandingTasks.Add(TranslateCompletionEvent);
				ALLOC_COMMAND(FRHICommandWaitForAndSubmitSubListParallel)(TranslateCompletionEvent, ContextContainer, EffectiveThreads, ThreadIndex++);
				if (IsRunningRHIInSeparateThread())
				{
					FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread); // we don't want stuff after the async cmd list to be bundled with it
				}

				ContextContainer = nullptr;
				Start = Last + 1;
			}
			check(EffectiveThreads == ThreadIndex);
#if !UE_BUILD_SHIPPING
			if (CVarRHICmdFlushOnQueueParallelSubmit.GetValueOnRenderThread())
			{
				CSV_SCOPED_TIMING_STAT(RHITFlushes, QueueParallelAsyncCommandListSubmit);
				FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			}
#endif
			return;
		}
	}
	for (int32 Index = 0; Index < Num; Index++)
	{
		FGraphEventRef& AnyThreadCompletionEvent = AnyThreadCompletionEvents[Index];
		FRHICommandList* CmdList = CmdLists[Index];
		if (AnyThreadCompletionEvent.GetReference())
		{
			if (IsRunningRHIInSeparateThread())
			{
				AllOutstandingTasks.Add(AnyThreadCompletionEvent);
			}
			WaitOutstandingTasks.Add(AnyThreadCompletionEvent);
		}
		ALLOC_COMMAND(FRHICommandWaitForAndSubmitSubList)(AnyThreadCompletionEvent, CmdList);
	}
	if (IsRunningRHIInSeparateThread())
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread); // we don't want stuff after the async cmd list to be bundled with it
	}
}

void FRHICommandListBase::QueueAsyncCommandListSubmit(FGraphEventRef& AnyThreadCompletionEvent, FRHICommandList* CmdList)
{
	check(IsInRenderingThread() && IsImmediate());

	if (IsRunningRHIInSeparateThread())
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread); // we should start on the stuff before this async list
	}
	if (AnyThreadCompletionEvent.GetReference())
	{
		if (IsRunningRHIInSeparateThread())
		{
			AllOutstandingTasks.Add(AnyThreadCompletionEvent);
		}
		WaitOutstandingTasks.Add(AnyThreadCompletionEvent);
	}
	ALLOC_COMMAND(FRHICommandWaitForAndSubmitSubList)(AnyThreadCompletionEvent, CmdList);
	if (IsRunningRHIInSeparateThread())
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread); // we don't want stuff after the async cmd list to be bundled with it
	}
}

FRHICOMMAND_MACRO(FRHICommandWaitForAndSubmitRTSubList)
{
	FGraphEventRef EventToWaitFor;
	FRHICommandList* RHICmdList;

	FORCEINLINE_DEBUGGABLE FRHICommandWaitForAndSubmitRTSubList(FGraphEventRef& InEventToWaitFor, FRHICommandList* InRHICmdList)
		: EventToWaitFor(InEventToWaitFor)
		, RHICmdList(InRHICmdList)
	{}

	void Execute(FRHICommandListBase& CmdList)
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(EventToWaitFor);
		RHICmdList->CopyContext(CmdList);
		delete RHICmdList;
	}
};

void FRHICommandListBase::QueueRenderThreadCommandListSubmit(FGraphEventRef& RenderThreadCompletionEvent, FRHICommandList* CmdList)
{
	check(IsInRenderingThread() && IsImmediate());

	ALLOC_COMMAND(FRHICommandWaitForAndSubmitRTSubList)(RenderThreadCompletionEvent, CmdList);

#if WITH_MGPU
	// This will restore the context GPU masks to whatever they were set to
	// before the sub-list executed.
	ALLOC_COMMAND(FRHICommandSetGPUMask)(GPUMask);
#endif
}

void FRHICommandListBase::AddDispatchPrerequisite(const FGraphEventRef& Prereq)
{
	if (Prereq.GetReference())
	{
		RTTasks.AddUnique(Prereq);
	}
}

FRHICOMMAND_MACRO(FRHICommandSubmitSubList)
{
	FRHICommandList* RHICmdList;

	FORCEINLINE_DEBUGGABLE FRHICommandSubmitSubList(FRHICommandList* InRHICmdList)
		: RHICmdList(InRHICmdList)
	{
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		INC_DWORD_STAT_BY(STAT_ChainLinkCount, 1);
		SCOPE_CYCLE_COUNTER(STAT_ChainExecute);
		RHICmdList->CopyContext(CmdList);
		delete RHICmdList;
	}
};

void FRHICommandListBase::QueueCommandListSubmit(FRHICommandList* CmdList)
{
	ALLOC_COMMAND(FRHICommandSubmitSubList)(CmdList);

#if WITH_MGPU
	// This will restore the context GPU masks to whatever they were set to
	// before the sub-list executed.
	ALLOC_COMMAND(FRHICommandSetGPUMask)(GPUMask);
#endif
}


void FRHICommandList::BeginScene()
{
	check(IsImmediate() && IsInRenderingThread());
	if (Bypass())
	{
		GetContext().RHIBeginScene();
		return;
	}
	ALLOC_COMMAND(FRHICommandBeginScene)();
	if (!IsRunningRHIInSeparateThread())
	{
		// if we aren't running an RHIThread, there is no good reason to buffer this frame advance stuff and that complicates state management, so flush everything out now
		QUICK_SCOPE_CYCLE_COUNTER(BeginScene_Flush);
		CSV_SCOPED_TIMING_STAT(RHITFlushes, BeginScene);
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}
}
void FRHICommandList::EndScene()
{
	check(IsImmediate() && IsInRenderingThread());
	if (Bypass())
	{
		GetContext().RHIEndScene();
		return;
	}
	ALLOC_COMMAND(FRHICommandEndScene)();
	if (!IsRunningRHIInSeparateThread())
	{
		// if we aren't running an RHIThread, there is no good reason to buffer this frame advance stuff and that complicates state management, so flush everything out now
		QUICK_SCOPE_CYCLE_COUNTER(EndScene_Flush);
		CSV_SCOPED_TIMING_STAT(RHITFlushes, EndScene);
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}
}


void FRHICommandList::BeginDrawingViewport(FRHIViewport* Viewport, FRHITexture* RenderTargetRHI)
{
	check(IsImmediate() && IsInRenderingThread());
	if (Bypass())
	{
		GetContext().RHIBeginDrawingViewport(Viewport, RenderTargetRHI);
		return;
	}
	ALLOC_COMMAND(FRHICommandBeginDrawingViewport)(Viewport, RenderTargetRHI);
	if (!IsRunningRHIInSeparateThread())
	{
		// if we aren't running an RHIThread, there is no good reason to buffer this frame advance stuff and that complicates state management, so flush everything out now
		QUICK_SCOPE_CYCLE_COUNTER(BeginDrawingViewport_Flush);
		CSV_SCOPED_TIMING_STAT(RHITFlushes, BeginDrawingViewport);
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}
}

void FRHICommandList::EndDrawingViewport(FRHIViewport* Viewport, bool bPresent, bool bLockToVsync)
{
	check(IsImmediate() && IsInRenderingThread());
	if (Bypass())
	{
		GetContext().RHIEndDrawingViewport(Viewport, bPresent, bLockToVsync);
	}
	else
	{
		ALLOC_COMMAND(FRHICommandEndDrawingViewport)(Viewport, bPresent, bLockToVsync);

		if (IsRunningRHIInSeparateThread())
		{
			// Insert a fence to prevent the renderthread getting more than a frame ahead of the RHIThread
			GRHIThreadEndDrawingViewportFences[GRHIThreadEndDrawingViewportFenceIndex] = static_cast<FRHICommandListImmediate*>(this)->RHIThreadFence();
		}
		// if we aren't running an RHIThread, there is no good reason to buffer this frame advance stuff and that complicates state management, so flush everything out now
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_EndDrawingViewport_Dispatch);
			FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
		}
	}

	if (IsRunningRHIInSeparateThread())
	{
		// Wait on the previous frame's RHI thread fence (we never want the rendering thread to get more than a frame ahead)
		uint32 PreviousFrameFenceIndex = 1 - GRHIThreadEndDrawingViewportFenceIndex;
		FGraphEventRef& LastFrameFence = GRHIThreadEndDrawingViewportFences[PreviousFrameFenceIndex];
		FRHICommandListExecutor::WaitOnRHIThreadFence(LastFrameFence);
		GRHIThreadEndDrawingViewportFences[PreviousFrameFenceIndex] = nullptr;
		GRHIThreadEndDrawingViewportFenceIndex = PreviousFrameFenceIndex;
	}

	RHIAdvanceFrameForGetViewportBackBuffer(Viewport);
}

void FRHICommandList::BeginFrame()
{
	check(IsImmediate() && IsInRenderingThread());
	if (Bypass())
	{
		GetContext().RHIBeginFrame();
		return;
	}
	ALLOC_COMMAND(FRHICommandBeginFrame)();
	if (!IsRunningRHIInSeparateThread())
	{
		// if we aren't running an RHIThread, there is no good reason to buffer this frame advance stuff and that complicates state management, so flush everything out now
		QUICK_SCOPE_CYCLE_COUNTER(BeginFrame_Flush);
		CSV_SCOPED_TIMING_STAT(RHITFlushes, BeginFrame);
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}

}

void FRHICommandList::EndFrame()
{
	check(IsImmediate() && IsInRenderingThread());
	if (Bypass())
	{
		GetContext().RHIEndFrame();
		GDynamicRHI->RHIAdvanceFrameFence();
		return;
	}

	ALLOC_COMMAND(FRHICommandEndFrame)();
	GDynamicRHI->RHIAdvanceFrameFence();

	if (!IsRunningRHIInSeparateThread())
	{
		// if we aren't running an RHIThread, there is no good reason to buffer this frame advance stuff and that complicates state management, so flush everything out now
		QUICK_SCOPE_CYCLE_COUNTER(EndFrame_Flush);
		CSV_SCOPED_TIMING_STAT(RHITFlushes, EndFrame);
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}
	else
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
	}
}

DECLARE_CYCLE_STAT(TEXT("Explicit wait for tasks"), STAT_ExplicitWait, STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("Prewait dispatch"), STAT_PrewaitDispatch, STATGROUP_RHICMDLIST);
void FRHICommandListBase::WaitForTasks(bool bKnownToBeComplete)
{
	check(IsImmediate() && IsInRenderingThread());
	if (WaitOutstandingTasks.Num())
	{
		bool bAny = false;
		for (int32 Index = 0; Index < WaitOutstandingTasks.Num(); Index++)
		{
			if (!WaitOutstandingTasks[Index]->IsComplete())
			{
				ensure(!bKnownToBeComplete);
				bAny = true;
				break;
			}
		}
		if (bAny)
		{
			SCOPE_CYCLE_COUNTER(STAT_ExplicitWait);
			ENamedThreads::Type RenderThread_Local = ENamedThreads::GetRenderThread_Local();
			check(!FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local));
			FTaskGraphInterface::Get().WaitUntilTasksComplete(WaitOutstandingTasks, RenderThread_Local);
		}
		WaitOutstandingTasks.Reset();
	}
}

FScopedCommandListWaitForTasks::~FScopedCommandListWaitForTasks()
{
	check(IsInRenderingThread());
	if (bWaitForTasks)
	{
		if (IsRunningRHIInSeparateThread())
		{
#if 0
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_FScopedCommandListWaitForTasks_Dispatch);
				RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
			}
#endif
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_FScopedCommandListWaitForTasks_WaitAsync);
				RHICmdList.ImmediateFlush(EImmediateFlushType::WaitForOutstandingTasksOnly);
			}
		}
		else
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FScopedCommandListWaitForTasks_Flush);
			CSV_SCOPED_TIMING_STAT(RHITFlushes, FScopedCommandListWaitForTasksDtor);
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("Explicit wait for dispatch"), STAT_ExplicitWaitDispatch, STATGROUP_RHICMDLIST);
void FRHICommandListBase::WaitForDispatch()
{
	check(IsImmediate() && IsInRenderingThread());
	check(!AllOutstandingTasks.Num()); // dispatch before you get here
	if (RenderThreadSublistDispatchTask.GetReference() && RenderThreadSublistDispatchTask->IsComplete())
	{
#if NEEDS_DEBUG_INFO_ON_PRESENT_HANG
		bRenderThreadSublistDispatchTaskClearedOnRT = IsInActualRenderingThread();
		bRenderThreadSublistDispatchTaskClearedOnGT = IsInGameThread();
#endif
		RenderThreadSublistDispatchTask = nullptr;
	}
	while (RenderThreadSublistDispatchTask.GetReference())
	{
		SCOPE_CYCLE_COUNTER(STAT_ExplicitWaitDispatch);
		ENamedThreads::Type RenderThread_Local = ENamedThreads::GetRenderThread_Local();
		if (FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local))
		{
			// this is a deadlock. RT tasks must be done by now or they won't be done. We could add a third queue...
			UE_LOG(LogRHI, Fatal, TEXT("Deadlock in FRHICommandListBase::WaitForDispatch."));
		}
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(RenderThreadSublistDispatchTask, RenderThread_Local);
		if (RenderThreadSublistDispatchTask.GetReference() && RenderThreadSublistDispatchTask->IsComplete())
		{
#if NEEDS_DEBUG_INFO_ON_PRESENT_HANG
			bRenderThreadSublistDispatchTaskClearedOnRT = IsInActualRenderingThread();
			bRenderThreadSublistDispatchTaskClearedOnGT = IsInGameThread();
#endif
			RenderThreadSublistDispatchTask = nullptr;
		}
	}
}

void FDynamicRHI::VirtualTextureSetFirstMipInMemory_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 FirstMip)
{
	CSV_SCOPED_TIMING_STAT(RHITFlushes, VirtualTextureSetFirstMipInMemory_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	GDynamicRHI->RHIVirtualTextureSetFirstMipInMemory(Texture, FirstMip);
}

void FDynamicRHI::VirtualTextureSetFirstMipVisible_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 FirstMip)
{
	CSV_SCOPED_TIMING_STAT(RHITFlushes, VirtualTextureSetFirstMipVisible_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	GDynamicRHI->RHIVirtualTextureSetFirstMipVisible(Texture, FirstMip);
}

DECLARE_CYCLE_STAT(TEXT("Explicit wait for RHI thread"), STAT_ExplicitWaitRHIThread, STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("Explicit wait for RHI thread async dispatch"), STAT_ExplicitWaitRHIThread_Dispatch, STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("Deep spin for stray resource init"), STAT_SpinWaitRHIThread, STATGROUP_RHICMDLIST);
DECLARE_CYCLE_STAT(TEXT("Spin RHIThread wait for stall"), STAT_SpinWaitRHIThreadStall, STATGROUP_RHICMDLIST);

#define TIME_RHIT_STALLS (0)

#if TIME_RHIT_STALLS
uint32 TestLastFrame = 0; 
double TotalTime = 0.0;
int32 TotalStalls = 0;
#endif

bool FRHICommandListImmediate::IsStalled()
{
	return GRHIThreadStallRequestCount.load() > 0;
}

bool FRHICommandListImmediate::StallRHIThread()
{
	if (GRHIThreadStallRequestCount.load() > 0)
	{
		return false;
	}
	CSV_SCOPED_TIMING_STAT(RHITStalls, Total);

	check(IsInRenderingThread() && IsRunningRHIInSeparateThread());
	bool bAsyncSubmit = CVarRHICmdAsyncRHIThreadDispatch.GetValueOnRenderThread() > 0;
	if (bAsyncSubmit)
	{
		SCOPED_NAMED_EVENT(StallRHIThread, FColor::Red);

		if (RenderThreadSublistDispatchTask.GetReference() && RenderThreadSublistDispatchTask->IsComplete())
		{
#if NEEDS_DEBUG_INFO_ON_PRESENT_HANG
			bRenderThreadSublistDispatchTaskClearedOnRT = IsInActualRenderingThread();
			bRenderThreadSublistDispatchTaskClearedOnGT = IsInGameThread();
#endif
			RenderThreadSublistDispatchTask = nullptr;
		}
		if (!RenderThreadSublistDispatchTask.GetReference())
		{
			if (RHIThreadTask.GetReference() && RHIThreadTask->IsComplete())
			{
				RHIThreadTask = nullptr;
				PrevRHIThreadTask = nullptr;
			}
			if (!RHIThreadTask.GetReference())
			{
				return false;
			}
		}
		const int32 OldStallCount = GRHIThreadStallRequestCount.fetch_add(1);
		if (OldStallCount > 0)
		{
			return true;
		}
		{
			SCOPE_CYCLE_COUNTER(STAT_SpinWaitRHIThreadStall);
#if TIME_RHIT_STALLS
			double StartTime = FPlatformTime::Seconds();
#endif

			{
				SCOPED_NAMED_EVENT(RHIThreadLock_Wait, FColor::Red);
#if PLATFORM_USES_UNFAIR_LOCKS
				// When we have unfair locks, we're not guaranteed to get the lock between the RHI tasks if our thread goes to sleep,
				// so we need to be more aggressive here as this is time critical.
				while (!GRHIThreadOnTasksCritical.TryLock())
				{
					FPlatformProcess::YieldThread();
				}
#else
				GRHIThreadOnTasksCritical.Lock();
#endif
			}

#if TIME_RHIT_STALLS
			TotalTime += FPlatformTime::Seconds() - StartTime;
			TotalStalls++;
			if (TestLastFrame != GFrameNumberRenderThread)
			{
				if (TestLastFrame)
				{
					int32 Frames = (int32)(GFrameNumberRenderThread - TestLastFrame);
					UE_LOG(LogRHI, Error, TEXT("%d frames %d stalls     %6.2fms / frame"), Frames, TotalStalls, float(1000.0 * TotalTime) / float(Frames) );
				}
				TestLastFrame = GFrameNumberRenderThread;
				TotalStalls = 0;
				TotalTime = 0.0;
			}
#endif
		}
		return true;
	}
	else
	{
		WaitForRHIThreadTasks();
		return false;
	}
}

void FRHICommandListImmediate::UnStallRHIThread()
{
	check(IsInRenderingThread() && IsRunningRHIInSeparateThread());
	const int32 NewStallCount = GRHIThreadStallRequestCount.fetch_sub(1) - 1;
	check(NewStallCount >= 0);
	if (NewStallCount == 0)
	{
		GRHIThreadOnTasksCritical.Unlock();
	}
}

void FRHICommandListBase::WaitForRHIThreadTasks()
{
	check(IsImmediate() && IsInRenderingThread());
	bool bAsyncSubmit = CVarRHICmdAsyncRHIThreadDispatch.GetValueOnRenderThread() > 0;
	ENamedThreads::Type RenderThread_Local = ENamedThreads::GetRenderThread_Local();
	if (bAsyncSubmit)
	{
		if (RenderThreadSublistDispatchTask.GetReference() && RenderThreadSublistDispatchTask->IsComplete())
		{
#if NEEDS_DEBUG_INFO_ON_PRESENT_HANG
			bRenderThreadSublistDispatchTaskClearedOnRT = IsInActualRenderingThread();
			bRenderThreadSublistDispatchTaskClearedOnGT = IsInGameThread();
#endif
			RenderThreadSublistDispatchTask = nullptr;
		}
		while (RenderThreadSublistDispatchTask.GetReference())
		{
			SCOPE_CYCLE_COUNTER(STAT_ExplicitWaitRHIThread_Dispatch);
			if (FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local))
			{
				// we have to spin here because all task threads might be stalled, meaning the fire event anythread task might not be hit.
				// todo, add a third queue
				SCOPE_CYCLE_COUNTER(STAT_SpinWaitRHIThread);
				while (!RenderThreadSublistDispatchTask->IsComplete())
				{
					FPlatformProcess::SleepNoStats(0);
				}
			}
			else
			{
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(RenderThreadSublistDispatchTask, RenderThread_Local);
			}
			if (RenderThreadSublistDispatchTask.GetReference() && RenderThreadSublistDispatchTask->IsComplete())
			{
#if NEEDS_DEBUG_INFO_ON_PRESENT_HANG
				bRenderThreadSublistDispatchTaskClearedOnRT = IsInActualRenderingThread();
				bRenderThreadSublistDispatchTaskClearedOnGT = IsInGameThread();
#endif
				RenderThreadSublistDispatchTask = nullptr;
			}
		}
		// now we can safely look at RHIThreadTask
	}
	if (RHIThreadTask.GetReference() && RHIThreadTask->IsComplete())
	{
		RHIThreadTask = nullptr;
		PrevRHIThreadTask = nullptr;
	}
	while (RHIThreadTask.GetReference())
	{
		SCOPE_CYCLE_COUNTER(STAT_ExplicitWaitRHIThread);
		if (FTaskGraphInterface::Get().IsThreadProcessingTasks(RenderThread_Local))
		{
			// we have to spin here because all task threads might be stalled, meaning the fire event anythread task might not be hit.
			// todo, add a third queue
			SCOPE_CYCLE_COUNTER(STAT_SpinWaitRHIThread);
			while (!RHIThreadTask->IsComplete())
			{
				FPlatformProcess::SleepNoStats(0);
			}
		}
		else
		{
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(RHIThreadTask, RenderThread_Local);
		}
		if (RHIThreadTask.GetReference() && RHIThreadTask->IsComplete())
		{
			RHIThreadTask = nullptr;
			PrevRHIThreadTask = nullptr;
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("RTTask completion join"), STAT_HandleRTThreadTaskCompletion_Join, STATGROUP_RHICMDLIST);
void FRHICommandListBase::HandleRTThreadTaskCompletion(const FGraphEventRef& MyCompletionGraphEvent)
{
	check(!IsImmediate() && !IsInRHIThread());
	for (int32 Index = 0; Index < RTTasks.Num(); Index++)
	{
		if (RTTasks[Index].IsValid() && !RTTasks[Index]->IsComplete())
		{
			MyCompletionGraphEvent->DontCompleteUntil(RTTasks[Index]);
		}
	}
	RTTasks.Empty();
}

void* FRHICommandList::operator new(size_t Size)
{
	return FMemory::Malloc(Size);
}

void FRHICommandList::operator delete(void *RawMemory)
{
	FMemory::Free(RawMemory);
}

void* FRHIComputeCommandList::operator new(size_t Size)
{
	return FMemory::Malloc(Size);
}

void FRHIComputeCommandList::operator delete(void *RawMemory)
{
	FMemory::Free(RawMemory);
}

void FRHIComputeCommandList::Transition(TArrayView<const FRHITransitionInfo> Infos)
{
	const ERHIPipeline Pipeline = GetPipeline();

	if (Bypass())
	{
		// Stack allocate the transition
		FMemStack& MemStack = FMemStack::Get();
		FMemMark Mark(MemStack);
		FRHITransition* Transition = new (MemStack.Alloc(FRHITransition::GetTotalAllocationSize(), FRHITransition::GetAlignment())) FRHITransition(Pipeline, Pipeline);
		GDynamicRHI->RHICreateTransition(Transition, FRHITransitionCreateInfo(Pipeline, Pipeline, ERHITransitionCreateFlags::NoSplit, Infos));

		GetComputeContext().RHIBeginTransitions(MakeArrayView((const FRHITransition**)&Transition, 1));
		GetComputeContext().RHIEndTransitions(MakeArrayView((const FRHITransition**)&Transition, 1));

		// Manual release
		GDynamicRHI->RHIReleaseTransition(Transition);
		Transition->~FRHITransition();
	}
	else
	{
		// Allocate the transition in the command list
		FRHITransition* Transition = new (Alloc(FRHITransition::GetTotalAllocationSize(), FRHITransition::GetAlignment())) FRHITransition(Pipeline, Pipeline);
		GDynamicRHI->RHICreateTransition(Transition, FRHITransitionCreateInfo(Pipeline, Pipeline, ERHITransitionCreateFlags::NoSplit, Infos));

		ALLOC_COMMAND(FRHICommandResourceTransition)(Transition);
	}

	for (const FRHITransitionInfo& Info : Infos)
	{
		ensureMsgf(Info.IsWholeResource(), TEXT("The Transition method only supports whole resource transitions."));

		if (FRHIViewableResource* Resource = GetViewableResource(Info))
		{
			SetTrackedAccess({ FRHITrackedAccessInfo(Resource, Info.AccessAfter) });
		}
	}
}

#if RHI_RAYTRACING
void FRHIComputeCommandList::BuildAccelerationStructure(FRHIRayTracingGeometry* Geometry)
{
	FRayTracingGeometryBuildParams Params;
	Params.Geometry = Geometry;
	Params.BuildMode = EAccelerationStructureBuildMode::Build;

	FRHIBufferRange ScratchBufferRange{};
	
	FRHIResourceCreateInfo ScratchBufferCreateInfo(TEXT("RHIScratchBuffer"));
	ScratchBufferRange.Buffer = RHICreateBuffer(Geometry->GetSizeInfo().BuildScratchSize, BUF_StructuredBuffer | BUF_RayTracingScratch, 0, ERHIAccess::UAVCompute, ScratchBufferCreateInfo);

	BuildAccelerationStructures(MakeArrayView(&Params, 1), ScratchBufferRange);
}

void FRHIComputeCommandList::BuildAccelerationStructures(const TArrayView<const FRayTracingGeometryBuildParams> Params)
{
	uint64 TotalRequiredScratchMemorySize = 0;
	for (const FRayTracingGeometryBuildParams& P : Params)
	{
		uint64 ScratchBufferRequiredSize = P.BuildMode == EAccelerationStructureBuildMode::Update ? P.Geometry->GetSizeInfo().UpdateScratchSize : P.Geometry->GetSizeInfo().BuildScratchSize;
		TotalRequiredScratchMemorySize += ScratchBufferRequiredSize;
	}

	FRHIResourceCreateInfo ScratchBufferCreateInfo(TEXT("RHIScratchBuffer"));
	FRHIBufferRange ScratchBufferRange{};	
	ScratchBufferRange.Buffer = RHICreateBuffer(TotalRequiredScratchMemorySize, BUF_StructuredBuffer | BUF_RayTracingScratch, 0, ERHIAccess::UAVCompute, ScratchBufferCreateInfo);

	BuildAccelerationStructures(Params, ScratchBufferRange);
}
#endif

void* FRHICommandListBase::operator new(size_t Size)
{
	check(0); // you shouldn't be creating these
	return FMemory::Malloc(Size);
}

void FRHICommandListBase::operator delete(void *RawMemory)
{
	FMemory::Free(RawMemory);
}


FBufferRHIRef FDynamicRHI::CreateBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, EBufferUsageFlags Usage, uint32 Stride, ERHIAccess ResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, CreateBuffer_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	FBufferRHIRef Buffer = GDynamicRHI->RHICreateBuffer(Size, Usage, Stride, ResourceState, CreateInfo);
	return Buffer;
}

FShaderResourceViewRHIRef FDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint32 Stride, uint8 Format)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, CreateShaderResourceView_RenderThread_VB);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateShaderResourceView(Buffer, Stride, Format);
}

FShaderResourceViewRHIRef FDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, const FShaderResourceViewInitializer& Initializer)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, CreateShaderResourceView_RenderThread_VB);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateShaderResourceView(Initializer);
}

FShaderResourceViewRHIRef FDynamicRHI::CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, CreateShaderResourceView_RenderThread_IB);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateShaderResourceView(Buffer);
}

static FLockTracker GLockTracker;

void* FDynamicRHI::RHILockBuffer(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_LockBuffer);

	void* Result;
	if (RHICmdList.IsTopOfPipe())
	{
		bool bBuffer = CVarRHICmdBufferWriteLocks.GetValueOnRenderThread() > 0;
		if (!bBuffer || LockMode != RLM_WriteOnly)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockBuffer_FlushAndLock);
			CSV_SCOPED_TIMING_STAT(RHITFlushes, LockBuffer_BottomOfPipe);

			FRHICommandListScopedFlushAndExecute Flush(RHICmdList);
			Result = GDynamicRHI->LockBuffer_BottomOfPipe(RHICmdList, Buffer, Offset, SizeRHI, LockMode);
		}
		else
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockBuffer_Malloc);
			Result = FMemory::Malloc(SizeRHI, 16);
		}

		// Only use the lock tracker at the top of the pipe. There's no need to track locks
		// at the bottom of the pipe, and doing so would require a critical section.
		GLockTracker.Lock(Buffer, Result, Offset, SizeRHI, LockMode);
	}
	else
	{
		Result = GDynamicRHI->LockBuffer_BottomOfPipe(RHICmdList, Buffer, Offset, SizeRHI, LockMode);
	}

	check(Result);
	return Result;
}

void FDynamicRHI::RHIUnlockBuffer(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_UnlockBuffer_RenderThread);

	if (RHICmdList.IsTopOfPipe())
	{
		FLockTracker::FLockParams Params = GLockTracker.Unlock(Buffer);

		bool bBuffer = CVarRHICmdBufferWriteLocks.GetValueOnRenderThread() > 0;
		if (!bBuffer || Params.LockMode != RLM_WriteOnly)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_UnlockBuffer_FlushAndUnlock);
			CSV_SCOPED_TIMING_STAT(RHITFlushes, UnlockBuffer_BottomOfPipe);

			FRHICommandListScopedFlushAndExecute Flush(RHICmdList);
			GDynamicRHI->UnlockBuffer_BottomOfPipe(RHICmdList, Buffer);
			GLockTracker.TotalMemoryOutstanding = 0;
		}
		else
		{
			RHICmdList.EnqueueLambda([Buffer, Params](FRHICommandListImmediate& InRHICmdList)
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_FRHICommandUpdateBuffer_Execute);
				void* Data = GDynamicRHI->LockBuffer_BottomOfPipe(InRHICmdList, Buffer, Params.Offset, Params.BufferSize, RLM_WriteOnly);
				{
					// If we spend a long time doing this memcpy, it means we got freshly allocated memory from the OS that has never been
					// initialized and is causing pagefault to bring zeroed pages into our process.
					TRACE_CPUPROFILER_EVENT_SCOPE(RHIUnlockBuffer_Memcpy);
					FMemory::Memcpy(Data, Params.Buffer, Params.BufferSize);
				}
				FMemory::Free(Params.Buffer);
				GDynamicRHI->UnlockBuffer_BottomOfPipe(InRHICmdList, Buffer);
			});
			RHICmdList.RHIThreadFence(true);
		}

		if (GLockTracker.TotalMemoryOutstanding > uint32(CVarRHICmdMaxOutstandingMemoryBeforeFlush.GetValueOnRenderThread()) * 1024u)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_UnlockBuffer_FlushForMem);
			// we could be loading a level or something, lets get this stuff going
			RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread); 
			GLockTracker.TotalMemoryOutstanding = 0;
		}
	}
	else
	{
		GDynamicRHI->UnlockBuffer_BottomOfPipe(RHICmdList, Buffer);
	}
}

// @todo-mattc-staging Default implementation
void* FDynamicRHI::RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
{
	check(false);
	return nullptr;
	//return GDynamicRHI->RHILockVertexBuffer(StagingBuffer->GetSourceBuffer(), Offset, SizeRHI, RLM_ReadOnly);
}
void FDynamicRHI::RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer)
{
	check(false);
	//GDynamicRHI->RHIUnlockVertexBuffer(StagingBuffer->GetSourceBuffer());
}

void* FDynamicRHI::LockStagingBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
{
	check(IsInRenderingThread());
	if (!Fence || !Fence->Poll() || Fence->NumPendingWriteCommands.GetValue() != 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_LockStagingBuffer_Flush);
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_LockStagingBuffer_RenderThread);
		if (GRHISupportsMultithreading)
		{
			return GDynamicRHI->RHILockStagingBuffer(StagingBuffer, Fence, Offset, SizeRHI);
		}
		else
		{
			FScopedRHIThreadStaller StallRHIThread(RHICmdList);
			return GDynamicRHI->RHILockStagingBuffer(StagingBuffer, Fence, Offset, SizeRHI);
		}
	}
}

void FDynamicRHI::UnlockStagingBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStagingBuffer* StagingBuffer)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_UnlockStagingBuffer_RenderThread);
	check(IsInRenderingThread());
	if (GRHISupportsMultithreading)
	{
		GDynamicRHI->RHIUnlockStagingBuffer(StagingBuffer);
	}
	else
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		GDynamicRHI->RHIUnlockStagingBuffer(StagingBuffer);
	}
}

FTexture2DRHIRef FDynamicRHI::AsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_AsyncReallocateTexture2D_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, AsyncReallocateTexture2D_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);  
	return GDynamicRHI->RHIAsyncReallocateTexture2D(Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
}

ETextureReallocationStatus FDynamicRHI::FinalizeAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, FinalizeAsyncReallocateTexture2D_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHIFinalizeAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
}

ETextureReallocationStatus FDynamicRHI::CancelAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, CancelAsyncReallocateTexture2D_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICancelAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
}

FVertexShaderRHIRef FDynamicRHI::CreateVertexShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, CreateVertexShader_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateVertexShader(Code, Hash);
}

FMeshShaderRHIRef FDynamicRHI::CreateMeshShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, CreateMeshShader_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateMeshShader(Code, Hash);
}

FAmplificationShaderRHIRef FDynamicRHI::CreateAmplificationShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, CreateAmplificationShader_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateAmplificationShader(Code, Hash);
}

FPixelShaderRHIRef FDynamicRHI::CreatePixelShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, CreatePixelShader_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreatePixelShader(Code, Hash);
}

FGeometryShaderRHIRef FDynamicRHI::CreateGeometryShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, CreateGeometryShader_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateGeometryShader(Code, Hash);
}

FComputeShaderRHIRef FDynamicRHI::CreateComputeShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, CreateGeometryShader_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateComputeShader(Code, Hash);
}

void FDynamicRHI::UpdateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, UpdateTexture2D_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHIUpdateTexture2D(Texture, MipIndex, UpdateRegion, SourcePitch, SourceData);
}

void FDynamicRHI::UpdateFromBufferTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, FRHIBuffer* Buffer, uint32 BufferOffset)
{
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHIUpdateFromBufferTexture2D(Texture, MipIndex, UpdateRegion, SourcePitch, Buffer, BufferOffset);
}

FUpdateTexture3DData FDynamicRHI::BeginUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion)
{
	check(IsInRenderingThread());

	const int32 FormatSize = PixelFormatBlockBytes[Texture->GetFormat()];
	const int32 RowPitch = UpdateRegion.Width * FormatSize;
	const int32 DepthPitch = UpdateRegion.Width * UpdateRegion.Height * FormatSize;

	SIZE_T MemorySize = static_cast<SIZE_T>(DepthPitch) * UpdateRegion.Depth;
	uint8* Data = (uint8*)FMemory::Malloc(MemorySize);	

	return FUpdateTexture3DData(Texture, MipIndex, UpdateRegion, RowPitch, DepthPitch, Data, MemorySize, GFrameNumberRenderThread);
}

void FDynamicRHI::EndUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FUpdateTexture3DData& UpdateData)
{
	check(IsInRenderingThread());
	check(GFrameNumberRenderThread == UpdateData.FrameNumber); 
	CSV_SCOPED_TIMING_STAT(RHITStalls, EndUpdateTexture3D_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);	
	GDynamicRHI->RHIUpdateTexture3D(UpdateData.Texture, UpdateData.MipIndex, UpdateData.UpdateRegion, UpdateData.RowPitch, UpdateData.DepthPitch, UpdateData.Data);
	FMemory::Free(UpdateData.Data);
	UpdateData.Data = nullptr;
}

void FDynamicRHI::EndMultiUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, TArray<FUpdateTexture3DData>& UpdateDataArray)
{
	for (int32 Idx = 0; Idx < UpdateDataArray.Num(); ++Idx)
	{
		GDynamicRHI->EndUpdateTexture3D_RenderThread(RHICmdList, UpdateDataArray[Idx]);
	}
}

void FDynamicRHI::UpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, UpdateTexture3D_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	GDynamicRHI->RHIUpdateTexture3D(Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
}

void* FDynamicRHI::LockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush)
{
	if (bNeedsDefaultRHIFlush) 
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockTexture2D_Flush);
		CSV_SCOPED_TIMING_STAT(RHITFlushes, LockTexture2D_RenderThread);
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		return GDynamicRHI->RHILockTexture2D(Texture, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}
	CSV_SCOPED_TIMING_STAT(RHITStalls, LockTexture2D_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHILockTexture2D(Texture, MipIndex, LockMode, DestStride, bLockWithinMiptail);
}

void FDynamicRHI::UnlockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush)
{
	if (bNeedsDefaultRHIFlush)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_UnlockTexture2D_Flush);
		CSV_SCOPED_TIMING_STAT(RHITFlushes, UnlockTexture2D_RenderThread);
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		GDynamicRHI->RHIUnlockTexture2D(Texture, MipIndex, bLockWithinMiptail);
		return;
	}
	CSV_SCOPED_TIMING_STAT(RHITStalls, UnlockTexture2D_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	GDynamicRHI->RHIUnlockTexture2D(Texture, MipIndex, bLockWithinMiptail);
}

void* FDynamicRHI::LockTexture2DArray_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2DArray* Texture, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockTexture2DArray_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, LockTexture2DArray_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	return GDynamicRHI->RHILockTexture2DArray(Texture, ArrayIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
}

void FDynamicRHI::UnlockTexture2DArray_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2DArray* Texture, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_UnlockTexture2DArray_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, UnlockTexture2DArray_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	GDynamicRHI->RHIUnlockTexture2DArray(Texture, ArrayIndex, MipIndex, bLockWithinMiptail);
}

FRHIShaderLibraryRef FDynamicRHI::RHICreateShaderLibrary_RenderThread(class FRHICommandListImmediate& RHICmdList, EShaderPlatform Platform, FString FilePath, FString Name)
{
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateShaderLibrary(Platform, FilePath, Name);
}

FTextureRHIRef FDynamicRHI::RHICreateTexture_RenderThread(class FRHICommandListImmediate& RHICmdList, const FRHITextureCreateDesc& CreateDesc)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, RHICreateTexture_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateTexture(CreateDesc);
}

FUnorderedAccessViewRHIRef FDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, bool bUseUAVCounter, bool bAppendBuffer)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, RHICreateUnorderedAccessView_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateUnorderedAccessView(Buffer, bUseUAVCounter, bAppendBuffer);
}

FUnorderedAccessViewRHIRef FDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipLevel, uint16 FirstArraySlice, uint16 NumArraySlices)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, RHICreateUnorderedAccessView_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateUnorderedAccessView(Texture, MipLevel, FirstArraySlice, NumArraySlices);
}

FUnorderedAccessViewRHIRef FDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipLevel, uint8 Format, uint16 FirstArraySlice, uint16 NumArraySlices)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, RHICreateUnorderedAccessView_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateUnorderedAccessView(Texture, MipLevel, Format, FirstArraySlice, NumArraySlices);
}

FUnorderedAccessViewRHIRef FDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint8 Format)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, RHICreateUnorderedAccessView_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateUnorderedAccessView(Buffer, Format);
}

FShaderResourceViewRHIRef FDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, RHICreateShaderResourceView_RenderThread_Tex2D); // TODO - clean this up
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateShaderResourceView(Texture, CreateInfo);
}

FShaderResourceViewRHIRef FDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer, uint32 Stride, uint8 Format)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, RHICreateShaderResourceView_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateShaderResourceView(Buffer, Stride, Format);
}

FShaderResourceViewRHIRef FDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, const FShaderResourceViewInitializer& Initializer)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, RHICreateShaderResourceView_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateShaderResourceView(Initializer);
}

FShaderResourceViewRHIRef FDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIBuffer* Buffer)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, RHICreateShaderResourceView_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateShaderResourceView(Buffer);
}

FShaderResourceViewRHIRef FDynamicRHI::RHICreateShaderResourceViewWriteMask_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2DRHI)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, RHICreateShaderResourceView_RenderThread_Tex2DWriteMask);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateShaderResourceViewWriteMask(Texture2DRHI);
}

FShaderResourceViewRHIRef FDynamicRHI::RHICreateShaderResourceViewFMask_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2DRHI)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, RHICreateShaderResourceView_RenderThread_Tex2DFMask);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateShaderResourceViewFMask(Texture2DRHI);
}

FRenderQueryRHIRef FDynamicRHI::RHICreateRenderQuery_RenderThread(class FRHICommandListImmediate& RHICmdList, ERenderQueryType QueryType)
{
	CSV_SCOPED_TIMING_STAT(RHITStalls, RHICreateRenderQuery_RenderThread);
	FScopedRHIThreadStaller StallRHIThread(RHICmdList);
	return GDynamicRHI->RHICreateRenderQuery(QueryType);
}

void* FDynamicRHI::RHILockTextureCubeFace_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_LockTextureCubeFace_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, RHILockTextureCubeFace_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	return GDynamicRHI->RHILockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
}

void FDynamicRHI::RHIUnlockTextureCubeFace_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_UnlockTextureCubeFace_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, RHIUnlockTextureCubeFace_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	GDynamicRHI->RHIUnlockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, bLockWithinMiptail);
}


void FDynamicRHI::RHIMapStagingSurface_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 GPUIndex, FRHIGPUFence* Fence, void*& OutData, int32& OutWidth, int32& OutHeight)
{
	if (Fence == nullptr || !Fence->Poll() || Fence->NumPendingWriteCommands.GetValue() != 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_MapStagingSurface_Flush);
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_MapStagingSurface_RenderThread);
		if (GRHISupportsMultithreading)
		{
			GDynamicRHI->RHIMapStagingSurface(Texture, Fence, OutData, OutWidth, OutHeight, GPUIndex != INDEX_NONE ? GPUIndex : RHICmdList.GetGPUMask().ToIndex());
		}
		else
		{
			FScopedRHIThreadStaller StallRHIThread(RHICmdList);
			GDynamicRHI->RHIMapStagingSurface(Texture, Fence, OutData, OutWidth, OutHeight, GPUIndex != INDEX_NONE ? GPUIndex : RHICmdList.GetGPUMask().ToIndex());
		}
	}
}

void FDynamicRHI::RHIUnmapStagingSurface_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 GPUIndex)
{
	if (GRHISupportsMultithreading)
	{
		GDynamicRHI->RHIUnmapStagingSurface(Texture, GPUIndex != INDEX_NONE ? GPUIndex : RHICmdList.GetGPUMask().ToIndex());
	}
	else
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		GDynamicRHI->RHIUnmapStagingSurface(Texture, GPUIndex != INDEX_NONE ? GPUIndex : RHICmdList.GetGPUMask().ToIndex());
	}
}

void FDynamicRHI::RHIReadSurfaceFloatData_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace, int32 ArrayIndex, int32 MipIndex)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_ReadSurfaceFloatData_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, RHIReadSurfaceFloatData_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	GDynamicRHI->RHIReadSurfaceFloatData(Texture, Rect, OutData, CubeFace, ArrayIndex, MipIndex);
}

void FDynamicRHI::RHIReadSurfaceFloatData_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, FIntRect Rect, TArray<FFloat16Color>& OutData, FReadSurfaceDataFlags Flags)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RHIMETHOD_ReadSurfaceFloatData_Flush);
	CSV_SCOPED_TIMING_STAT(RHITFlushes, RHIReadSurfaceFloatData_RenderThread);
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	GDynamicRHI->RHIReadSurfaceFloatData(Texture, Rect, OutData, Flags);
}

void FRHICommandListImmediate::UpdateTextureReference(FRHITextureReference* TextureRef, FRHITexture* NewTexture)
{
	if (TextureRef == nullptr)
	{
		return;
	}

	EnqueueLambda([TextureRef, NewTexture](auto&)
	{
		TextureRef->SetReferencedTexture(NewTexture);
	});
	RHIThreadFence(true);
	if (GetUsedMemory() > 256 * 1024)
	{
		// we could be loading a level or something, lets get this stuff going
		ImmediateFlush(EImmediateFlushType::DispatchToRHIThread); 
	}
}

void FRHICommandListImmediate::UpdateRHIResources(FRHIResourceUpdateInfo* UpdateInfos, int32 Num, bool bNeedReleaseRefs)
{
	if (this->Bypass())
	{
		FRHICommandUpdateRHIResources Cmd(UpdateInfos, Num, bNeedReleaseRefs);
		Cmd.Execute(*this);
	}
	else
	{
		const SIZE_T NumBytes = sizeof(FRHIResourceUpdateInfo) * Num;
		FRHIResourceUpdateInfo* LocalUpdateInfos = reinterpret_cast<FRHIResourceUpdateInfo*>(this->Alloc(NumBytes, alignof(FRHIResourceUpdateInfo)));
		FMemory::Memcpy(LocalUpdateInfos, UpdateInfos, NumBytes);
		new (AllocCommand<FRHICommandUpdateRHIResources>()) FRHICommandUpdateRHIResources(LocalUpdateInfos, Num, bNeedReleaseRefs);
		RHIThreadFence(true);
		if (GetUsedMemory() > 256 * 1024)
		{
			// we could be loading a level or something, lets get this stuff going
			ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
		}
	}
}

RHI_API void RHISetComputeShaderBackwardsCompatible(IRHIComputeContext* InContext, FRHIComputeShader* InShader)
{
	TRefCountPtr<FRHIComputePipelineState> ComputePipelineState = RHICreateComputePipelineState(InShader);
	InContext->RHISetComputePipelineState(ComputePipelineState);
}
