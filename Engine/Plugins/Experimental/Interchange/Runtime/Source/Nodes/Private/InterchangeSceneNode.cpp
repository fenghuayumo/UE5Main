// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeSceneNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		const FString& FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey()
		{
			static FString SceneNodeSpecializeType_BaseKey = TEXT("SceneNodeSpecializeType");
			return SceneNodeSpecializeType_BaseKey;
		}

		const FString& FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey()
		{
			static FString MaterialDependencyUids_BaseKey = TEXT("__MaterialDependencyUidsBaseKey__");
			return MaterialDependencyUids_BaseKey;
		}
		
		const FString& FSceneNodeStaticData::GetTransformSpecializeTypeString()
		{
			static FString TransformSpecializeTypeString = TEXT("Transform");
			return TransformSpecializeTypeString;
		}

		const FString& FSceneNodeStaticData::GetJointSpecializeTypeString()
		{
			static FString JointSpecializeTypeString = TEXT("Joint");
			return JointSpecializeTypeString;
		}

		const FString& FSceneNodeStaticData::GetLodGroupSpecializeTypeString()
		{
			static FString JointSpecializeTypeString = TEXT("LodGroup");
			return JointSpecializeTypeString;
		}
	}//ns Interchange
}//ns UE

UInterchangeSceneNode::UInterchangeSceneNode()
{
	NodeSpecializeTypes.Initialize(Attributes, UE::Interchange::FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey());
	MaterialDependencyUids.Initialize(Attributes, UE::Interchange::FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey());
}

/**
	* Return the node type name of the class, we use this when reporting error
	*/
FString UInterchangeSceneNode::GetTypeName() const
{
	const FString TypeName = TEXT("SceneNode");
	return TypeName;
}

FString UInterchangeSceneNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	FString KeyDisplayName = NodeAttributeKey.Key;
	if (NodeAttributeKey.Key.Equals(UE::Interchange::FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey()))
	{
		KeyDisplayName = TEXT("Specialized type count");
		return KeyDisplayName;
	}
	else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey()))
	{
		KeyDisplayName = TEXT("Specialized type index ");
		const FString IndexKey = UE::Interchange::TArrayAttributeHelper<FString>::IndexKey();
		int32 IndexPosition = NodeAttributeKey.Key.Find(IndexKey) + IndexKey.Len();
		if (IndexPosition < NodeAttributeKey.Key.Len())
		{
			KeyDisplayName += NodeAttributeKey.Key.RightChop(IndexPosition);
		}
		return KeyDisplayName;
	}
	else if (NodeAttributeKey.Key.Equals(UE::Interchange::FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey()))
	{
		KeyDisplayName = TEXT("Material dependencies count");
		return KeyDisplayName;
	}
	else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey()))
	{
		KeyDisplayName = TEXT("Material dependency index ");
		const FString IndexKey = UE::Interchange::TArrayAttributeHelper<FString>::IndexKey();
		int32 IndexPosition = NodeAttributeKey.Key.Find(IndexKey) + IndexKey.Len();
		if (IndexPosition < NodeAttributeKey.Key.Len())
		{
			KeyDisplayName += NodeAttributeKey.Key.RightChop(IndexPosition);
		}
		return KeyDisplayName;
	}
	else if (NodeAttributeKey.Key.Equals(Macro_CustomTransformCurvePayloadKeyKey.Key))
	{
		return FString(TEXT("Transform Curve Payload Key"));
	}
	return Super::GetKeyDisplayName(NodeAttributeKey);
}

FString UInterchangeSceneNode::GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey()))
	{
		return FString(TEXT("SpecializeType"));
	}
	else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey()))
	{
		return FString(TEXT("MaterialDependencies"));
	}
	else if (NodeAttributeKey == Macro_CustomLocalTransformKey
		|| NodeAttributeKey == Macro_CustomAssetInstanceUidKey)
	{
		return FString(TEXT("Scene"));
	}
	else if (NodeAttributeKey == Macro_CustomBindPoseLocalTransformKey
		|| NodeAttributeKey == Macro_CustomTimeZeroLocalTransformKey
		|| NodeAttributeKey == Macro_CustomTransformCurvePayloadKeyKey)
	{
		return FString(TEXT("Joint"));
	}
	return Super::GetAttributeCategory(NodeAttributeKey);
}

FName UInterchangeSceneNode::GetIconName() const
{
	FString SpecializedType;
	GetSpecializedType(0, SpecializedType);
	if (SpecializedType.IsEmpty())
	{
		return NAME_None;
	}
	SpecializedType = TEXT("SceneGraphIcon.") + SpecializedType;
	return FName(*SpecializedType);
}

bool UInterchangeSceneNode::IsSpecializedTypeContains(const FString& SpecializedType) const
{
	TArray<FString> SpecializedTypes;
	GetSpecializedTypes(SpecializedTypes);
	for (const FString& SpecializedTypeRef : SpecializedTypes)
	{
		if (SpecializedTypeRef.Equals(SpecializedType))
		{
			return true;
		}
	}
	return false;
}

