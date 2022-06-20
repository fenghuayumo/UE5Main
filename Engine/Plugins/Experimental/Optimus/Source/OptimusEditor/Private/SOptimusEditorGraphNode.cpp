// Copyright Epic Games, Inc. All Rights Reserved.

#include "SOptimusEditorGraphNode.h"

#include "OptimusEditorHelpers.h"
#include "OptimusEditorGraph.h"
#include "OptimusEditorGraphNode.h"
#include "OptimusEditorStyle.h"

#include "OptimusActionStack.h"
#include "OptimusNode.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodePin.h"
#include "IOptimusNodeAdderPinProvider.h"

#include "Editor.h"
#include "GraphEditorSettings.h"
#include "SGraphPin.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Views/STreeView.h"


#define LOCTEXT_NAMESPACE "SOptimusEditorGraphNode"

static const FName NAME_Pin_Resource_Connected("Node.Pin.Resource_Connected");
static const FName NAME_Pin_Resource_Disconnected("Node.Pin.Resource_Disconnected");
static const FName NAME_Pin_Value_Connected("Node.Pin.Value_Connected");
static const FName NAME_Pin_Value_Disconnected("Node.Pin.Value_Disconnected");

static const FSlateBrush* CachedImg_Pin_Resource_Connected = nullptr;
static const FSlateBrush* CachedImg_Pin_Resource_Disconnected = nullptr;
static const FSlateBrush* CachedImg_Pin_Value_Connected = nullptr;
static const FSlateBrush* CachedImg_Pin_Value_Disconnected = nullptr;


class SOptimusEditorExpanderArrow : public SExpanderArrow
{
	SLATE_BEGIN_ARGS(SOptimusEditorExpanderArrow) {}

	SLATE_ARGUMENT(bool, LeftAligned)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<class ITableRow>& TableRow)
	{
		bLeftAligned = InArgs._LeftAligned;

		SExpanderArrow::Construct(
		    SExpanderArrow::FArguments()
		        .IndentAmount(8.0f),
		    TableRow);

		// override padding
		ChildSlot.Padding(TAttribute<FMargin>(this, &SOptimusEditorExpanderArrow::GetExpanderPadding_Extended));

		// override image
		ExpanderArrow->SetContent(
		    SNew(SImage)
		        .Image(this, &SOptimusEditorExpanderArrow::GetExpanderImage_Extended)
		        .ColorAndOpacity(FSlateColor::UseForeground()));
	}

	FMargin GetExpanderPadding_Extended() const
	{
		const int32 NestingDepth = FMath::Max(0, OwnerRowPtr.Pin()->GetIndentLevel() - BaseIndentLevel.Get());
		const float Indent = IndentAmount.Get(8.0f);
		return bLeftAligned ? FMargin(NestingDepth * Indent, 0, 0, 0) : FMargin(0, 0, NestingDepth * Indent, 0);
	}

	const FSlateBrush* GetExpanderImage_Extended() const
	{
		const bool bIsItemExpanded = OwnerRowPtr.Pin()->IsItemExpanded();

		// FIXME: COllapse to a table.
		FName ResourceName;
		if (bIsItemExpanded)
		{
			if (ExpanderArrow->IsHovered())
			{
				static FName ExpandedHoveredLeftName("Node.PinTree.Arrow_Expanded_Hovered_Left");
				static FName ExpandedHoveredRightName("Node.PinTree.Arrow_Expanded_Hovered_Right");
				ResourceName = bLeftAligned ? ExpandedHoveredLeftName : ExpandedHoveredRightName;
			}
			else
			{
				static FName ExpandedLeftName("Node.PinTree.Arrow_Expanded_Left");
				static FName ExpandedRightName("Node.PinTree.Arrow_Expanded_Right");
				ResourceName = bLeftAligned ? ExpandedLeftName : ExpandedRightName;
			}
		}
		else
		{
			if (ExpanderArrow->IsHovered())
			{
				static FName CollapsedHoveredLeftName("Node.PinTree.Arrow_Collapsed_Hovered_Left");
				static FName CollapsedHoveredRightName("Node.PinTree.Arrow_Collapsed_Hovered_Right");
				ResourceName = bLeftAligned ? CollapsedHoveredLeftName : CollapsedHoveredRightName;
			}
			else
			{
				static FName CollapsedLeftName("Node.PinTree.Arrow_Collapsed_Left");
				static FName CollapsedRightName("Node.PinTree.Arrow_Collapsed_Right");
				ResourceName = bLeftAligned ? CollapsedLeftName : CollapsedRightName;
			}
		}

		return FOptimusEditorStyle::Get().GetBrush(ResourceName);
	}

	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
	{
		return FCursorReply::Cursor(EMouseCursor::Default);
	}

	bool bLeftAligned;
};


