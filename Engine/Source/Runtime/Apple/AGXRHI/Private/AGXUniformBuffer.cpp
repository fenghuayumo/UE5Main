// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXConstantBuffer.cpp: AGX RHI constant buffer implementation.
=============================================================================*/

#include "AGXRHIPrivate.h"
#include "AGXFrameAllocator.h"
#include "AGXUniformBuffer.h"
#include "ShaderParameterStruct.h"

#pragma mark Suballocated Uniform Buffer Implementation

FAGXSuballocatedUniformBuffer::FAGXSuballocatedUniformBuffer(const FRHIUniformBufferLayout* Layout, EUniformBufferUsage Usage, EUniformBufferValidation InValidation)
    : FRHIUniformBuffer(Layout)
    , LastFrameUpdated(0)
    , Offset(0)
    , Backing(nil)
    , Shadow(nullptr)
    , ResourceTable()
#if METAL_UNIFORM_BUFFER_VALIDATION
    , Validation(InValidation)
#endif // METAL_UNIFORM_BUFFER_VALIDATION
{
    // Slate can create SingleDraw uniform buffers and use them several frames later. So it must be included.
    if (Usage == UniformBuffer_SingleDraw || Usage == UniformBuffer_MultiFrame)
    {
        Shadow = FMemory::Malloc(GetSize());
    }
}

FAGXSuballocatedUniformBuffer::~FAGXSuballocatedUniformBuffer()
{
    if (HasShadow())
    {
        FMemory::Free(Shadow);
    }

    // Note: this object does NOT own a reference
    // to the uniform buffer backing store
}

bool FAGXSuballocatedUniformBuffer::HasShadow()
{
    return Shadow != nullptr;
}

void FAGXSuballocatedUniformBuffer::Update(const void* Contents, TArray<TRefCountPtr<FRHIResource> > const& InResourceTable)
{
    if (HasShadow())
    {
        FMemory::Memcpy(Shadow, Contents, GetSize());
    }

	ResourceTable = InResourceTable;

	PushToGPUBacking(Contents);
}

// Acquires a region in the current frame's uniform buffer and
// pushes the data in Contents into that GPU backing store
// The amount of data read from Contents is given by the Layout
void FAGXSuballocatedUniformBuffer::PushToGPUBacking(const void* Contents)
{
    check(IsInRenderingThread() ^ IsRunningRHIInSeparateThread());
    
    FAGXDeviceContext& DeviceContext = GetAGXDeviceContext();
    
    FAGXFrameAllocator* Allocator = DeviceContext.GetUniformAllocator();
    FAGXFrameAllocator::AllocationEntry Entry = Allocator->AcquireSpace(GetSize());
    // copy contents into backing
    Backing = Entry.Backing;
    Offset = Entry.Offset;
    uint8* ConstantSpace = reinterpret_cast<uint8*>([Backing contents]) + Entry.Offset;
    FMemory::Memcpy(ConstantSpace, Contents, GetSize());
    LastFrameUpdated = DeviceContext.GetFrameNumberRHIThread();
}

// Because we can create a uniform buffer on frame N and may not bind it until frame N+10
// we need to keep a copy of the most recent data. Then when it's time to bind this
// uniform buffer we can push the data into the GPU backing.
void FAGXSuballocatedUniformBuffer::PrepareToBind()
{
    FAGXDeviceContext& DeviceContext = GetAGXDeviceContext();
    if(Shadow && LastFrameUpdated < DeviceContext.GetFrameNumberRHIThread())
    {
        PushToGPUBacking(Shadow);
    }
}

void FAGXSuballocatedUniformBuffer::CopyResourceTable_RenderThread(const void* Contents, TArray<TRefCountPtr<FRHIResource> >& OutResourceTable)
{
#if METAL_UNIFORM_BUFFER_VALIDATION
	if (Validation == EUniformBufferValidation::ValidateResources)
	{
		ValidateShaderParameterResourcesRHI(Contents, GetLayout());
	}
#endif // METAL_UNIFORM_BUFFER_VALIDATION

	const FRHIUniformBufferLayout& Layout = GetLayout();
    const uint32 NumResources = Layout.Resources.Num();
    if (NumResources > 0)
    {
		OutResourceTable.Empty(NumResources);
		OutResourceTable.AddZeroed(NumResources);
        
        for (uint32 Index = 0; Index < NumResources; ++Index)
		{
			OutResourceTable[Index] = GetShaderParameterResourceRHI(Contents, Layout.Resources[Index].MemberOffset, Layout.Resources[Index].MemberType);
		}
    }
}
