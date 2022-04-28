// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Framework/TypedElementListFwd.h"

class UTypedElementRegistry;

namespace TypedElementUtil
{

/**
 * Batch the given elements by their type.
 */
TYPEDELEMENTFRAMEWORK_API void BatchElementsByType(FTypedElementListConstRef InElementsToBatch, TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>>& OutElementsByType);
TYPEDELEMENTFRAMEWORK_API void BatchElementsByType(TArrayView<const FTypedElementHandle> InElementsToBatch, TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>>& OutElementsByType);

TYPEDELEMENTFRAMEWORK_API TArray<FScriptTypedElementHandle> ConvertToScriptElementArray(const TArray<FTypedElementHandle>& InNativeHandles, UTypedElementRegistry* Registry);
TYPEDELEMENTFRAMEWORK_API TArray<FTypedElementHandle> ConvertToNativeElementArray(const TArray<FScriptTypedElementHandle>& InScriptHandles);

} // namespace TypedElementUtil