class SOptimusEditorGraphPinWidget : public SCompoundWidget
{
public:	
	SLATE_BEGIN_ARGS(SOptimusEditorGraphPinWidget) {}
		/** The text displayed for the pin label*/
		SLATE_ATTRIBUTE(FText, PinLabel)
	SLATE_END_ARGS()
	
	void Construct(
		const FArguments& InArgs,
		TSharedRef<SGraphPin> InPinWidget,
		bool bIsValue,
		TSharedPtr<ITableRow> InOptionalOwnerRow)
	{
		const UEdGraphPin* InGraphPin = InPinWidget->GetPinObj();
		check(InGraphPin);
		const bool bIsLeaf = InGraphPin->SubPins.Num() == 0;
		const bool bIsInput = InGraphPin->Direction == EGPD_Input;
		const bool bLeftAligned = bIsInput;
		
		const TSharedRef<SWidget> LabelWidget = SNew(STextBlock)
			.Text(InArgs._PinLabel)
			.TextStyle(FEditorStyle::Get(), NAME_DefaultPinLabelStyle)
			.ColorAndOpacity(FLinearColor::White)
			// .ColorAndOpacity(this, &SOptimusEditorGraphNode::GetPinTextColor, WeakPin)
			;

		TSharedRef<SWidget> LabelContent = LabelWidget;
		const TSharedRef<SWidget> PinContent = InPinWidget;
		
		if (bIsLeaf && bIsInput && bIsValue)
		{
			TSharedPtr<SWidget> InputValueWidget = InPinWidget->GetValueWidget();

			if (InputValueWidget.IsValid())
			{
				TSharedRef<SWidget> LabelAndInputWidget = 
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(2.0f)
					[
						LabelWidget
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.Padding(2.0f, 2.0f, 18.0f, 2.0f)
					[
						InputValueWidget.IsValid() ? InputValueWidget.ToSharedRef() : SNew(SSpacer)
					];
				LabelContent = LabelAndInputWidget;
			}
		}

		// To allow the label to be a part of the hoverable set of widgets for the pin.
		// HoverWidgetLabels.Add(LabelWidget);
		// HoverWidgetPins.Add(PinWidget.ToSharedRef());


		const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
		FMargin InputPadding = Settings->GetInputPinPadding();
		InputPadding.Top = InputPadding.Bottom = 3.0f;
		InputPadding.Right = 0.0f;

		FMargin OutputPadding = Settings->GetOutputPinPadding();
		OutputPadding.Top = OutputPadding.Bottom = 3.0f;
		OutputPadding.Left = 2.0f;

		SHorizontalBox::FSlot* InnerContentSlotNativePtr = nullptr;

		
		TSharedPtr<SWidget> ExpanderWidget;
		if (InOptionalOwnerRow.IsValid())
		{
			ExpanderWidget =
				SNew(SOptimusEditorExpanderArrow, InOptionalOwnerRow)
					.LeftAligned(bLeftAligned);
		}
		else
		{
			// For pins that are not part of a tree view,
			// add a spacer that is the same size as the expander arrow button
			ExpanderWidget =
				SNew(SSpacer)
					.Size(FVector2d(10.0f, 10.0f));
		}

		TSharedRef<SHorizontalBox> ContentBox = SNew(SHorizontalBox);

		if(bLeftAligned)
		{
			ContentBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(InputPadding)
			[
				SNew(SBox)
				[
					PinContent
				]
			];

			ContentBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				ExpanderWidget.ToSharedRef()
			];

			ContentBox->AddSlot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(2.0f)
			.Expose(InnerContentSlotNativePtr)
			[
				SNew(SBox)
				[
					LabelContent
				]
			];
		}
		else
		{
			ContentBox->AddSlot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(2.0f)
			.Expose(InnerContentSlotNativePtr)
			[
				SNew(SBox)
				[
					LabelContent
				]
			];

			ContentBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				ExpanderWidget.ToSharedRef()
			];

			ContentBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(OutputPadding)
			[
				SNew(SBox)
				[
					PinContent
				]
			];
		}