int32 UInterchangeSceneNode::GetSpecializedTypeCount() const
{
	return NodeSpecializeTypes.GetCount();
}

void UInterchangeSceneNode::GetSpecializedType(const int32 Index, FString& OutSpecializedType) const
{
	NodeSpecializeTypes.GetItem(Index, OutSpecializedType);
}

void UInterchangeSceneNode::GetSpecializedTypes(TArray<FString>& OutSpecializedTypes) const
{
	NodeSpecializeTypes.GetItems(OutSpecializedTypes);
}

bool UInterchangeSceneNode::AddSpecializedType(const FString& SpecializedType)
{
	return NodeSpecializeTypes.AddItem(SpecializedType);
}

bool UInterchangeSceneNode::RemoveSpecializedType(const FString& SpecializedType)
{
	return NodeSpecializeTypes.RemoveItem(SpecializedType);
}

int32 UInterchangeSceneNode::GetMaterialDependencyUidsCount() const
{
	return MaterialDependencyUids.GetCount();
}

void UInterchangeSceneNode::GetMaterialDependencyUid(const int32 Index, FString& OutMaterialDependencyUid) const
{
	MaterialDependencyUids.GetItem(Index, OutMaterialDependencyUid);
}

void UInterchangeSceneNode::GetMaterialDependencyUids(TArray<FString>& OutMaterialDependencyUids) const
{
	MaterialDependencyUids.GetItems(OutMaterialDependencyUids);
}

bool UInterchangeSceneNode::AddMaterialDependencyUid(const FString& MaterialDependencyUid)
{
	return MaterialDependencyUids.AddItem(MaterialDependencyUid);
}

bool UInterchangeSceneNode::RemoveMaterialDependencyUid(const FString& MaterialDependencyUid)
{
	return MaterialDependencyUids.RemoveItem(MaterialDependencyUid);
}

bool UInterchangeSceneNode::GetCustomLocalTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(LocalTransform, FTransform);
}

bool UInterchangeSceneNode::SetCustomLocalTransform(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FTransform& AttributeValue)
{
	ResetGlobalTransformCachesOfNodeAndAllChildren(BaseNodeContainer, this);
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(LocalTransform, FTransform);
}

bool UInterchangeSceneNode::GetCustomGlobalTransform(const UInterchangeBaseNodeContainer* BaseNodeContainer, FTransform& AttributeValue, bool bForceRecache /*= false*/) const
{
	return GetGlobalTransformInternal(Macro_CustomLocalTransformKey, CacheGlobalTransform, BaseNodeContainer, AttributeValue, bForceRecache);
}

bool UInterchangeSceneNode::GetCustomBindPoseLocalTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(BindPoseLocalTransform, FTransform);
}

bool UInterchangeSceneNode::SetCustomBindPoseLocalTransform(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FTransform& AttributeValue)
{
	ResetGlobalTransformCachesOfNodeAndAllChildren(BaseNodeContainer, this);
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(BindPoseLocalTransform, FTransform);
}

bool UInterchangeSceneNode::GetCustomBindPoseGlobalTransform(const UInterchangeBaseNodeContainer* BaseNodeContainer, FTransform& AttributeValue, bool bForceRecache /*= false*/) const
{
	return GetGlobalTransformInternal(Macro_CustomBindPoseLocalTransformKey, CacheBindPoseGlobalTransform, BaseNodeContainer, AttributeValue, bForceRecache);
}

bool UInterchangeSceneNode::GetCustomTimeZeroLocalTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(TimeZeroLocalTransform, FTransform);
}

bool UInterchangeSceneNode::SetCustomTimeZeroLocalTransform(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FTransform& AttributeValue)
{
	ResetGlobalTransformCachesOfNodeAndAllChildren(BaseNodeContainer, this);
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(TimeZeroLocalTransform, FTransform);
}

bool UInterchangeSceneNode::GetCustomTimeZeroGlobalTransform(const UInterchangeBaseNodeContainer* BaseNodeContainer, FTransform& AttributeValue, bool bForceRecache /*= false*/) const
{
	return GetGlobalTransformInternal(Macro_CustomTimeZeroLocalTransformKey, CacheTimeZeroGlobalTransform, BaseNodeContainer, AttributeValue, bForceRecache);
}

bool UInterchangeSceneNode::GetCustomGeometricTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(GeometricTransform, FTransform);
}

bool UInterchangeSceneNode::SetCustomGeometricTransform(const FTransform& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(GeometricTransform, FTransform);
}

bool UInterchangeSceneNode::GetCustomAssetInstanceUid(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(AssetInstanceUid, FString);
}

bool UInterchangeSceneNode::SetCustomAssetInstanceUid(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AssetInstanceUid, FString);
}

bool UInterchangeSceneNode::GetCustomIsNodeTransformAnimated(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(IsNodeTransformAnimated, bool);
}

