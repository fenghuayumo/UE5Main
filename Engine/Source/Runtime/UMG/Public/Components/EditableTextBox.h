// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SWidget.h"
#include "Components/Widget.h"
#include "Components/TextWidgetTypes.h"
#include "Widgets/Text/ISlateEditableTextWidget.h"
#include "EditableTextBox.generated.h"

class SEditableTextBox;
class USlateWidgetStyleAsset;

/**
 * Allows the user to type in custom text.  Only permits a single line of text to be entered.
 * 
 * * No Children
 * * Text Entry
 */
UCLASS(meta=(DisplayName="Text Box"))
class UMG_API UEditableTextBox : public UWidget
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEditableTextBoxChangedEvent, const FText&, Text);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEditableTextBoxCommittedEvent, const FText&, Text, ETextCommit::Type, CommitMethod);

public:
	/** The text content for this editable text box widget */
	UPROPERTY(EditAnywhere, Category=Content)
	FText Text;

	/** A bindable delegate to allow logic to drive the text of the widget */
	UPROPERTY()
	FGetText TextDelegate;

public:

	/** The style */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Appearance, meta=(DisplayName="Style", ShowOnlyInnerProperties))
	FEditableTextBoxStyle WidgetStyle;

	/** Hint text that appears when there is no text in the text box */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Content)
	FText HintText;

	/** A bindable delegate to allow logic to drive the hint text of the widget */
	UPROPERTY()
	FGetText HintTextDelegate;

	/** Sets whether this text box can actually be modified interactively by the user */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Appearance)
	bool IsReadOnly;

	/** Sets whether this text box is for storing a password */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Appearance)
	bool IsPassword;

	/** Minimum width that a text block should be */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Appearance)
	float MinimumDesiredWidth;

	/** Workaround as we lose focus when the auto completion closes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Behavior, AdvancedDisplay)
	bool IsCaretMovedWhenGainFocus;

	/** Whether to select all text when the user clicks to give focus on the widget */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Behavior, AdvancedDisplay)
	bool SelectAllTextWhenFocused;

	/** Whether to allow the user to back out of changes when they press the escape key */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Behavior, AdvancedDisplay)
	bool RevertTextOnEscape;

	/** Whether to clear keyboard focus when pressing enter to commit changes */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Behavior, AdvancedDisplay)
	bool ClearKeyboardFocusOnCommit;

	/** Whether to select all text when pressing enter to commit changes */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Behavior, AdvancedDisplay)
	bool SelectAllTextOnCommit;

	/** Whether the context menu can be opened */
	UPROPERTY(EditAnywhere, Category=Behavior, AdvancedDisplay)
	bool AllowContextMenu;

	/** If we're on a platform that requires a virtual keyboard, what kind of keyboard should this widget use? */
	UPROPERTY(EditAnywhere, Category=Behavior, AdvancedDisplay)
	TEnumAsByte<EVirtualKeyboardType::Type> KeyboardType;

	/** Additional options to use for the virtual keyboard summoned by this widget */
	UPROPERTY(EditAnywhere, Category=Behavior, AdvancedDisplay)
	FVirtualKeyboardOptions VirtualKeyboardOptions;

	/** The type of event that will trigger the display of the virtual keyboard */
	UPROPERTY(EditAnywhere, Category = Behavior, AdvancedDisplay)
	EVirtualKeyboardTrigger VirtualKeyboardTrigger;

	/** What action should be taken when the virtual keyboard is dismissed? */
	UPROPERTY(EditAnywhere, Category=Behavior, AdvancedDisplay)
	EVirtualKeyboardDismissAction VirtualKeyboardDismissAction;
	
	/** How the text should be aligned with the margin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetJustification, Category=Appearance)
	TEnumAsByte<ETextJustify::Type> Justification;

	/** Sets what should happen when text is clipped because the block does not have enough space */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetTextOverflowPolicy, Category = Appearance)
	ETextOverflowPolicy OverflowPolicy;


	/** Controls how the text within this widget should be shaped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Localization, AdvancedDisplay, meta=(ShowOnlyInnerProperties))
	FShapedTextOptions ShapedTextOptions;

public:

	/** Called whenever the text is changed programmatically or interactively by the user */
	UPROPERTY(BlueprintAssignable, Category="TextBox|Event")
	FOnEditableTextBoxChangedEvent OnTextChanged;

	/** Called whenever the text is committed.  This happens when the user presses enter or the text box loses focus. */
	UPROPERTY(BlueprintAssignable, Category="TextBox|Event")
	FOnEditableTextBoxCommittedEvent OnTextCommitted;

	/** Provide a alternative mechanism for error reporting. */
	//SLATE_ARGUMENT(TSharedPtr<class IErrorReportingWidget>, ErrorReporting)

public:

	/**  */
	UFUNCTION(BlueprintCallable, Category="Widget", meta=(DisplayName="GetText (Text Box)"))
	FText GetText() const;

	/**  */
	UFUNCTION(BlueprintCallable, Category="Widget", meta=(DisplayName="SetText (Text Box)"))
	void SetText(FText InText);

	UFUNCTION(BlueprintCallable, Category = "Widget", meta = (DisplayName = "Set Hint Text (Text Box)"))
	void SetHintText(FText InText);

	UFUNCTION(BlueprintCallable, Category="Widget",  meta=(DisplayName="SetError (Text Box)"))
	void SetError(FText InError);

	UFUNCTION(BlueprintCallable, Category="Widget", meta=(DisplayName="SetIsReadOnly (Text Box)"))
	void SetIsReadOnly(bool bReadOnly);

	UFUNCTION(BlueprintCallable, Category = "Widget")
	void SetIsPassword(bool bIsPassword);

	UFUNCTION(BlueprintCallable, Category="Widget")
	void ClearError();

	UFUNCTION(BlueprintCallable, Category = "Widget")
	bool HasError() const;

	UFUNCTION(BlueprintSetter)
	void SetJustification(ETextJustify::Type InJustification);

	UFUNCTION(BlueprintSetter)
	void SetTextOverflowPolicy(ETextOverflowPolicy InOverflowPolicy);

	UFUNCTION(BlueprintCallable, Category="Widget", meta=(DisplayName="SetForegroundColor (Text Box)"))
	void SetForegroundColor(FLinearColor color);

	//~ Begin UWidget Interface
	virtual void SynchronizeProperties() override;
	//~ End UWidget Interface

	//~ Begin UVisual Interface
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	//~ End UVisual Interface

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	//~ Begin UWidget Interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// End of UWidget

	virtual void HandleOnTextChanged(const FText& Text);
	virtual void HandleOnTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

#if WITH_ACCESSIBILITY
	virtual TSharedPtr<SWidget> GetAccessibleWidget() const override;
#endif

protected:
	TSharedPtr<SEditableTextBox> MyEditableTextBlock;

	PROPERTY_BINDING_IMPLEMENTATION(FText, Text);
	PROPERTY_BINDING_IMPLEMENTATION(FText, HintText);
};
