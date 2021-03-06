// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "Layout/Margin.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/PanelSlot.h"
#include "Widgets/SBoxPanel.h"

#include "VerticalBoxSlot.generated.h"

/** The Slot for the UVerticalBox, contains the widget that is flowed vertically */
UCLASS()
class UMG_API UVerticalBoxSlot : public UPanelSlot
{
	GENERATED_UCLASS_BODY()

public:
	
	/** How much space this slot should occupy in the direction of the panel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layout|Vertical Box Slot", meta = (DisplayAfter = "Padding"))
	FSlateChildSize Size;

	/** The padding area between the slot and the content it contains. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layout|Vertical Box Slot")
	FMargin Padding;

private:
	//TODO UMG Slots should hold weak or shared refs to slots.

	/** A raw pointer to the slot to allow us to adjust the size, padding...etc at runtime. */
	SVerticalBox::FSlot* Slot;

public:

	/** The alignment of the object horizontally. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layout|Vertical Box Slot")
	TEnumAsByte<EHorizontalAlignment> HorizontalAlignment;

	/** The alignment of the object vertically. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layout|Vertical Box Slot")
	TEnumAsByte<EVerticalAlignment> VerticalAlignment;

public:

	UFUNCTION(BlueprintCallable, Category="Layout|Vertical Box Slot")
	void SetPadding(FMargin InPadding);

	UFUNCTION(BlueprintCallable, Category="Layout|Vertical Box Slot")
	void SetSize(FSlateChildSize InSize);

	UFUNCTION(BlueprintCallable, Category="Layout|Vertical Box Slot")
	void SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment);

	UFUNCTION(BlueprintCallable, Category="Layout|Vertical Box Slot")
	void SetVerticalAlignment(EVerticalAlignment InVerticalAlignment);

public:

	// UPanelSlot interface
	virtual void SynchronizeProperties() override;
	// End of UPanelSlot interface

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	/** Builds the underlying FSlot for the Slate layout panel. */
	void BuildSlot(TSharedRef<SVerticalBox> InVerticalBox);

#if WITH_EDITOR
	virtual bool NudgeByDesigner(const FVector2D& NudgeDirection, const TOptional<int32>& GridSnapSize) override;
	virtual void SynchronizeFromTemplate(const UPanelSlot* const TemplateSlot) override;
#endif //WITH_EDITOR
};
