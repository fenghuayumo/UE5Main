// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintHeaderViewSettings.h"
#include "BlueprintHeaderView.h"
#include "Styling/StyleColors.h"

FHeaderViewSyntaxColors::FHeaderViewSyntaxColors()
	: Comment(FStyleColors::AccentGreen.GetSpecifiedColor())
	, Error(FStyleColors::Error.GetSpecifiedColor())
	, Macro(FStyleColors::AccentPurple.GetSpecifiedColor())
	, Typename(0.0f, 0.3f, 0.3f)
	, Identifier(FStyleColors::White.GetSpecifiedColor())
	, Keyword(FStyleColors::AccentBlue.GetSpecifiedColor())
{

}

UBlueprintHeaderViewSettings::UBlueprintHeaderViewSettings()
{
	
}

FName UBlueprintHeaderViewSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FText UBlueprintHeaderViewSettings::GetSectionText() const
{
	return NSLOCTEXT("BlueprintHeaderViewSettings", "HeaderViewSectionText", "Blueprint Header View");
}

FName UBlueprintHeaderViewSettings::GetSectionName() const
{
	return TEXT("Blueprint Header View");
}

void UBlueprintHeaderViewSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyChangedName = PropertyChangedEvent.GetPropertyName();

	SyntaxColors = FHeaderViewSyntaxColors();

	if (PropertyChangedName == GET_MEMBER_NAME_CHECKED(UBlueprintHeaderViewSettings, FontSize))
	{
		FBlueprintHeaderViewModule::HeaderViewTextStyle.SetFontSize(FontSize);
	}

	if (PropertyChangedName == GET_MEMBER_NAME_CHECKED(UBlueprintHeaderViewSettings, SelectionColor))
	{
		FBlueprintHeaderViewModule::HeaderViewTableRowStyle.ActiveBrush.TintColor = SelectionColor;
		FBlueprintHeaderViewModule::HeaderViewTableRowStyle.ActiveHoveredBrush.TintColor = SelectionColor;
	}
}