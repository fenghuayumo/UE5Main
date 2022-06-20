// Copyright Epic Games, Inc. All Rights Reserved.


#include "SFilterList.h"
#include "Styling/SlateTypes.h"
#include "Framework/Commands/UIAction.h"
#include "Textures/SlateIcon.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SBoxPanel.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SCheckBox.h"
#include "Styling/SlateTypes.h"
#include "EditorStyleSet.h"
#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSource.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserUtils.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "FrontendFilters.h"
#include "ContentBrowserFrontEndFilterExtension.h"
#include "Misc/NamePermissionList.h"
#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "Widgets/Images/SImage.h"
#include "Styling/SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"

/** Helper struct to avoid friending the whole of SFilterList */
struct FFrontendFilterExternalActivationHelper
{
	static void BindToFilter(TSharedRef<SFilterList> InFilterList, TSharedRef<FFrontendFilter> InFrontendFilter)
	{
		TWeakPtr<FFrontendFilter> WeakFilter = InFrontendFilter;
		InFrontendFilter->SetActiveEvent.AddSP(&InFilterList.Get(), &SFilterList::OnSetFilterActive, WeakFilter);
	}
};

/** A class for check boxes in the filter list. If you double click a filter checkbox, you will enable it and disable all others */
class SFilterCheckBox : public SCheckBox
{
public:
	void SetOnFilterCtrlClicked(const FOnClicked& NewFilterCtrlClicked)
	{
		OnFilterCtrlClicked = NewFilterCtrlClicked;
	}

	void SetOnFilterAltClicked(const FOnClicked& NewFilteAltClicked)
	{
		OnFilterAltClicked = NewFilteAltClicked;
	}

	void SetOnFilterDoubleClicked( const FOnClicked& NewFilterDoubleClicked )
	{
		OnFilterDoubleClicked = NewFilterDoubleClicked;
	}

	void SetOnFilterMiddleButtonClicked( const FOnClicked& NewFilterMiddleButtonClicked )
	{
		OnFilterMiddleButtonClicked = NewFilterMiddleButtonClicked;
	}

	virtual FReply OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent ) override
	{
		if ( InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnFilterDoubleClicked.IsBound() )
		{
			return OnFilterDoubleClicked.Execute();
		}
		else
		{
			return SCheckBox::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
		}
	}

	virtual FReply OnMouseButtonUp( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent ) override
	{
		if (InMouseEvent.IsControlDown() && OnFilterCtrlClicked.IsBound())
		{
			return OnFilterCtrlClicked.Execute();
		}
		else if (InMouseEvent.IsAltDown() && OnFilterAltClicked.IsBound())
		{
			return OnFilterAltClicked.Execute();
		}
		else if( InMouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton && OnFilterMiddleButtonClicked.IsBound() )
		{
			return OnFilterMiddleButtonClicked.Execute();
		}
		else
		{
			SCheckBox::OnMouseButtonUp(InMyGeometry, InMouseEvent);
			return FReply::Handled().ReleaseMouseCapture();
		}
	}

private:
	FOnClicked OnFilterCtrlClicked;
	FOnClicked OnFilterAltClicked;
	FOnClicked OnFilterDoubleClicked;
	FOnClicked OnFilterMiddleButtonClicked;
};

/**
 * A single filter in the filter list. Can be removed by clicking the remove button on it.
 */
class SFilter : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam( FOnRequestRemove, const TSharedRef<SFilter>& /*FilterToRemove*/ );
	DECLARE_DELEGATE_OneParam( FOnRequestRemoveAllButThis, const TSharedRef<SFilter>& /*FilterToKeep*/ );
	DECLARE_DELEGATE_OneParam( FOnRequestEnableOnly, const TSharedRef<SFilter>& /*FilterToEnable*/ );
	DECLARE_DELEGATE( FOnRequestEnableAll );
	DECLARE_DELEGATE( FOnRequestDisableAll );
	DECLARE_DELEGATE( FOnRequestRemoveAll );

	SLATE_BEGIN_ARGS( SFilter ){}

		/** The asset type actions that are associated with this filter */
		SLATE_ARGUMENT( TWeakPtr<IAssetTypeActions>, AssetTypeActions )

		/** If this is an front end filter, this is the filter object */
		SLATE_ARGUMENT( TSharedPtr<FFrontendFilter>, FrontendFilter )

		/** Invoked when the filter toggled */
		SLATE_EVENT( SFilterList::FOnFilterChanged, OnFilterChanged )

		/** Invoked when a request to remove this filter originated from within this filter */
		SLATE_EVENT( FOnRequestRemove, OnRequestRemove )

		/** Invoked when a request to enable only this filter originated from within this filter */
		SLATE_EVENT( FOnRequestEnableOnly, OnRequestEnableOnly )

		/** Invoked when a request to enable all filters originated from within this filter */
		SLATE_EVENT(FOnRequestEnableAll, OnRequestEnableAll)

		/** Invoked when a request to disable all filters originated from within this filter */
		SLATE_EVENT( FOnRequestDisableAll, OnRequestDisableAll )

		/** Invoked when a request to remove all filters originated from within this filter */
		SLATE_EVENT( FOnRequestRemoveAll, OnRequestRemoveAll )

		/** Invoked when a request to remove all filters originated from within this filter */
		SLATE_EVENT( FOnRequestRemoveAllButThis, OnRequestRemoveAllButThis )

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct( const FArguments& InArgs )
	{
		bEnabled = false;
		OnFilterChanged = InArgs._OnFilterChanged;
		AssetTypeActions = InArgs._AssetTypeActions;
		OnRequestRemove = InArgs._OnRequestRemove;
		OnRequestEnableOnly = InArgs._OnRequestEnableOnly;
		OnRequestEnableAll = InArgs._OnRequestEnableAll;
		OnRequestDisableAll = InArgs._OnRequestDisableAll;
		OnRequestRemoveAll = InArgs._OnRequestRemoveAll;
		OnRequestRemoveAllButThis = InArgs._OnRequestRemoveAllButThis;
		FrontendFilter = InArgs._FrontendFilter;

		// Get the tooltip and color of the type represented by this filter
		TAttribute<FText> FilterToolTip;
		FilterColor = FLinearColor::White;
		if ( InArgs._AssetTypeActions.IsValid() )
		{
			TSharedPtr<IAssetTypeActions> TypeActions = InArgs._AssetTypeActions.Pin();
			FilterColor = FLinearColor( TypeActions->GetTypeColor() );

			// No tooltip for asset type filters
		}
		else if ( FrontendFilter.IsValid() )
		{
			FilterColor = FrontendFilter->GetColor();
			FilterToolTip = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(FrontendFilter.ToSharedRef(), &FFrontendFilter::GetToolTipText));
		}

		ChildSlot
		[
			SNew(SBorder)
 			.Padding(1.0f)
			.BorderImage(FAppStyle::Get().GetBrush("ContentBrowser.FilterBackground"))
 			[
				SAssignNew( ToggleButtonPtr, SFilterCheckBox )
				.Style(FAppStyle::Get(), "ContentBrowser.FilterButton")
				.ToolTipText(FilterToolTip)
				.Padding(0.0f)
				.IsChecked(this, &SFilter::IsChecked)
				.OnCheckStateChanged(this, &SFilter::FilterToggled)
				.OnGetMenuContent(this, &SFilter::GetRightClickMenuContent)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("ContentBrowser.FilterImage"))
						.ColorAndOpacity(this, &SFilter::GetFilterImageColorAndOpacity)
					]
					+SHorizontalBox::Slot()
					.Padding(TAttribute<FMargin>(this, &SFilter::GetFilterNamePadding))
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SFilter::GetFilterName)
						.IsEnabled_Lambda([this] {return bEnabled;})
					]
				]
			]
		];

		ToggleButtonPtr->SetOnFilterCtrlClicked(FOnClicked::CreateSP(this, &SFilter::FilterCtrlClicked));
		ToggleButtonPtr->SetOnFilterAltClicked(FOnClicked::CreateSP(this, &SFilter::FilterAltClicked));
		ToggleButtonPtr->SetOnFilterDoubleClicked( FOnClicked::CreateSP(this, &SFilter::FilterDoubleClicked) );
		ToggleButtonPtr->SetOnFilterMiddleButtonClicked( FOnClicked::CreateSP(this, &SFilter::FilterMiddleButtonClicked) );
	}

	/** Sets whether or not this filter is applied to the combined filter */
	void SetEnabled(bool InEnabled, bool InExecuteOnFilterChanged = true)
	{
		if ( InEnabled != bEnabled)
		{
			bEnabled = InEnabled;
			if (InExecuteOnFilterChanged)
			{
				OnFilterChanged.ExecuteIfBound();
			}
		}
	}

	/** Returns true if this filter contributes to the combined filter */
	bool IsEnabled() const
	{
		return bEnabled;
	}

	/** Returns this widgets contribution to the combined filter */
	FARFilter GetBackendFilter() const
	{
		FARFilter Filter;

		if ( AssetTypeActions.IsValid() )
		{
			if (AssetTypeActions.Pin()->CanFilter())
			{
				AssetTypeActions.Pin()->BuildBackendFilter(Filter);
			}
		}

		return Filter;
	}

	/** If this is an front end filter, this is the filter object */
	const TSharedPtr<FFrontendFilter>& GetFrontendFilter() const
	{
		return FrontendFilter;
	}

	/** Gets the asset type actions associated with this filter */
	const TWeakPtr<IAssetTypeActions>& GetAssetTypeActions() const
	{
		return AssetTypeActions;
	}

	/** Returns the display name for this filter */
	FText GetFilterName() const
	{
		FText FilterName;
		if (AssetTypeActions.IsValid())
		{
			TSharedPtr<IAssetTypeActions> TypeActions = AssetTypeActions.Pin();
			FilterName = TypeActions->GetName();
		}
		else if (FrontendFilter.IsValid())
		{
			FilterName = FrontendFilter->GetDisplayName();
		}

		if (FilterName.IsEmpty())
		{
			FilterName = LOCTEXT("UnknownFilter", "???");
		}

		return FilterName;
	}