		this->ChildSlot
		[
			ContentBox
		];

	}
};

class SOptimusEditorGraphPinTreeRow : public STableRow<UOptimusNodePin*>
{
	SLATE_BEGIN_ARGS(SOptimusEditorGraphPinTreeRow) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		STableRow<UOptimusNodePin*>::FArguments TableRowArgs;
		TableRowArgs
		.Content()
		[
			InArgs._Content.Widget
		];
		STableRow<UOptimusNodePin*>::Construct(TableRowArgs, InOwnerTableView);
	}

	const FSlateBrush* GetBorder() const override
	{
		// We want a transparent background.
		return FCoreStyle::Get().GetBrush("NoBrush");
	}


	void ConstructChildren( ETableViewMode::Type InOwnerTableMode, const TAttribute<FMargin>& InPadding, const TSharedRef<SWidget>& InContent ) override
	{
		// ConstructChildren is called from STableRow<UOptimusNodePin*>::Construct(...)
		this->ChildSlot
		[
			InContent
		];
	}
};


static void SetTreeExpansion_Recursive(
	TSharedPtr<STreeView<UOptimusNodePin*>>& InTreeWidget, 
	const TArray<UOptimusNodePin*> &InItems
	)
{
	for (UOptimusNodePin *Pin: InItems)
	{
		if (Pin->GetIsExpanded())
		{
			InTreeWidget->SetItemExpansion(Pin, true);

			SetTreeExpansion_Recursive(InTreeWidget, Pin->GetSubPins());
		}
	}
}

