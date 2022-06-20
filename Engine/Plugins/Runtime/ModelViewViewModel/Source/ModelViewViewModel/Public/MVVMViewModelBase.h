// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "FieldNotification/FieldId.h"
#include "FieldNotification/FieldNotificationDeclaration.h"
#include "FieldNotification/FieldMulticastDelegate.h"
#include "FieldNotification/IFieldValueChanged.h"
#include "Types/MVVMAvailableBinding.h"
#include "Types/MVVMBindingName.h"

#include "MVVMViewModelBase.generated.h"

//UENUM()
//enum class EMVVMNGetterType : uint8
//{
//	/** The Source Property has a Delegate associated with it that will notify us when the value change. */
//	Delegate,
//	/** The value is fetch when requested. */
//	FetchOnRequest,
//	/** The value is fetch every frame and compared. */ 
//	FetchOnTickAndCompared,
//	/** The value is fetch every frame and is considered changed. */
//	FetchOnTick,
//};

///** The user will notify the ViewModel when the value change via the PropertyValueChanged event. */
//Event,
//Interface,		// The Source Context implement the MVVMModelInterface.
					//Use the interface to register to the SourceContext's OnPropertyValueChanged.
//PushModel,		// The property has a NetId generated by UBT. Used to notify us when the value changed.

#define UE_MVVM_NOTIFY_FIELD_VALUE_CHANGED(MemberName) \
	BindingFieldValueChanged(ThisClass::FFieldNotificationClassDescriptor::MemberName)

#define UE_MVVM_SET_PROPERTY_VALUE(MemberName, NewValue) \
	SetPropertyValue(MemberName, NewValue, ThisClass::FFieldNotificationClassDescriptor::MemberName)


/** Sub class of this will be generated from the BP to cached the source data. */
UCLASS(Blueprintable, Abstract, DisplayName="MVVM ViewModel")
class MODELVIEWVIEWMODEL_API UMVVMViewModelBase : public UObject, public INotifyFieldValueChanged
{
	GENERATED_BODY()

public:
	struct MODELVIEWVIEWMODEL_API FFieldNotificationClassDescriptor : public ::UE::FieldNotification::IClassDescriptor
	{
		virtual void ForEachField(const UClass* Class, TFunctionRef<bool(UE::FieldNotification::FFieldId FielId)> Callback) const override;
	};

public:
	//~ Begin INotifyFieldValueChanged Interface
	virtual FDelegateHandle AddFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FFieldValueChangedDelegate InNewDelegate) override final;
	virtual bool RemoveFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FDelegateHandle InHandle) override final;
	virtual int32 RemoveAllFieldValueChangedDelegates(const void* InUserObject) override final;
	virtual int32 RemoveAllFieldValueChangedDelegates(UE::FieldNotification::FFieldId InFieldId, const void* InUserObject) override final;
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged Interface

protected:
	UFUNCTION(BlueprintCallable, Category="FieldNotify", meta=(DisplayName="Broadcast Field Value Changed", ScriptName="BroadcastFieldValueChanged"))
	void K2_BroadcastFieldValueChanged(FFieldNotificationId FieldId);
	void BindingFieldValueChanged(UE::FieldNotification::FFieldId InFieldId);

protected:
	template<typename T>
	bool SetPropertyValue(T& Value, const T& NewValue, UE::FieldNotification::FFieldId FieldId)
	{
		if (Value == NewValue)
		{
			return false;
		}

		Value = NewValue;
		BindingFieldValueChanged(FieldId);
		return true;
	}

private:
	UE::FieldNotification::FFieldMulticastDelegate Delegates;
	TBitArray<> EnabledFieldNotifications;
};