private:
	/** Handler for when the filter checkbox is clicked */
	void FilterToggled(ECheckBoxState NewState)
	{
		bEnabled = NewState == ECheckBoxState::Checked;
		OnFilterChanged.ExecuteIfBound();
	}

	/** Handler for when the filter checkbox is clicked and a control key is pressed */
	FReply FilterCtrlClicked()
	{
		OnRequestEnableAll.ExecuteIfBound();
		return FReply::Handled();
	}

	/** Handler for when the filter checkbox is clicked and an alt key is pressed */
	FReply FilterAltClicked()
	{
		OnRequestDisableAll.ExecuteIfBound();
		return FReply::Handled();
	}

	/** Handler for when the filter checkbox is double clicked */
	FReply FilterDoubleClicked()
	{
		// Disable all other filters and enable this one.
		OnRequestDisableAll.ExecuteIfBound();
		bEnabled = true;
		OnFilterChanged.ExecuteIfBound();

		return FReply::Handled();
	}

	/** Handler for when the filter checkbox is middle button clicked */
	FReply FilterMiddleButtonClicked()
	{
		RemoveFilter();
		return FReply::Handled();
	}

	/** Handler to create a right click menu */
	TSharedRef<SWidget> GetRightClickMenuContent()
	{
		FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, NULL);

		MenuBuilder.BeginSection("FilterOptions", LOCTEXT("FilterContextHeading", "Filter Options"));
		{
			MenuBuilder.AddMenuEntry(
				FText::Format( LOCTEXT("RemoveFilter", "Remove: {0}"), GetFilterName() ),
				LOCTEXT("RemoveFilterTooltip", "Remove this filter from the list. It can be added again in the filters menu."),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateSP(this, &SFilter::RemoveFilter) )
				);

			MenuBuilder.AddMenuEntry(
				FText::Format( LOCTEXT("EnableOnlyThisFilter", "Enable Only This: {0}"), GetFilterName() ),
				LOCTEXT("EnableOnlyThisFilterTooltip", "Enable only this filter from the list."),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateSP(this, &SFilter::EnableOnly) )
				);

		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("FilterBulkOptions", LOCTEXT("BulkFilterContextHeading", "Bulk Filter Options"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("EnableAllFilters", "Enable All Filters"),
				LOCTEXT("EnableAllFiltersTooltip", "Enables all filters."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SFilter::EnableAllFilters))
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("DisableAllFilters", "Disable All Filters"),
				LOCTEXT("DisableAllFiltersTooltip", "Disables all active filters."),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateSP(this, &SFilter::DisableAllFilters) )
				);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("RemoveAllFilters", "Remove All Filters"),
				LOCTEXT("RemoveAllFiltersTooltip", "Removes all filters from the list."),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateSP(this, &SFilter::RemoveAllFilters) )
				);

			MenuBuilder.AddMenuEntry(
				FText::Format( LOCTEXT("RemoveAllButThisFilter", "Remove All But This: {0}"), GetFilterName() ),
				LOCTEXT("RemoveAllButThisFilterTooltip", "Remove all other filters except this one from the list."),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateSP(this, &SFilter::RemoveAllButThis) )
				);
		}
		MenuBuilder.EndSection();

		if (FrontendFilter.IsValid())
		{
			FrontendFilter->ModifyContextMenu(MenuBuilder);
		}

		return MenuBuilder.MakeWidget();
	}

	/** Removes this filter from the filter list */
	void RemoveFilter()
	{
		TSharedRef<SFilter> Self = SharedThis(this);
		OnRequestRemove.ExecuteIfBound( Self );
	}

	/** Remove all but this filter from the filter list. */
	void RemoveAllButThis()
	{
		TSharedRef<SFilter> Self = SharedThis(this);
		OnRequestRemoveAllButThis.ExecuteIfBound(Self);
	}

	/** Enables only this filter from the filter list */
	void EnableOnly()
	{
		TSharedRef<SFilter> Self = SharedThis(this);
		OnRequestEnableOnly.ExecuteIfBound( Self );
	}

	/** Enables all filters in the list */
	void EnableAllFilters()
	{
		OnRequestEnableAll.ExecuteIfBound();
	}

	/** Disables all active filters in the list */
	void DisableAllFilters()
	{
		OnRequestDisableAll.ExecuteIfBound();
	}

	/** Removes all filters in the list */
	void RemoveAllFilters()
	{
		OnRequestRemoveAll.ExecuteIfBound();
	}

	/** Handler to determine the "checked" state of the filter checkbox */
	ECheckBoxState IsChecked() const
	{
		return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	/** Handler to determine the color of the checkbox when it is checked */
	FSlateColor GetFilterImageColorAndOpacity() const
	{
		return bEnabled ? FilterColor : FAppStyle::Get().GetSlateColor("Colors.Recessed");
	}

	EVisibility GetFilterOverlayVisibility() const
	{
		return bEnabled ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
	}

	/** Handler to determine the padding of the checkbox text when it is pressed */
	FMargin GetFilterNamePadding() const
	{
		return ToggleButtonPtr->IsPressed() ? FMargin(4,2,4,0) : FMargin(4,1,4,1);
	}

private:
	/** Invoked when the filter toggled */
	SFilterList::FOnFilterChanged OnFilterChanged;

	/** Invoked when a request to remove this filter originated from within this filter */
	FOnRequestRemove OnRequestRemove;

	/** Invoked when a request to enable only this filter originated from within this filter */
	FOnRequestEnableOnly OnRequestEnableOnly;

	/** Invoked when a request to enable all filters originated from within this filter */
	FOnRequestEnableAll OnRequestEnableAll;

	/** Invoked when a request to disable all filters originated from within this filter */
	FOnRequestDisableAll OnRequestDisableAll;

	/** Invoked when a request to remove all filters originated from within this filter */
	FOnRequestDisableAll OnRequestRemoveAll;

	/** Invoked when a request to remove all filters except this one originated from within this filter */
	FOnRequestRemoveAllButThis OnRequestRemoveAllButThis;

	/** true when this filter should be applied to the search */
	bool bEnabled;

	/** The asset type actions that are associated with this filter */
	TWeakPtr<IAssetTypeActions> AssetTypeActions;

	/** If this is an front end filter, this is the filter object */
	TSharedPtr<FFrontendFilter> FrontendFilter;

	/** The button to toggle the filter on or off */
	TSharedPtr<SFilterCheckBox> ToggleButtonPtr;

	/** The color of the checkbox for this filter */
	FLinearColor FilterColor;
};