void SOptimusEditorGraphNode::Construct(const FArguments& InArgs)
{
	if (!CachedImg_Pin_Resource_Connected)
	{
		CachedImg_Pin_Resource_Connected = FOptimusEditorStyle::Get().GetBrush(NAME_Pin_Resource_Connected);
		CachedImg_Pin_Resource_Disconnected = FOptimusEditorStyle::Get().GetBrush(NAME_Pin_Resource_Disconnected);
		CachedImg_Pin_Value_Connected = FOptimusEditorStyle::Get().GetBrush(NAME_Pin_Value_Connected);
		CachedImg_Pin_Value_Disconnected = FOptimusEditorStyle::Get().GetBrush(NAME_Pin_Value_Disconnected);
	}

	GraphNode = InArgs._GraphNode;

	UOptimusEditorGraphNode *EditorGraphNode = InArgs._GraphNode;

	SetCursor( EMouseCursor::CardinalCross );
	UpdateGraphNode();

	TreeScrollBar = SNew(SScrollBar);

	LeftNodeBox->AddSlot()
	    .AutoHeight()
		[
			SAssignNew(InputTree, STreeView<UOptimusNodePin*>)
	        .Visibility(this, &SOptimusEditorGraphNode::GetInputTreeVisibility)
	        .TreeViewStyle(&FOptimusEditorStyle::Get().GetWidgetStyle<FTableViewStyle>("Node.PinTreeView"))
	        .TreeItemsSource(EditorGraphNode->GetTopLevelInputPins())
	        .SelectionMode(ESelectionMode::None)
	        .OnGenerateRow(this, &SOptimusEditorGraphNode::MakeTableRowWidget)
	        .OnGetChildren(this, &SOptimusEditorGraphNode::HandleGetChildrenForTree)
	        .OnExpansionChanged(this, &SOptimusEditorGraphNode::HandleExpansionChanged)
	        .ExternalScrollbar(TreeScrollBar)
	        .ItemHeight(20.0f)
		];

	RightNodeBox->AddSlot()
	    .AutoHeight()
		[
			SAssignNew(OutputTree, STreeView<UOptimusNodePin*>)
			.Visibility(this, &SOptimusEditorGraphNode::GetOutputTreeVisibility)
	        .TreeViewStyle(&FOptimusEditorStyle::Get().GetWidgetStyle<FTableViewStyle>("Node.PinTreeView"))
			.TreeItemsSource(EditorGraphNode->GetTopLevelOutputPins())
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SOptimusEditorGraphNode::MakeTableRowWidget)
			.OnGetChildren(this, &SOptimusEditorGraphNode::HandleGetChildrenForTree)
			.OnExpansionChanged(this, &SOptimusEditorGraphNode::HandleExpansionChanged)
			.ExternalScrollbar(TreeScrollBar)
			.ItemHeight(20.0f)
		];

	// Add an extra pin for AdderPinProviders to show an adder pin on both input and output sides
	if (IOptimusNodeAdderPinProvider* AdderPinProvider = Cast<IOptimusNodeAdderPinProvider>(GetModelNode()))
	{
		const TArray<EEdGraphPinDirection> Directions = {EGPD_Input, EGPD_Output};
		
		for (const EEdGraphPinDirection& Direction : Directions)
		{
			UEdGraphPin* GraphPin = GraphNode->FindPin(OptimusEditor::GetAdderPinName(Direction), Direction);
			check(GraphPin);

			TSharedPtr<SGraphPin> PinWidget = GetPinWidget(GraphPin);
			TWeakPtr<SGraphPin> WeakPin = PinWidget;
			check(PinWidget.IsValid());

			TSharedPtr<SVerticalBox> NodeBox = Direction == EGPD_Input ? LeftNodeBox : RightNodeBox;
			EHorizontalAlignment Alignment = Direction == EGPD_Input ? HAlign_Left : HAlign_Right;
			
			NodeBox->AddSlot()
				.MaxHeight(22.0f)
				.HAlign(Alignment)
				[
					SNew(SHorizontalBox)
					.ToolTipText(LOCTEXT("OptimusNodeAdderPin_ToolTip", "Connect to add a new pin"))
					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SOptimusEditorGraphPinWidget, PinWidget.ToSharedRef(), false, nullptr)
						.PinLabel(this, &SOptimusEditorGraphNode::GetPinLabel, WeakPin)
					]
				];
		}
	}
	
	// FIXME: Do expansion from stored expansion data.
	SetTreeExpansion_Recursive(InputTree, *EditorGraphNode->GetTopLevelInputPins());
	SetTreeExpansion_Recursive(OutputTree, *EditorGraphNode->GetTopLevelOutputPins());

	EditorGraphNode->OnNodeTitleDirtied().BindLambda([this]()
	{
		if (NodeTitle.IsValid())
		{
			NodeTitle->MarkDirty();
		}
	});

	EditorGraphNode->OnNodePinsChanged().BindSP(this, &SOptimusEditorGraphNode::SyncPinWidgetsWithGraphPins);
}

EVisibility SOptimusEditorGraphNode::GetTitleVisibility() const
{
	// return UseLowDetailNodeTitles() ? EVisibility::Hidden : EVisibility::Visible;
	return EVisibility::Visible;
}

TSharedRef<SWidget> SOptimusEditorGraphNode::CreateTitleWidget(TSharedPtr<SNodeTitle> InNodeTitle)
{
	NodeTitle = InNodeTitle;

	TSharedRef<SWidget> WidgetRef = SGraphNode::CreateTitleWidget(NodeTitle);
	WidgetRef->SetVisibility(MakeAttributeSP(this, &SOptimusEditorGraphNode::GetTitleVisibility));
	if (NodeTitle.IsValid())
	{
		NodeTitle->SetVisibility(MakeAttributeSP(this, &SOptimusEditorGraphNode::GetTitleVisibility));
	}

	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.Padding(0.0f)
		[
			WidgetRef
		];
}


