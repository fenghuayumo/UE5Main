// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Object/ObjectElementAssetDataInterface.h"

#include "Elements/Object/ObjectElementData.h"
#include "UObject/Object.h"

FAssetData UObjectElementAssetDataInterface::GetAssetData(const FTypedElementHandle& InElementHandle)
{
	UObject* RawObjectPtr = ObjectElementDataUtil::GetObjectFromHandle(InElementHandle);
	return FAssetData(RawObjectPtr);
}