bool UInterchangeSceneNode::SetCustomIsNodeTransformAnimated(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(IsNodeTransformAnimated, bool);
}

bool UInterchangeSceneNode::GetCustomNodeTransformAnimationKeyCount(int32& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(NodeTransformAnimationKeyCount, int32);
}

bool UInterchangeSceneNode::SetCustomNodeTransformAnimationKeyCount(const int32& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(NodeTransformAnimationKeyCount, int32);
}

bool UInterchangeSceneNode::GetCustomNodeTransformAnimationStartTime(double& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(NodeTransformAnimationStartTime, double);
}

bool UInterchangeSceneNode::SetCustomNodeTransformAnimationStartTime(const double& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(NodeTransformAnimationStartTime, double);
}

bool UInterchangeSceneNode::GetCustomNodeTransformAnimationEndTime(double& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(NodeTransformAnimationEndTime, double);
}

bool UInterchangeSceneNode::SetCustomNodeTransformAnimationEndTime(const double& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(NodeTransformAnimationEndTime, double);
}

bool UInterchangeSceneNode::GetCustomTransformCurvePayloadKey(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(TransformCurvePayloadKey, FString);
}

bool UInterchangeSceneNode::SetCustomTransformCurvePayloadKey(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(TransformCurvePayloadKey, FString);
}

void UInterchangeSceneNode::ResetAllGlobalTransformCaches(const UInterchangeBaseNodeContainer* BaseNodeContainer)
{
	BaseNodeContainer->IterateNodes([](const FString& NodeUid, UInterchangeBaseNode* Node)
		{
			if (UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(Node))
			{
				SceneNode->CacheGlobalTransform.Reset();
				SceneNode->CacheBindPoseGlobalTransform.Reset();
				SceneNode->CacheTimeZeroGlobalTransform.Reset();
			}
		});
}

void UInterchangeSceneNode::ResetGlobalTransformCachesOfNodeAndAllChildren(const UInterchangeBaseNodeContainer* BaseNodeContainer, const UInterchangeBaseNode* ParentNode)
{
	check(ParentNode);
	if (const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(ParentNode))
	{
		SceneNode->CacheGlobalTransform.Reset();
		SceneNode->CacheBindPoseGlobalTransform.Reset();
		SceneNode->CacheTimeZeroGlobalTransform.Reset();
	}
	TArray<FString> ChildrenUids = BaseNodeContainer->GetNodeChildrenUids(ParentNode->GetUniqueID());
	for (const FString& ChildUid : ChildrenUids)
	{
		if (const UInterchangeBaseNode* ChildNode = BaseNodeContainer->GetNode(ChildUid))
		{
			ResetGlobalTransformCachesOfNodeAndAllChildren(BaseNodeContainer, ChildNode);
		}
	}
}

bool UInterchangeSceneNode::GetGlobalTransformInternal(const UE::Interchange::FAttributeKey LocalTransformKey, TOptional<FTransform>& CacheTransform, const UInterchangeBaseNodeContainer* BaseNodeContainer, FTransform& AttributeValue, bool bForceRecache) const
{
	if (!Attributes->ContainAttribute(LocalTransformKey))
	{
		return false;
	}
	if (bForceRecache)
	{
		CacheTransform.Reset();
	}
	if (!CacheTransform.IsSet())
	{
		FTransform LocalTransform;
		UE::Interchange::FAttributeStorage::TAttributeHandle<FTransform> AttributeHandle = GetAttributeHandle<FTransform>(LocalTransformKey);
		if (AttributeHandle.IsValid() && AttributeHandle.Get(LocalTransform) == UE::Interchange::EAttributeStorageResult::Operation_Success)
		{
			//Compute the Global
			if (Attributes->ContainAttribute(UE::Interchange::FBaseNodeStaticData::ParentIDKey()))
			{
				FTransform GlobalParent;
				if (const UInterchangeSceneNode* ParentSceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(GetParentUid())))
				{
					if (LocalTransformKey == Macro_CustomLocalTransformKey)
					{
						ParentSceneNode->GetCustomGlobalTransform(BaseNodeContainer, GlobalParent, bForceRecache);
					}
					else if (LocalTransformKey == Macro_CustomBindPoseLocalTransformKey)
					{
						ParentSceneNode->GetCustomBindPoseGlobalTransform(BaseNodeContainer, GlobalParent, bForceRecache);
					}
					else if (LocalTransformKey == Macro_CustomTimeZeroLocalTransformKey)
					{
						ParentSceneNode->GetCustomTimeZeroGlobalTransform(BaseNodeContainer, GlobalParent, bForceRecache);
					}
				}
				CacheTransform = LocalTransform * GlobalParent;
			}
			else
			{
				CacheTransform = LocalTransform;
			}
		}
		else
		{
			CacheTransform = FTransform::Identity;
		}
	}
	//The cache is always valid here
	check(CacheTransform.IsSet());
	AttributeValue = CacheTransform.GetValue();
	return true;
}