void SOptimusEditorGraphNode::EndUserInteraction() const
{
	UOptimusEditorGraph* Graph = Cast<UOptimusEditorGraph>(GraphNode->GetGraph());
	if (ensure(Graph))
	{
#if WITH_EDITOR
		// Cancel the current transaction created by SNodePanel::OnMouseMove so that the
		// only transaction recorded is the one we place on the action stack.
		if (GEditor)
		{
			GEditor->CancelTransaction(0);
		}
#endif

		const TSet<UOptimusEditorGraphNode*> &SelectedNodes = Graph->GetSelectedNodes();

		if (SelectedNodes.Num() == 0)
		{
			return;
		}

		FString ActionTitle;
		if (SelectedNodes.Num() == 1)
		{
			ActionTitle = TEXT("Move Node");
		}
		else
		{
			ActionTitle = FString::Printf(TEXT("Move %d Nodes"), SelectedNodes.Num());
		}

		FOptimusActionScope Scope(*Graph->GetModelGraph()->GetActionStack(), ActionTitle);
		for (UOptimusEditorGraphNode* SelectedNode : SelectedNodes)
		{
			FVector2D Position(SelectedNode->NodePosX, SelectedNode->NodePosY);
			SelectedNode->ModelNode->SetGraphPosition(Position);
		}
	}
}


void SOptimusEditorGraphNode::CreateStandardPinWidget(UEdGraphPin* CurPin)
{
	const bool bShowPin = ShouldPinBeHidden(CurPin);

	if (bShowPin)
	{
		// Do we have this pin in our list of pins to keep?
		TSharedRef<SGraphPin>* RecycledPin = PinsToKeep.Find(CurPin);
		TSharedPtr<SGraphPin> NewPin;
		if (!RecycledPin)
		{
			NewPin = CreatePinWidget(CurPin);
			check(NewPin.IsValid());
			
			AddPin(NewPin.ToSharedRef());
		}
		else
		{
			NewPin = *RecycledPin;
		}

		PinWidgetMap.Add(CurPin, NewPin);
		if (CurPin->Direction == EGPD_Input)
		{
			InputPins.Add(NewPin.ToSharedRef());
		}
		else
		{
			OutputPins.Add(NewPin.ToSharedRef());
		}
	}
}


void SOptimusEditorGraphNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	PinToAdd->SetShowLabel(false);

	UOptimusEditorGraphNode* EditorGraphNode = GetEditorGraphNode();
	if (ensure(EditorGraphNode))
	{
		const UEdGraphPin* EdPinObj = PinToAdd->GetPinObj();

		UOptimusNodePin *ModelPin = EditorGraphNode->FindModelPinFromGraphPin(EdPinObj);
		if (ModelPin)
		{
			switch (ModelPin->GetStorageType())
			{
			case EOptimusNodePinStorageType::Resource:
				PinToAdd->SetCustomPinIcon(CachedImg_Pin_Resource_Connected, CachedImg_Pin_Resource_Disconnected);
				break;

			case EOptimusNodePinStorageType::Value:
				PinToAdd->SetCustomPinIcon(CachedImg_Pin_Value_Connected, CachedImg_Pin_Value_Disconnected);
				break;
			}
		}
		else if (OptimusEditor::IsAdderPin(EdPinObj))
		{
			// TODO: Use a adder pin specific icon
			PinToAdd->SetCustomPinIcon(CachedImg_Pin_Value_Connected, CachedImg_Pin_Value_Disconnected);
		}

		// Remove value widget from combined pin content
		TSharedPtr<SWrapBox> LabelAndValueWidget = PinToAdd->GetLabelAndValue();
		TSharedPtr<SHorizontalBox> FullPinHorizontalRowWidget = PinToAdd->GetFullPinHorizontalRowWidget().Pin();
		if (LabelAndValueWidget.IsValid() && FullPinHorizontalRowWidget.IsValid())
		{
			FullPinHorizontalRowWidget->RemoveSlot(LabelAndValueWidget.ToSharedRef());
		}

		PinToAdd->SetOwner(SharedThis(this));
	}
}