/** Helper that creates a toolbar with all the given SFilter's as toolbar items. Filters that don't fit appear in the overflow menu as toggles. */
static TSharedRef<SWidget> MakeFilterToolBarWidget(const TArray<TSharedRef<SFilter>>& Filters)
{
	FSlimHorizontalToolBarBuilder ToolbarBuilder(TSharedPtr<const FUICommandList>(), FMultiBoxCustomization::None, TSharedPtr<FExtender>(), true);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), "ContentBrowser.FilterToolBar");

	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		ToolbarBuilder.AddWidget(Filter, NAME_None, true, EHorizontalAlignment::HAlign_Fill, FNewMenuDelegate::CreateLambda([Filter](FMenuBuilder& MenuBuilder)
		{
			FUIAction Action;
			Action.GetActionCheckState = FGetActionCheckState::CreateLambda([Filter]()
			{
				return Filter->IsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			});
			Action.ExecuteAction = FExecuteAction::CreateLambda([Filter]()
			{
				Filter->SetEnabled(!Filter->IsEnabled());
			});

			MenuBuilder.AddMenuEntry(Filter->GetFilterName(), FText::GetEmpty(), FSlateIcon(), Action, NAME_None, EUserInterfaceActionType::ToggleButton);
		}));
	}

	return ToolbarBuilder.MakeWidget();
}


/////////////////////
// SFilterList
/////////////////////