TSharedPtr<SGraphPin> SOptimusEditorGraphNode::GetHoveredPin(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
{
	TSharedPtr<SGraphPin> HoveredPin = SGraphNode::GetHoveredPin(MyGeometry, MouseEvent);
#if 0
	if (!HoveredPin.IsValid())
	{
		TArray<TSharedRef<SWidget>> ExtraWidgetArray;
		ExtraWidgetToPinMap.GenerateKeyArray(ExtraWidgetArray);
		TSet<TSharedRef<SWidget>> ExtraWidgets(ExtraWidgetArray);

		TMap<TSharedRef<SWidget>, FArrangedWidget> Result;
		FindChildGeometries(MyGeometry, ExtraWidgets, Result);

		if (Result.Num() > 0)
		{
			FArrangedChildren ArrangedWidgets(EVisibility::Visible);
			Result.GenerateValueArray(ArrangedWidgets.GetInternalArray());
			int32 HoveredWidgetIndex = SWidget::FindChildUnderMouse(ArrangedWidgets, MouseEvent);
			if (HoveredWidgetIndex != INDEX_NONE)
			{
				return *ExtraWidgetToPinMap.Find(ArrangedWidgets[HoveredWidgetIndex].Widget);
			}
		}
	}
#endif
	return HoveredPin;
}


void SOptimusEditorGraphNode::RefreshErrorInfo()
{
	if (GraphNode)
	{
		if (CachedErrorType != GraphNode->ErrorType)
		{
			SGraphNode::RefreshErrorInfo();
			CachedErrorType = GraphNode->ErrorType;
		}
	}
}


void SOptimusEditorGraphNode::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime
	)
{
	SGraphNode::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	
	if (GraphNode)
	{
		// GraphNode->NodeWidth = (int32)AllottedGeometry.Size.X;
		// GraphNode->NodeHeight = (int32)AllottedGeometry.Size.Y;
		RefreshErrorInfo();

		// These will be deleted on the next tick.
		for (UEdGraphPin *PinToDelete: PinsToDelete)
		{
			PinToDelete->MarkAsGarbage();
		}
		PinsToDelete.Reset();
	}
}


UOptimusEditorGraphNode* SOptimusEditorGraphNode::GetEditorGraphNode() const
{
	return Cast<UOptimusEditorGraphNode>(GraphNode);
}


UOptimusNode* SOptimusEditorGraphNode::GetModelNode() const
{
	UOptimusEditorGraphNode* EditorGraphNode = GetEditorGraphNode();
	return EditorGraphNode ? EditorGraphNode->ModelNode : nullptr;
}

TSharedPtr<SGraphPin> SOptimusEditorGraphNode::GetPinWidget(UEdGraphPin* InGraphPin)
{
	TWeakPtr<SGraphPin>* PinWidgetPtr = PinWidgetMap.Find(InGraphPin);
	if (PinWidgetPtr)
	{
		TSharedPtr<SGraphPin> PinWidget = PinWidgetPtr->Pin();
		return PinWidget;
	}

	return nullptr;
}


void SOptimusEditorGraphNode::SyncPinWidgetsWithGraphPins()
{
	// Collect graph pins to delete. We do this here because this widget is the only entity
	// that's aware of the lifetime requirements for the graph pins (SGraphPanel uses Slate 
	// timers to trigger a delete, which makes deleting them from a non-widget setting). 
	TSet<UEdGraphPin *> LocalPinsToDelete;
	for (const TSharedRef<SGraphPin>& GraphPin: InputPins)
	{
		LocalPinsToDelete.Add(GraphPin->GetPinObj());
	}
	for (const TSharedRef<SGraphPin>& GraphPin: OutputPins)
	{
		LocalPinsToDelete.Add(GraphPin->GetPinObj());
	}

	check(PinsToKeep.IsEmpty());

	UOptimusEditorGraphNode* EditorGraphNode = GetEditorGraphNode();
	for (const UEdGraphPin* LivePin: EditorGraphNode->Pins)
	{
		TWeakPtr<SGraphPin>* PinWidgetPtr = PinWidgetMap.Find(LivePin);
		if (PinWidgetPtr)
		{
			TSharedPtr<SGraphPin> PinWidget = PinWidgetPtr->Pin();
			if (PinWidget.IsValid())
			{
				PinsToKeep.Add(LivePin, PinWidget.ToSharedRef());
			}
		}
		LocalPinsToDelete.Remove(LivePin);
	}

	for (const UEdGraphPin* DeletingPin: LocalPinsToDelete)
	{
		TWeakPtr<SGraphPin>* PinWidgetPtr = PinWidgetMap.Find(DeletingPin);
		if (PinWidgetPtr)
		{
			TSharedPtr<SGraphPin> PinWidget = PinWidgetPtr->Pin();
			if (PinWidget.IsValid())
			{
				// Ensure that this pin widget can no longer depend on the soon-to-be-deleted
				// graph pin.
				PinWidget->InvalidateGraphData();
			}
		}
	}
	PinsToDelete.Append(LocalPinsToDelete);

	// Reconstruct the pin widgets. This could be done more surgically but will do for now.
	InputPins.Reset();
	OutputPins.Reset();
	PinWidgetMap.Reset();

	CreatePinWidgets();

	// Nix any pins left in this map. They're most likely hidden sub-pins.
	PinsToKeep.Reset();

	InputTree->RequestTreeRefresh();
	OutputTree->RequestTreeRefresh();
}