void SFilterList::Construct( const FArguments& InArgs )
{
	OnGetContextMenu = InArgs._OnGetContextMenu;
	OnFilterChanged = InArgs._OnFilterChanged;
	FrontendFilters = InArgs._FrontendFilters;
	InitialClassFilters = InArgs._InitialClassFilters;

	TSharedPtr<FFrontendFilterCategory> DefaultCategory = MakeShareable( new FFrontendFilterCategory(LOCTEXT("FrontendFiltersCategory", "Other Filters"), LOCTEXT("FrontendFiltersCategoryTooltip", "Filter assets by all filters in this category.")) );

	// Add all built-in frontend filters here
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_CheckedOut(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_Modified(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_Writable(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_ShowOtherDevelopers(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_ReplicatedBlueprint(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_ShowRedirectors(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_InUseByLoadedLevels(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_UsedInAnyLevel(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_NotUsedInAnyLevel(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_ArbitraryComparisonOperation(DefaultCategory)) );
	AllFrontendFilters.Add(MakeShareable(new FFrontendFilter_Recent(DefaultCategory)));
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_NotSourceControlled(DefaultCategory)) );
	AllFrontendFilters.Add(MakeShareable(new FFrontendFilter_VirtualizedData(DefaultCategory)));

	// Add any global user-defined frontend filters
	for (TObjectIterator<UContentBrowserFrontEndFilterExtension> ExtensionIt(RF_NoFlags); ExtensionIt; ++ExtensionIt)
	{
		if (UContentBrowserFrontEndFilterExtension* PotentialExtension = *ExtensionIt)
		{
			if (PotentialExtension->HasAnyFlags(RF_ClassDefaultObject) && !PotentialExtension->GetClass()->HasAnyClassFlags(CLASS_Deprecated | CLASS_Abstract))
			{
				// Grab the filters
				TArray< TSharedRef<FFrontendFilter> > ExtendedFrontendFilters;
				PotentialExtension->AddFrontEndFilterExtensions(DefaultCategory, ExtendedFrontendFilters);
				AllFrontendFilters.Append(ExtendedFrontendFilters);

				// Grab the categories
				for (const TSharedRef<FFrontendFilter>& FilterRef : ExtendedFrontendFilters)
				{
					TSharedPtr<FFrontendFilterCategory> Category = FilterRef->GetCategory();
					if (Category.IsValid())
					{
						AllFrontendFilterCategories.AddUnique(Category);
					}
				}
			}
		}
	}

	// Add in filters specific to this invocation
	for (const TSharedRef<FFrontendFilter>& Filter : InArgs._ExtraFrontendFilters)
	{
		if (TSharedPtr<FFrontendFilterCategory> Category = Filter->GetCategory())
		{
			AllFrontendFilterCategories.AddUnique(Category);
		}

		AllFrontendFilters.Add(Filter);
	}

	AllFrontendFilterCategories.AddUnique(DefaultCategory);

	
	for (const TSharedRef<FFrontendFilter>& Filter : AllFrontendFilters)
	{
		// Bind external activation event
		FFrontendFilterExternalActivationHelper::BindToFilter(SharedThis(this), Filter);

		// Auto add all inverse filters
		SetFrontendFilterActive(Filter, false);
	}
}

FReply SFilterList::OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if ( MouseEvent.GetEffectingButton() == EKeys::RightMouseButton )
	{
		if ( OnGetContextMenu.IsBound() )
		{
			FReply Reply = FReply::Handled().ReleaseMouseCapture();

			// Get the context menu content. If NULL, don't open a menu.
			TSharedPtr<SWidget> MenuContent = OnGetContextMenu.Execute();

			if ( MenuContent.IsValid() )
			{
				FVector2D SummonLocation = MouseEvent.GetScreenSpacePosition();
				FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
				FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuContent.ToSharedRef(), SummonLocation, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
			}

			return Reply;
		}
	}

	return FReply::Unhandled();
}

const TArray<UClass*>& SFilterList::GetInitialClassFilters()
{
	return InitialClassFilters;
}

bool SFilterList::HasAnyFilters() const
{
	return Filters.Num() > 0;
}

FARFilter SFilterList::GetCombinedBackendFilter() const
{
	FARFilter CombinedFilter;

	// Add all selected filters
	for (int32 FilterIdx = 0; FilterIdx < Filters.Num(); ++FilterIdx)
	{
		if ( Filters[FilterIdx]->IsEnabled() )
		{
			CombinedFilter.Append(Filters[FilterIdx]->GetBackendFilter());
		}
	}

	if ( CombinedFilter.bRecursiveClasses )
	{
		// Add exclusions for AssetTypeActions NOT in the filter.
		// This will prevent assets from showing up that are both derived from an asset in the filter set and derived from an asset not in the filter set
		// Get the list of all asset type actions
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		TArray< TWeakPtr<IAssetTypeActions> > AssetTypeActionsList;
		AssetToolsModule.Get().GetAssetTypeActionsList(AssetTypeActionsList);
		for (const TWeakPtr<IAssetTypeActions>& WeakTypeActions : AssetTypeActionsList)
		{
			if (const TSharedPtr<IAssetTypeActions> TypeActions = WeakTypeActions.Pin())
			{
				if (TypeActions->CanFilter())
				{
					const UClass* TypeClass = TypeActions->GetSupportedClass();
					if (TypeClass && !CombinedFilter.ClassNames.Contains(TypeClass->GetFName()))
					{
						CombinedFilter.RecursiveClassesExclusionSet.Add(TypeClass->GetFName());
					}
				}
			}
		}
	}

	// HACK: A blueprint can be shown as Blueprint or as BlueprintGeneratedClass, but we don't want to distinguish them while filtering.
	// This should be removed, once all blueprints are shown as BlueprintGeneratedClass.
	if(CombinedFilter.ClassNames.Contains(FName(TEXT("Blueprint"))))
	{
		CombinedFilter.ClassNames.AddUnique(FName(TEXT("BlueprintGeneratedClass")));
	}

	return CombinedFilter;
}

TSharedPtr<FFrontendFilter> SFilterList::GetFrontendFilter(const FString& InName) const
{
	for (const TSharedRef<FFrontendFilter>& Filter : AllFrontendFilters)
	{
		if (Filter->GetName() == InName)
		{
			return Filter;
		}
	}
	return TSharedPtr<FFrontendFilter>();
}

TSharedRef<SWidget> SFilterList::ExternalMakeAddFilterMenu(EAssetTypeCategories::Type MenuExpansion)
{
	return MakeAddFilterMenu(MenuExpansion);
}

void SFilterList::EnableAllFilters()
{
	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		Filter->SetEnabled(true, false);
		if (const TSharedPtr<FFrontendFilter>& FrontendFilter = Filter->GetFrontendFilter())
		{
			SetFrontendFilterActive(FrontendFilter.ToSharedRef(), true);
		}
	}

	OnFilterChanged.ExecuteIfBound();
}

void SFilterList::DisableAllFilters()
{
	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		Filter->SetEnabled(false, false);
		if (const TSharedPtr<FFrontendFilter>& FrontendFilter = Filter->GetFrontendFilter())
		{
			SetFrontendFilterActive(FrontendFilter.ToSharedRef(), false);
		}
	}

	OnFilterChanged.ExecuteIfBound();
}

void SFilterList::RemoveAllFilters()
{
	if (HasAnyFilters())
	{
		// Update the frontend filters collection
		for (const TSharedRef<SFilter>& FilterToRemove : Filters)
		{
			if (const TSharedPtr<FFrontendFilter>& FrontendFilter = FilterToRemove->GetFrontendFilter())
			{
				SetFrontendFilterActive(FrontendFilter.ToSharedRef(), false); // Deactivate.
			}
		}
		
		ChildSlot
		[
			SNullWidget::NullWidget
		];

		Filters.Empty();

		// Notify that a filter has changed
		OnFilterChanged.ExecuteIfBound();
	}
}

void SFilterList::RemoveAllButThis(const TSharedRef<SFilter>& FilterToKeep)
{
	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		if (Filter == FilterToKeep)
		{
			continue;
		}

		if (const TSharedPtr<FFrontendFilter>& FrontendFilter = Filter->GetFrontendFilter())
		{
			SetFrontendFilterActive(FrontendFilter.ToSharedRef(), false);
		}
	}

	Filters.Empty();

	AddFilter(FilterToKeep);

	OnFilterChanged.ExecuteIfBound();
}

void SFilterList::DisableFiltersThatHideItems(TArrayView<const FContentBrowserItem> ItemList)
{
	if (HasAnyFilters() && ItemList.Num() > 0)
	{
		// Determine if we should disable backend filters. If any item fails the combined backend filter, disable them all.
		bool bDisableAllBackendFilters = false;
		{
			FContentBrowserDataCompiledFilter CompiledDataFilter;
			{
				static const FName RootPath = "/";

				UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();

				FContentBrowserDataFilter DataFilter;
				DataFilter.bRecursivePaths = true;
				ContentBrowserUtils::AppendAssetFilterToContentBrowserFilter(GetCombinedBackendFilter(), nullptr, nullptr, DataFilter);

				ContentBrowserData->CompileFilter(RootPath, DataFilter, CompiledDataFilter);
			}

			for (const FContentBrowserItem& Item : ItemList)
			{
				if (!Item.IsFile())
				{
					continue;
				}

				FContentBrowserItem::FItemDataArrayView InternalItems = Item.GetInternalItems();
				for (const FContentBrowserItemData& InternalItem : InternalItems)
				{
					UContentBrowserDataSource* ItemDataSource = InternalItem.GetOwnerDataSource();
					if (!ItemDataSource->DoesItemPassFilter(InternalItem, CompiledDataFilter))
					{
						bDisableAllBackendFilters = true;
						break;
					}
				}

				if (bDisableAllBackendFilters)
				{
					break;
				}
			}
		}

		// Iterate over all enabled filters and disable any frontend filters that would hide any of the supplied assets
		// and disable all backend filters if it was determined that the combined backend filter hides any of the assets
		bool ExecuteOnFilteChanged = false;
		for (const TSharedRef<SFilter>& Filter : Filters)
		{
			if (Filter->IsEnabled())
			{
				if (const TSharedPtr<FFrontendFilter>& FrontendFilter = Filter->GetFrontendFilter())
				{
					for (const FContentBrowserItem& Item : ItemList)
					{
						if (!FrontendFilter->IsInverseFilter() && !FrontendFilter->PassesFilter(Item))
						{
							// This is a frontend filter and at least one asset did not pass.
							Filter->SetEnabled(false, false);
							SetFrontendFilterActive(FrontendFilter.ToSharedRef(), false);
							ExecuteOnFilteChanged = true;
						}
					}
				}

				if (bDisableAllBackendFilters)
				{
					FARFilter BackendFilter = Filter->GetBackendFilter();
					if (!BackendFilter.IsEmpty())
					{
						Filter->SetEnabled(false, false);
						ExecuteOnFilteChanged = true;
					}
				}
			}
		}

		if (ExecuteOnFilteChanged)
		{
			OnFilterChanged.ExecuteIfBound();
		}
	}
}

void SFilterList::SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const
{
	FString ActiveTypeFilterString;
	FString EnabledTypeFilterString;
	FString ActiveFrontendFilterString;
	FString EnabledFrontendFilterString;
	for ( auto FilterIt = Filters.CreateConstIterator(); FilterIt; ++FilterIt )
	{
		const TSharedRef<SFilter>& Filter = *FilterIt;

		if ( Filter->GetAssetTypeActions().IsValid() )
		{
			if ( ActiveTypeFilterString.Len() > 0 )
			{
				ActiveTypeFilterString += TEXT(",");
			}

			const FString FilterName = Filter->GetAssetTypeActions().Pin()->GetFilterName().ToString();
			ActiveTypeFilterString += FilterName;

			if ( Filter->IsEnabled() )
			{
				if ( EnabledTypeFilterString.Len() > 0 )
				{
					EnabledTypeFilterString += TEXT(",");
				}

				EnabledTypeFilterString += FilterName;
			}
		}
		else if ( Filter->GetFrontendFilter().IsValid() )
		{
			const TSharedPtr<FFrontendFilter>& FrontendFilter = Filter->GetFrontendFilter();
			if ( ActiveFrontendFilterString.Len() > 0 )
			{
				ActiveFrontendFilterString += TEXT(",");
			}

			const FString FilterName = FrontendFilter->GetName();
			ActiveFrontendFilterString += FilterName;

			if ( Filter->IsEnabled() )
			{
				if ( EnabledFrontendFilterString.Len() > 0 )
				{
					EnabledFrontendFilterString += TEXT(",");
				}

				EnabledFrontendFilterString += FilterName;
			}

			const FString CustomSettingsString = FString::Printf(TEXT("%s.CustomSettings.%s"), *SettingsString, *FilterName);
			FrontendFilter->SaveSettings(IniFilename, IniSection, CustomSettingsString);
		}
	}

	GConfig->SetString(*IniSection, *(SettingsString + TEXT(".ActiveTypeFilters")), *ActiveTypeFilterString, IniFilename);
	GConfig->SetString(*IniSection, *(SettingsString + TEXT(".EnabledTypeFilters")), *EnabledTypeFilterString, IniFilename);
	GConfig->SetString(*IniSection, *(SettingsString + TEXT(".ActiveFrontendFilters")), *ActiveFrontendFilterString, IniFilename);
	GConfig->SetString(*IniSection, *(SettingsString + TEXT(".EnabledFrontendFilters")), *EnabledFrontendFilterString, IniFilename);
}

void SFilterList::LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString)
{
	{
		// Add all the type filters that were found in the ActiveTypeFilters
		FString ActiveTypeFilterString;
		FString EnabledTypeFilterString;
		GConfig->GetString(*IniSection, *(SettingsString + TEXT(".ActiveTypeFilters")), ActiveTypeFilterString, IniFilename);
		GConfig->GetString(*IniSection, *(SettingsString + TEXT(".EnabledTypeFilters")), EnabledTypeFilterString, IniFilename);

		// Parse comma delimited strings into arrays
		TArray<FString> TypeFilterNames;
		TArray<FString> EnabledTypeFilterNames;
		ActiveTypeFilterString.ParseIntoArray(TypeFilterNames, TEXT(","), /*bCullEmpty=*/true);
		EnabledTypeFilterString.ParseIntoArray(EnabledTypeFilterNames, TEXT(","), /*bCullEmpty=*/true);

		// Get the list of all asset type actions
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		TArray< TWeakPtr<IAssetTypeActions> > AssetTypeActionsList;
		AssetToolsModule.Get().GetAssetTypeActionsList(AssetTypeActionsList);

		// For each TypeActions, add any that were active and enable any that were previously enabled
		for ( auto TypeActionsIt = AssetTypeActionsList.CreateConstIterator(); TypeActionsIt; ++TypeActionsIt )
		{
			const TWeakPtr<IAssetTypeActions>& TypeActions = *TypeActionsIt;
			if ( TypeActions.IsValid() && TypeActions.Pin()->CanFilter() && !IsAssetTypeActionsInUse(TypeActions) )
			{
				const FString FilterName = TypeActions.Pin()->GetFilterName().ToString();
				if ( TypeFilterNames.Contains(FilterName) )
				{
					TSharedRef<SFilter> NewFilter = AddFilter(TypeActions);

					if ( EnabledTypeFilterNames.Contains(FilterName) )
					{
						NewFilter->SetEnabled(true, false);
					}
				}
			}
		}
	}

	{
		// Add all the frontend filters that were found in the ActiveFrontendFilters
		FString ActiveFrontendFilterString;	
		FString EnabledFrontendFilterString;
		GConfig->GetString(*IniSection, *(SettingsString + TEXT(".ActiveFrontendFilters")), ActiveFrontendFilterString, IniFilename);
		GConfig->GetString(*IniSection, *(SettingsString + TEXT(".EnabledFrontendFilters")), EnabledFrontendFilterString, IniFilename);

		// Parse comma delimited strings into arrays
		TArray<FString> FrontendFilterNames;
		TArray<FString> EnabledFrontendFilterNames;
		ActiveFrontendFilterString.ParseIntoArray(FrontendFilterNames, TEXT(","), /*bCullEmpty=*/true);
		EnabledFrontendFilterString.ParseIntoArray(EnabledFrontendFilterNames, TEXT(","), /*bCullEmpty=*/true);

		// For each FrontendFilter, add any that were active and enable any that were previously enabled
		for ( auto FrontendFilterIt = AllFrontendFilters.CreateIterator(); FrontendFilterIt; ++FrontendFilterIt )
		{
			TSharedRef<FFrontendFilter>& FrontendFilter = *FrontendFilterIt;
			const FString& FilterName = FrontendFilter->GetName();
			if (!IsFrontendFilterInUse(FrontendFilter))
			{
				if ( FrontendFilterNames.Contains(FilterName) )
				{
					TSharedRef<SFilter> NewFilter = AddFilter(FrontendFilter);

					if ( EnabledFrontendFilterNames.Contains(FilterName) )
					{
						NewFilter->SetEnabled(true, false);
						SetFrontendFilterActive(FrontendFilter, NewFilter->IsEnabled());
					}
				}
			}

			const FString CustomSettingsString = FString::Printf(TEXT("%s.CustomSettings.%s"), *SettingsString, *FilterName);
			FrontendFilter->LoadSettings(IniFilename, IniSection, CustomSettingsString);
		}
	}

	OnFilterChanged.ExecuteIfBound();
}

void SFilterList::SetFrontendFilterCheckState(const TSharedPtr<FFrontendFilter>& InFrontendFilter, ECheckBoxState InCheckState)
{
	if (!InFrontendFilter || InCheckState == ECheckBoxState::Undetermined)
	{
		return;
	}

	// Check if the filter is already checked.
	TSharedRef<FFrontendFilter> FrontendFilter = InFrontendFilter.ToSharedRef();
	bool FrontendFilterChecked = IsFrontendFilterInUse(FrontendFilter);

	if (InCheckState == ECheckBoxState::Checked && !FrontendFilterChecked)
	{
		AddFilter(FrontendFilter)->SetEnabled(true); // Pin a filter widget on the UI and activate the filter. Same behaviour as FrontendFilterClicked()
	}
	else if (InCheckState == ECheckBoxState::Unchecked && FrontendFilterChecked)
	{
		RemoveFilter(FrontendFilter); // Unpin the filter widget and deactivate the filter.
	}
	// else -> Already in the desired 'check' state.
}

ECheckBoxState SFilterList::GetFrontendFilterCheckState(const TSharedPtr<FFrontendFilter>& InFrontendFilter) const
{
	return InFrontendFilter && IsFrontendFilterInUse(InFrontendFilter.ToSharedRef()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool SFilterList::IsFrontendFilterActive(const TSharedPtr<FFrontendFilter>& InFrontendFilter) const
{
	if (InFrontendFilter.IsValid())
	{
		for (const TSharedRef<SFilter>& Filter : Filters)
		{
			if (InFrontendFilter == Filter->GetFrontendFilter())
			{
				return Filter->IsEnabled(); // Is active or not?
			}
		}
	}
	return false;
}

void SFilterList::SetFrontendFilterActive(const TSharedRef<FFrontendFilter>& Filter, bool bActive)
{
	if(Filter->IsInverseFilter())
	{
		//Inverse filters are active when they are "disabled"
		bActive = !bActive;
	}
	Filter->ActiveStateChanged(bActive);

	if ( bActive )
	{
		FrontendFilters->Add(Filter);
	}
	else
	{
		FrontendFilters->Remove(Filter);
	}
}

TSharedRef<SFilter> SFilterList::AddFilter(const TWeakPtr<IAssetTypeActions>& AssetTypeActions)
{
	TSharedRef<SFilter> NewFilter =
		SNew(SFilter)
		.AssetTypeActions(AssetTypeActions)
		.OnFilterChanged(OnFilterChanged)
		.OnRequestRemove(this, &SFilterList::RemoveFilterAndUpdate)
		.OnRequestEnableOnly(this, &SFilterList::EnableOnlyThisFilter)
		.OnRequestEnableAll(this, &SFilterList::EnableAllFilters)
		.OnRequestDisableAll(this, &SFilterList::DisableAllFilters)
		.OnRequestRemoveAll(this, &SFilterList::RemoveAllFilters)
		.OnRequestRemoveAllButThis(this, &SFilterList::RemoveAllButThis);

	AddFilter( NewFilter );

	return NewFilter;
}

TSharedRef<SFilter> SFilterList::AddFilter(const TSharedRef<FFrontendFilter>& FrontendFilter)
{
	TSharedRef<SFilter> NewFilter =
		SNew(SFilter)
		.FrontendFilter(FrontendFilter)
		.OnFilterChanged( this, &SFilterList::FrontendFilterChanged, FrontendFilter )
		.OnRequestRemove(this, &SFilterList::RemoveFilterAndUpdate)
		.OnRequestEnableOnly(this, &SFilterList::EnableOnlyThisFilter)
		.OnRequestEnableAll(this, &SFilterList::EnableAllFilters)
		.OnRequestDisableAll(this, &SFilterList::DisableAllFilters)
		.OnRequestRemoveAll(this, &SFilterList::RemoveAllFilters)
		.OnRequestRemoveAllButThis(this, &SFilterList::RemoveAllButThis);

	AddFilter( NewFilter );

	return NewFilter;
}

void SFilterList::AddFilter(const TSharedRef<SFilter>& FilterToAdd)
{
	Filters.Add(FilterToAdd);
	
	ChildSlot
	[
		MakeFilterToolBarWidget(Filters)
	];
}

void SFilterList::RemoveFilter(const TWeakPtr<IAssetTypeActions>& AssetTypeActions, bool ExecuteOnFilterChanged)
{
	TSharedPtr<SFilter> FilterToRemove;
	for ( auto FilterIt = Filters.CreateConstIterator(); FilterIt; ++FilterIt )
	{
		const TWeakPtr<IAssetTypeActions>& Actions = (*FilterIt)->GetAssetTypeActions();
		if ( Actions.IsValid() && Actions == AssetTypeActions)
		{
			FilterToRemove = *FilterIt;
			break;
		}
	}

	if ( FilterToRemove.IsValid() )
	{
		if (ExecuteOnFilterChanged)
		{
			RemoveFilterAndUpdate(FilterToRemove.ToSharedRef());
		}
		else
		{
			RemoveFilter(FilterToRemove.ToSharedRef());
		}
	}
}

void SFilterList::EnableOnlyThisFilter(const TSharedRef<SFilter>& FilterToEnable)
{
	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		bool bEnable = Filter == FilterToEnable;
		Filter->SetEnabled(bEnable, /*ExecuteOnFilterChange*/false);
		if (const TSharedPtr<FFrontendFilter>& FrontendFilter = Filter->GetFrontendFilter())
		{
			SetFrontendFilterActive(FrontendFilter.ToSharedRef(), bEnable);
		}
	}

	OnFilterChanged.ExecuteIfBound();
}

void SFilterList::RemoveFilter(const TSharedRef<FFrontendFilter>& FrontendFilter, bool ExecuteOnFilterChanged)
{
	TSharedPtr<SFilter> FilterToRemove;
	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		if (Filter->GetFrontendFilter() == FrontendFilter)
		{
			FilterToRemove = Filter;
			break;
		}
	}

	if (FilterToRemove.IsValid())
	{
		if (ExecuteOnFilterChanged)
		{
			RemoveFilterAndUpdate(FilterToRemove.ToSharedRef());
		}
		else
		{
			RemoveFilter(FilterToRemove.ToSharedRef());
		}
	}
}