EVisibility SOptimusEditorGraphNode::GetInputTreeVisibility() const
{
	const UOptimusEditorGraphNode* EditorGraphNode = GetEditorGraphNode();
	
	return EditorGraphNode && !EditorGraphNode->GetTopLevelInputPins()->IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SOptimusEditorGraphNode::GetOutputTreeVisibility() const
{
	const UOptimusEditorGraphNode* EditorGraphNode = GetEditorGraphNode();

	return EditorGraphNode && !EditorGraphNode->GetTopLevelOutputPins()->IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
}


TSharedRef<ITableRow> SOptimusEditorGraphNode::MakeTableRowWidget(
	UOptimusNodePin* InModelPin, 
	const TSharedRef<STableViewBase>& OwnerTable
	)
{
	const bool bIsValue = (InModelPin->GetStorageType() == EOptimusNodePinStorageType::Value && InModelPin->GetPropertyFromPin() != nullptr);
	
	const UOptimusEditorGraphNode* EditorGraphNode = GetEditorGraphNode();
	TSharedPtr<SGraphPin> PinWidget;
	TWeakPtr<SGraphPin> WeakPin;
	if (ensure(EditorGraphNode))
	{
		UEdGraphPin* GraphPin = EditorGraphNode->FindGraphPinFromModelPin(InModelPin);
		PinWidget = GetPinWidget(GraphPin);
		check(PinWidget);
		WeakPin = PinWidget;
	}

	TSharedPtr<SOptimusEditorGraphPinTreeRow> RowWidget;

	SAssignNew(RowWidget, SOptimusEditorGraphPinTreeRow, OwnerTable)
	.ToolTipText_UObject(InModelPin, &UOptimusNodePin::GetTooltipText)
	.Content()[
		SNew(SOptimusEditorGraphPinWidget, PinWidget.ToSharedRef(), bIsValue, RowWidget)
		.PinLabel(this, &SOptimusEditorGraphNode::GetPinLabel, WeakPin)
	];
	
	return RowWidget.ToSharedRef();
}


void SOptimusEditorGraphNode::HandleGetChildrenForTree(
	UOptimusNodePin* InItem, 
	TArray<UOptimusNodePin*>& OutChildren
	)
{
	OutChildren.Append(InItem->GetSubPins());
}


void SOptimusEditorGraphNode::HandleExpansionChanged(
	UOptimusNodePin* InItem, 
	bool bExpanded
	)
{
	InItem->SetIsExpanded(bExpanded);
}


FText SOptimusEditorGraphNode::GetPinLabel(TWeakPtr<SGraphPin> InWeakGraphPin) const
{
	UOptimusEditorGraphNode* EditorGraphNode = GetEditorGraphNode();
	TSharedPtr<SGraphPin> GraphPin = InWeakGraphPin.Pin();

	if (GraphPin.IsValid() && EditorGraphNode)
	{
		return EditorGraphNode->GetPinDisplayName(GraphPin->GetPinObj());
	}
	return FText::GetEmpty();
}


#undef LOCTEXT_NAMESPACE