void SFilterList::RemoveFilter(const TSharedRef<SFilter>& FilterToRemove)
{
	Filters.Remove(FilterToRemove);

	if (const TSharedPtr<FFrontendFilter>& FrontendFilter = FilterToRemove->GetFrontendFilter()) // Is valid?
	{
		// Update the frontend filters collection
		SetFrontendFilterActive(FrontendFilter.ToSharedRef(), false);
		OnFilterChanged.ExecuteIfBound();
	}

	ChildSlot
	[
		MakeFilterToolBarWidget(Filters)
	];
}

void SFilterList::RemoveFilterAndUpdate(const TSharedRef<SFilter>& FilterToRemove)
{
	RemoveFilter(FilterToRemove);

	// Notify that a filter has changed
	OnFilterChanged.ExecuteIfBound();
}

void SFilterList::FrontendFilterChanged(TSharedRef<FFrontendFilter> FrontendFilter)
{
	TSharedPtr<SFilter> FilterToUpdate;
	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		if (Filter->GetFrontendFilter() == FrontendFilter)
		{
			FilterToUpdate = Filter;
			break;
		}
	}

	if (FilterToUpdate.IsValid())
	{
		SetFrontendFilterActive(FrontendFilter, FilterToUpdate->IsEnabled());
		OnFilterChanged.ExecuteIfBound();
	}
}

void SFilterList::CreateFiltersMenuCategory(FToolMenuSection& Section, const TArray<TWeakPtr<IAssetTypeActions>> AssetTypeActionsList) const
{
	for (int32 ClassIdx = 0; ClassIdx < AssetTypeActionsList.Num(); ++ClassIdx)
	{
		const TWeakPtr<IAssetTypeActions>& WeakTypeActions = AssetTypeActionsList[ClassIdx];
		if ( WeakTypeActions.IsValid() )
		{
			TSharedPtr<IAssetTypeActions> TypeActions = WeakTypeActions.Pin();
			if ( TypeActions.IsValid() && TypeActions->CanFilter() )
			{
				const FText& LabelText = TypeActions->GetName();
				Section.AddMenuEntry(
					NAME_None,
					LabelText,
					FText::Format( LOCTEXT("FilterByTooltipPrefix", "Filter by {0}"), LabelText ),
					FSlateIconFinder::FindIconForClass(TypeActions->GetSupportedClass()),
					FUIAction(
						FExecuteAction::CreateSP( const_cast<SFilterList*>(this), &SFilterList::FilterByTypeClicked, WeakTypeActions ),
						FCanExecuteAction(),
						FIsActionChecked::CreateSP(this, &SFilterList::IsAssetTypeActionsInUse, WeakTypeActions ) ),
					EUserInterfaceActionType::ToggleButton
					);
			}
		}
	}
}

void SFilterList::CreateFiltersMenuCategory(UToolMenu* InMenu, const TArray<TWeakPtr<IAssetTypeActions>> AssetTypeActionsList) const
{
	CreateFiltersMenuCategory(InMenu->AddSection("Section"), AssetTypeActionsList);
}

void SFilterList::CreateOtherFiltersMenuCategory(FToolMenuSection& Section, TSharedPtr<FFrontendFilterCategory> MenuCategory) const
{
	for (const TSharedRef<FFrontendFilter>& FrontendFilter : AllFrontendFilters)
	{
		if(FrontendFilter->GetCategory() == MenuCategory)
		{
			Section.AddMenuEntry(
				NAME_None,
				FrontendFilter->GetDisplayName(),
				FrontendFilter->GetToolTipText(),
				FSlateIcon(FEditorStyle::GetStyleSetName(), FrontendFilter->GetIconName()),
				FUIAction(
				FExecuteAction::CreateSP(const_cast<SFilterList*>(this), &SFilterList::FrontendFilterClicked, FrontendFilter),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SFilterList::IsFrontendFilterInUse, FrontendFilter)),
				EUserInterfaceActionType::ToggleButton
				);
		}
	}
}

void SFilterList::CreateOtherFiltersMenuCategory(UToolMenu* InMenu, TSharedPtr<FFrontendFilterCategory> MenuCategory) const
{
	CreateOtherFiltersMenuCategory(InMenu->AddSection("Section"), MenuCategory);
}

bool IsFilteredByPicker(const TArray<UClass*>& FilterClassList, UClass* TestClass)
{
	if (FilterClassList.Num() == 0)
	{
		return false;
	}
	for (const UClass* Class : FilterClassList)
	{
		if (TestClass->IsChildOf(Class))
		{
			return false;
		}
	}
	return true;
}

void SFilterList::PopulateAddFilterMenu(UToolMenu* Menu)
{
	EAssetTypeCategories::Type MenuExpansion = EAssetTypeCategories::Basic;
	if (UContentBrowserFilterListContext* Context = Menu->FindContext<UContentBrowserFilterListContext>())
	{
		MenuExpansion = Context->MenuExpansion;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

	// A local struct to describe a category in the filter menu
	struct FCategoryMenu
	{
		FText Name;
		FText Tooltip;
		TArray<TWeakPtr<IAssetTypeActions>> Assets;

		//Menu section
		FName SectionExtensionHook;
		FText SectionHeading;

		FCategoryMenu(const FText& InName, const FText& InTooltip, const FName& InSectionExtensionHook, const FText& InSectionHeading)
			: Name(InName)
			, Tooltip(InTooltip)
			, Assets()
			, SectionExtensionHook(InSectionExtensionHook)
			, SectionHeading(InSectionHeading)
		{}
	};

	// Create a map of Categories to Menus
	TMap<EAssetTypeCategories::Type, FCategoryMenu> CategoryToMenuMap;

	// Add the Basic category
	CategoryToMenuMap.Add(EAssetTypeCategories::Basic, FCategoryMenu( LOCTEXT("BasicFilter", "Basic"), LOCTEXT("BasicFilterTooltip", "Filter by basic assets."), "ContentBrowserFilterBasicAsset", LOCTEXT("BasicAssetsMenuHeading", "Basic Assets") ) );

	// Add the advanced categories
	TArray<FAdvancedAssetCategory> AdvancedAssetCategories;
	AssetToolsModule.Get().GetAllAdvancedAssetCategories(/*out*/ AdvancedAssetCategories);

	for (const FAdvancedAssetCategory& AdvancedAssetCategory : AdvancedAssetCategories)
	{
		const FName ExtensionPoint = NAME_None;
		const FText SectionHeading = FText::Format(LOCTEXT("WildcardFilterHeadingHeadingTooltip", "{0} Assets."), AdvancedAssetCategory.CategoryName);
		const FText Tooltip = FText::Format(LOCTEXT("WildcardFilterTooltip", "Filter by {0}."), SectionHeading);
		CategoryToMenuMap.Add(AdvancedAssetCategory.CategoryType, FCategoryMenu(AdvancedAssetCategory.CategoryName, Tooltip, ExtensionPoint, SectionHeading));
	}

	// Get the browser type maps
	TArray<TWeakPtr<IAssetTypeActions>> AssetTypeActionsList;
	AssetToolsModule.Get().GetAssetTypeActionsList(AssetTypeActionsList);

	// Sort the list
	struct FCompareIAssetTypeActions
	{
		FORCEINLINE bool operator()( const TWeakPtr<IAssetTypeActions>& A, const TWeakPtr<IAssetTypeActions>& B ) const
		{
			return A.Pin()->GetName().CompareTo( B.Pin()->GetName() ) == -1;
		}
	};
	AssetTypeActionsList.Sort( FCompareIAssetTypeActions() );

	const TSharedRef<FNamePermissionList>& AssetClassPermissionList = AssetToolsModule.Get().GetAssetClassPermissionList(EAssetClassAction::CreateAsset);

	// For every asset type, move it into all the categories it should appear in
	for (int32 ClassIdx = 0; ClassIdx < AssetTypeActionsList.Num(); ++ClassIdx)
	{
		const TWeakPtr<IAssetTypeActions>& WeakTypeActions = AssetTypeActionsList[ClassIdx];
		if ( WeakTypeActions.IsValid() )
		{
			TSharedPtr<IAssetTypeActions> TypeActions = WeakTypeActions.Pin();
			if ( ensure(TypeActions.IsValid()) && TypeActions->CanFilter() )
			{
				UClass* SupportedClass = TypeActions->GetSupportedClass();
				if ((!SupportedClass || AssetClassPermissionList->PassesFilter(SupportedClass->GetFName())) && !IsFilteredByPicker(InitialClassFilters, SupportedClass))
				{
					for ( auto MenuIt = CategoryToMenuMap.CreateIterator(); MenuIt; ++MenuIt )
					{
						if ( TypeActions->GetCategories() & MenuIt.Key() )
						{
							// This is a valid asset type which can be filtered, add it to the correct category
							FCategoryMenu& CategoryMenu = MenuIt.Value();
							CategoryMenu.Assets.Add( WeakTypeActions );
						}
					}
				}
			}
		}
	}

	for (auto MenuIt = CategoryToMenuMap.CreateIterator(); MenuIt; ++MenuIt)
	{
		if (MenuIt.Value().Assets.Num() == 0)
		{
			CategoryToMenuMap.Remove(MenuIt.Key());
		}
	}

	{
		FToolMenuSection& Section = Menu->AddSection("ContentBrowserResetFilters");
		Section.AddMenuEntry(
			"ResetFilters",
			LOCTEXT("FilterListResetFilters", "Reset Filters"),
			LOCTEXT("FilterListResetToolTip", "Resets current filter selection"),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "PropertyWindow.DiffersFromDefault"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SFilterList::OnResetFilters),
				FCanExecuteAction::CreateLambda([this]() { return HasAnyFilters(); }))
		);
	}

	// First add the expanded category, this appears as standard entries in the list (Note: intentionally not using FindChecked here as removing it from the map later would cause the ref to be garbage)
	FCategoryMenu* ExpandedCategory = CategoryToMenuMap.Find( MenuExpansion );
	check( ExpandedCategory );

	{
		FToolMenuSection& Section = Menu->AddSection(ExpandedCategory->SectionExtensionHook, ExpandedCategory->SectionHeading);
		if(MenuExpansion == EAssetTypeCategories::Basic)
		{
			// If we are doing a full menu (i.e expanding basic) we add a menu entry which toggles all other categories
			Section.AddMenuEntry(
				NAME_None,
				ExpandedCategory->Name,
				ExpandedCategory->Tooltip,
				FSlateIcon(FAppStyle::Get().GetStyleSetName(), "PlacementBrowser.Icons.Basic"),
				FUIAction(
				FExecuteAction::CreateSP( this, &SFilterList::FilterByTypeCategoryClicked, MenuExpansion ),
				FCanExecuteAction(),
				FGetActionCheckState::CreateSP(this, &SFilterList::IsAssetTypeCategoryChecked, MenuExpansion ) ),
				EUserInterfaceActionType::ToggleButton
				);
		}

		// Now populate with all the basic assets
		SFilterList::CreateFiltersMenuCategory( Section, ExpandedCategory->Assets);
	}

	// Remove the basic category from the map now, as this is treated differently and is no longer needed.
	ExpandedCategory = nullptr;
	CategoryToMenuMap.Remove(EAssetTypeCategories::Basic);

	// If we have expanded Basic, assume we are in full menu mode and add all the other categories
	{
		FToolMenuSection& Section = Menu->AddSection("ContentBrowserFilterAdvancedAsset", LOCTEXT("AdvancedAssetsMenuHeading", "Other Assets"));
		if(MenuExpansion == EAssetTypeCategories::Basic)
		{
			// Sort by category name so that we add the submenus in alphabetical order
			CategoryToMenuMap.ValueSort([](const FCategoryMenu& A, const FCategoryMenu& B) {
				return A.Name.CompareTo(B.Name) < 0;
			});

			// For all the remaining categories, add them as submenus
			for (const TPair<EAssetTypeCategories::Type, FCategoryMenu>& CategoryMenuPair : CategoryToMenuMap)
			{
				Section.AddSubMenu(
					NAME_None,
					CategoryMenuPair.Value.Name,
					CategoryMenuPair.Value.Tooltip,
					FNewToolMenuDelegate::CreateSP(this, &SFilterList::CreateFiltersMenuCategory, CategoryMenuPair.Value.Assets),
					FUIAction(
					FExecuteAction::CreateSP(this, &SFilterList::FilterByTypeCategoryClicked, CategoryMenuPair.Key),
					FCanExecuteAction(),
					FGetActionCheckState::CreateSP(this, &SFilterList::IsAssetTypeCategoryChecked, CategoryMenuPair.Key)),
					EUserInterfaceActionType::ToggleButton
					);
			}
		}

		// Now add the other filter which aren't assets
		for (const TSharedPtr<FFrontendFilterCategory>& Category : AllFrontendFilterCategories)
		{
			Section.AddSubMenu(
				NAME_None,
				Category->Title,
				Category->Tooltip,
				FNewToolMenuDelegate::CreateSP(this, &SFilterList::CreateOtherFiltersMenuCategory, Category),
				FUIAction(
				FExecuteAction::CreateSP( this, &SFilterList::FrontendFilterCategoryClicked, Category ),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SFilterList::IsFrontendFilterCategoryInUse, Category ) ),
				EUserInterfaceActionType::ToggleButton
				);
		}
	}

	Menu->AddSection("ContentBrowserFilterMiscAsset", LOCTEXT("MiscAssetsMenuHeading", "Misc Options") );
}

TSharedRef<SWidget> SFilterList::MakeAddFilterMenu(EAssetTypeCategories::Type MenuExpansion)
{
	const FName FilterMenuName = "ContentBrowser.FilterMenu";
	if (!UToolMenus::Get()->IsMenuRegistered(FilterMenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(FilterMenuName);
		Menu->bShouldCloseWindowAfterMenuSelection = true;
		Menu->bCloseSelfOnly = true;

		Menu->AddDynamicSection(NAME_None, FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			if (UContentBrowserFilterListContext* Context = InMenu->FindContext<UContentBrowserFilterListContext>())
			{
				if (TSharedPtr<SFilterList> FilterList = Context->FilterList.Pin())
				{
					FilterList->PopulateAddFilterMenu(InMenu);
				}
			}
		}));
	}

	UContentBrowserFilterListContext* ContentBrowserFilterListContext = NewObject<UContentBrowserFilterListContext>();
	ContentBrowserFilterListContext->FilterList = SharedThis(this);
	ContentBrowserFilterListContext->MenuExpansion = MenuExpansion;
	FToolMenuContext ToolMenuContext(ContentBrowserFilterListContext);

	return UToolMenus::Get()->GenerateWidget(FilterMenuName, ToolMenuContext);
}

void SFilterList::FilterByTypeClicked(TWeakPtr<IAssetTypeActions> AssetTypeActions)
{
	if (AssetTypeActions.IsValid())
	{
		if (IsAssetTypeActionsInUse(AssetTypeActions))
		{
			RemoveFilter(AssetTypeActions);
		}
		else
		{
			TSharedRef<SFilter> NewFilter = AddFilter(AssetTypeActions);
			NewFilter->SetEnabled(true);
		}
	}
}

bool SFilterList::IsAssetTypeActionsInUse(TWeakPtr<IAssetTypeActions> AssetTypeActions) const
{
	if (!AssetTypeActions.IsValid())
	{
		return false;
	}

	TSharedPtr<IAssetTypeActions> TypeActions = AssetTypeActions.Pin();
	if (!TypeActions.IsValid())
	{
		return false;
	}

	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		if (Filter->GetAssetTypeActions().Pin() == TypeActions)
		{
			return true;
		}
	}

	return false;
}

void SFilterList::FilterByTypeCategoryClicked(EAssetTypeCategories::Type Category)
{
	TArray<TWeakPtr<IAssetTypeActions>> TypeActionsList;
	GetTypeActionsForCategory(Category, TypeActionsList);

	// Sort the list of type actions so that we add new filters in alphabetical order
	TypeActionsList.Sort([](const TWeakPtr<IAssetTypeActions>& A, const TWeakPtr<IAssetTypeActions>& B) {
		const FText NameA = A.IsValid() ? A.Pin()->GetName() : FText::GetEmpty();
		const FText NameB = B.IsValid() ? B.Pin()->GetName() : FText::GetEmpty();
		return NameA.CompareTo(NameB) < 0;
	});

	bool bFullCategoryInUse = IsAssetTypeCategoryInUse(Category);
	bool ExecuteOnFilterChanged = false;

	for (const TWeakPtr<IAssetTypeActions>& AssetTypeActions : TypeActionsList)
	{
		if (AssetTypeActions.IsValid())
		{
			if (bFullCategoryInUse)
			{
				RemoveFilter(AssetTypeActions);
				ExecuteOnFilterChanged = true;
			}
			else if (!IsAssetTypeActionsInUse(AssetTypeActions))
			{
				TSharedRef<SFilter> NewFilter = AddFilter(AssetTypeActions);
				NewFilter->SetEnabled(true, false);
				ExecuteOnFilterChanged = true;
			}
		}
	}

	if (ExecuteOnFilterChanged)
	{
		OnFilterChanged.ExecuteIfBound();
	}
}

bool SFilterList::IsAssetTypeCategoryInUse(EAssetTypeCategories::Type Category) const
{
	ECheckBoxState AssetTypeCategoryCheckState = IsAssetTypeCategoryChecked(Category);

	if (AssetTypeCategoryCheckState == ECheckBoxState::Unchecked)
	{
		return false;
	}

	// An asset type category is in use if any of its type actions are in use (ECheckBoxState::Checked or ECheckBoxState::Undetermined)
	return true;
}

ECheckBoxState SFilterList::IsAssetTypeCategoryChecked(EAssetTypeCategories::Type Category) const
{
	TArray<TWeakPtr<IAssetTypeActions>> TypeActionsList;
	GetTypeActionsForCategory(Category, TypeActionsList);

	bool bIsAnyActionInUse = false;
	bool bIsAnyActionNotInUse = false;

	for (const TWeakPtr<IAssetTypeActions>& AssetTypeActions : TypeActionsList)
	{
		if (AssetTypeActions.IsValid())
		{
			if (IsAssetTypeActionsInUse(AssetTypeActions))
			{
				bIsAnyActionInUse = true;
			}
			else
			{
				bIsAnyActionNotInUse = true;
			}

			if (bIsAnyActionInUse && bIsAnyActionNotInUse)
			{
				return ECheckBoxState::Undetermined;
			}
		}
	}

	if (bIsAnyActionInUse)
	{
		return ECheckBoxState::Checked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}

void SFilterList::GetTypeActionsForCategory(EAssetTypeCategories::Type Category, TArray< TWeakPtr<IAssetTypeActions> >& TypeActions) const
{
	// Load the asset tools module
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<TWeakPtr<IAssetTypeActions>> AssetTypeActionsList;
	AssetToolsModule.Get().GetAssetTypeActionsList(AssetTypeActionsList);
	const TSharedRef<FNamePermissionList>& AssetClassPermissionList = AssetToolsModule.Get().GetAssetClassPermissionList(EAssetClassAction::ViewAsset);

	// Find all asset type actions that match the category
	for (int32 ClassIdx = 0; ClassIdx < AssetTypeActionsList.Num(); ++ClassIdx)
	{
		const TWeakPtr<IAssetTypeActions>& WeakTypeActions = AssetTypeActionsList[ClassIdx];
		TSharedPtr<IAssetTypeActions> AssetTypeActions = WeakTypeActions.Pin();

		if (ensure(AssetTypeActions.IsValid()) && AssetTypeActions->CanFilter() && AssetTypeActions->GetCategories() & Category)
		{
			if (AssetTypeActions->GetSupportedClass() == nullptr || AssetClassPermissionList->PassesFilter(AssetTypeActions->GetSupportedClass()->GetFName()))
			{
				TypeActions.Add(WeakTypeActions);
			}
		}
	}
}

void SFilterList::FrontendFilterClicked(TSharedRef<FFrontendFilter> FrontendFilter)
{
	if (IsFrontendFilterInUse(FrontendFilter))
	{
		RemoveFilter(FrontendFilter);
	}
	else
	{
		TSharedRef<SFilter> NewFilter = AddFilter(FrontendFilter);
		NewFilter->SetEnabled(true);
	}
}

bool SFilterList::IsFrontendFilterInUse(TSharedRef<FFrontendFilter> FrontendFilter) const
{
	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		if (Filter->GetFrontendFilter() == FrontendFilter)
		{
			return true;
		}
	}

	return false;
}

void SFilterList::FrontendFilterCategoryClicked(TSharedPtr<FFrontendFilterCategory> MenuCategory)
{
	bool bFullCategoryInUse = IsFrontendFilterCategoryInUse(MenuCategory);
	bool ExecuteOnFilterChanged = false;

	for (const TSharedRef<FFrontendFilter>& FrontendFilter : AllFrontendFilters)
	{
		if (FrontendFilter->GetCategory() == MenuCategory)
		{
			if (bFullCategoryInUse)
			{
				RemoveFilter(FrontendFilter, false);
				ExecuteOnFilterChanged = true;
			}
			else if (!IsFrontendFilterInUse(FrontendFilter))
			{
				TSharedRef<SFilter> NewFilter = AddFilter(FrontendFilter);
				NewFilter->SetEnabled(true, false);
				SetFrontendFilterActive(FrontendFilter, NewFilter->IsEnabled());
				ExecuteOnFilterChanged = true;
			}
		}
	}

	if (ExecuteOnFilterChanged)
	{
		OnFilterChanged.ExecuteIfBound();
	}
}

bool SFilterList::IsFrontendFilterCategoryInUse(TSharedPtr<FFrontendFilterCategory> MenuCategory) const
{
	for (const TSharedRef<FFrontendFilter>& FrontendFilter : AllFrontendFilters)
	{
		if (FrontendFilter->GetCategory() == MenuCategory && !IsFrontendFilterInUse(FrontendFilter))
		{
			return false;
		}
	}

	return true;
}

void SFilterList::OnResetFilters()
{
	RemoveAllFilters();
}

void SFilterList::OnSetFilterActive(bool bInActive, TWeakPtr<FFrontendFilter> InWeakFilter)
{
	TSharedPtr<FFrontendFilter> Filter = InWeakFilter.Pin();
	if (Filter.IsValid())
	{
		if (!IsFrontendFilterInUse(Filter.ToSharedRef()))
		{
			TSharedRef<SFilter> NewFilter = AddFilter(Filter.ToSharedRef());
			NewFilter->SetEnabled(bInActive);
		}
	}
}

#undef LOCTEXT_NAMESPACE