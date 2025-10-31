// Copyright Epic Games, Inc. All Rights Reserved.
	

#include "AIAssistantSlateQuerier.h"

#include "EditorModes.h"
#include "Internationalization/Regex.h"
#include "LevelEditorSubsystem.h"
#include "EditorModeManager.h"
#include "IDetailsView.h"
#include "Interfaces/IMainFrameModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/BaseToolkit.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Docking/SDockTab.h"
#include "SGraphNode.h"
#include "Framework/Application/SlateApplication.h"
#include "Core/AIAssistantLog.h"
#include "Core/AIAssistantSubsystem.h"
#include "AIAssistantWebBrowser.h"


#define LOCTEXT_NAMESPACE "FAIAssistantSlateQuerier"


//
// FAIAssistantSlateQueryContext
//
// Used for accumulating UI context information, and ultimately building strings for the query.
//


struct FAIAssistantSlateQueryContext
{
	TSharedPtr<SWidget> LastPickedWidget;
	TArray<FText> GeneratedContextItems;
	FText CurrentToolTipText = FText();
	bool bIsUIWidget = false;
	bool bIsObject = false;
	bool bInOutliner = false;

	FText GeneratedQuery = FText();
	FText GeneratedContext = FText();
	FText GeneratedQueryInstructions = FText();
};


//
// Statics
//


static TSharedRef<SWidget> FindClosestWidgetOfType(const FWidgetPath& WidgetPathToTest, const FName& WidgetType)
{
	for (int32 WidgetIndex = WidgetPathToTest.Widgets.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
	{
		const FArrangedWidget* ArrangedWidget = &WidgetPathToTest.Widgets[WidgetIndex];
		const TSharedRef<SWidget>& ThisWidget = ArrangedWidget->Widget;
		if (ThisWidget->GetType() == WidgetType)
		{
			return ThisWidget;
		}
	}
	
	return SNullWidget::NullWidget;
}


static TSharedRef<SWidget> FindFirstWidgetOfType(const FWidgetPath& WidgetPathToTest, const FName& WidgetType)
{
	for (int32 WidgetIndex = 0; WidgetIndex < WidgetPathToTest.Widgets.Num(); WidgetIndex++)
	{
		const FArrangedWidget* ArrangedWidget = &WidgetPathToTest.Widgets[WidgetIndex];
		const TSharedRef<SWidget>& ThisWidget = ArrangedWidget->Widget;
		if (ThisWidget->GetType() == WidgetType)
		{
			return ThisWidget;
		}
	}
	
	return SNullWidget::NullWidget;
}


static void FindChildWidgetsOfType(TArray<TSharedRef<SWidget>>& OutWidgets, TSharedPtr<SWidget> WidgetToTest, const FName& WidgetType)
{
	if (WidgetToTest.IsValid())
	{
		for (int32 ChildIdx = 0; ChildIdx < WidgetToTest->GetChildren()->Num(); ChildIdx++)
		{
			TSharedRef<SWidget> ThisWidget = WidgetToTest->GetChildren()->GetChildAt(ChildIdx);
			if (ThisWidget->GetType() == WidgetType)
			{
				OutWidgets.Add(ThisWidget);
			}
			FindChildWidgetsOfType(OutWidgets, ThisWidget.ToSharedPtr(), WidgetType);
		}
	}
}


static TSharedRef<SWidget> FindChildWidgetOfType(const TSharedPtr<SWidget> WidgetToTest, const FName& WidgetType)
{
	if (WidgetToTest.IsValid())
	{
		for (int32 ChildIdx = 0; ChildIdx < WidgetToTest->GetChildren()->Num(); ChildIdx++)
		{
			TSharedRef<SWidget> ThisWidget = WidgetToTest->GetChildren()->GetChildAt(ChildIdx);
			if (ThisWidget->GetType() == WidgetType)
			{
				return ThisWidget;
			}
			else
			{
				TSharedRef<SWidget> ChildWidget = FindChildWidgetOfType(ThisWidget.ToSharedPtr(), WidgetType);
				if (ChildWidget->GetType() == WidgetType)
				{
					return ChildWidget;
				}
			}
		}
	}
	
	return SNullWidget::NullWidget;
}


static FText FindChildWidgetWithText(const TSharedPtr<SWidget> WidgetToTest)
{
	// get all children and see if any are text widgets.
	// refuse any that only contain numbers (they're probably just values)
	const FRegexPattern AlphaPattern(TEXT("[A-Za-z]+"));
	if (WidgetToTest.IsValid())
	{
		if (WidgetToTest->GetType() == "STextBlock")
		{
			TSharedRef<STextBlock> TextBlock = StaticCastSharedRef<STextBlock>(WidgetToTest.ToSharedRef());
			const FText WidgetText = TextBlock->GetText();
			if (!WidgetText.IsEmpty())
			{
				FRegexMatcher AlphaMatcher(AlphaPattern, WidgetText.ToString());
				if (AlphaMatcher.FindNext())
				{
					return WidgetText;
				}
			}
		}
		for (int32 ChildIdx = 0; ChildIdx < WidgetToTest->GetChildren()->Num(); ChildIdx++)
		{
			TSharedRef<SWidget> ThisWidget = WidgetToTest->GetChildren()->GetChildAt(ChildIdx);
			const FText ChildText = FindChildWidgetWithText(ThisWidget.ToSharedPtr());
			if (!ChildText.IsEmpty())
			{
				return ChildText;
			}
		}
	}
	
	return FText();
}


static FText FindCurrentToolTipText()
{
	for (const TArray<TSharedRef<SWindow>> AllWindows = FSlateApplication::Get().GetTopLevelWindows();
		const TSharedRef<SWindow>& ThisWindow : AllWindows)
	{
		if (ThisWindow->GetType() == EWindowType::ToolTip && ThisWindow->IsVisible())
		{
			return FindChildWidgetWithText(ThisWindow);
		}
	}
	
	return FText();
}


static TSharedRef<SWidget> FindClosestMenuItem(const FWidgetPath& WidgetPath)
{
	if (WidgetPath.IsValid())
	{
		// is it a menu item? Look up for an SMenuEntryBlock or SWidgetBlock.
		TSharedRef<SWidget> MenuItemBlock = FindClosestWidgetOfType(WidgetPath, "SMenuEntryBlock");
		if (MenuItemBlock->GetType() == "SNullWidgetContent")
		{
			MenuItemBlock = FindClosestWidgetOfType(WidgetPath, "SWidgetBlock");
		}
		// disabled menu items *contain* the menu entry block
		if (MenuItemBlock->GetType() == "SNullWidgetContent")
		{
			if (WidgetPath.GetLastWidget()->GetChildren()->Num() > 0)
			{
				for (int32 ChildIdx = 0; ChildIdx < WidgetPath.GetLastWidget()->GetChildren()->Num(); ChildIdx++)
				{
					TSharedRef<SWidget> ThisWidget = WidgetPath.GetLastWidget()->GetChildren()->GetChildAt(ChildIdx);
					if (ThisWidget->GetType() == "SMenuEntryBlock" || ThisWidget->GetType() == "SWidgetBlock")
					{
						MenuItemBlock = ThisWidget;
					}
				}
			}
		}
		if (MenuItemBlock->GetType() != "SNullWidgetContent")
		{
			return MenuItemBlock;
		}
	}

	return SNullWidget::NullWidget;
}


static FWidgetPath FindContextWidgetPath(const FWidgetPath& WidgetPath)
{
	if (const TSharedRef<SWidget> MenuItem = FindClosestMenuItem(WidgetPath);
		MenuItem->GetType() != "SNullWidgetContent")
	{
		TSharedPtr<SWidget> RootWidget = FSlateApplication::Get().GetMenuHostWidget();
		if (RootWidget.IsValid())
		{
			FWidgetPath OutWidgetPath;
			FSlateApplication::Get().GeneratePathToWidgetChecked(RootWidget.ToSharedRef(), OutWidgetPath);
			return OutWidgetPath;
		}
	}
	return WidgetPath;
}


static FText GenerateModeToolContext(const FWidgetPath& WidgetPath)
{
	FText OutContext = FText();

	// if we're in a level editor mode, include which tool is active
	TSharedRef<SWidget> LevelEditor = FindClosestWidgetOfType(WidgetPath, "SLevelEditor");
	if (LevelEditor->GetType() != "SNullWidgetContent")
	{
		FText ToolString;
		ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
		if (FEditorModeTools* ModeManager = LevelEditorSubsystem->GetLevelEditorModeManager())
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			TArray<FEditorModeInfo> ModeInfos = AssetEditorSubsystem->GetEditorModeInfoOrderedByPriority();
			for (const FEditorModeInfo& ModeInfo : ModeInfos)
			{
				FString ThisModeID = ModeInfo.ID.ToString();
				if (UEdMode* EdMode = ModeManager->GetActiveScriptableMode(ModeInfo.ID))
				{
					TWeakPtr<FModeToolkit> ModeToolkit = EdMode->GetToolkit();
					if (ModeToolkit.IsValid())
					{
						FText ToolDisplayName = ModeToolkit.Pin()->GetActiveToolDisplayName();
						if (!ToolDisplayName.IsEmpty())
						{
							FFormatNamedArguments Args;
							Args.Add(TEXT("ToolName"), ToolDisplayName);
							Args.Add(TEXT("ModeName"), ModeInfo.Name);
							return FText::Format(LOCTEXT("EditorName_ModeToolFormat", "The {ToolName} tool is active for {ModeName} mode."), Args);
						}
					}
				}
			}
		}
	}

	return OutContext;
}


static FText FindItemName(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	FText OutName = FText();
	FText OutDescriptor = LOCTEXT("ItemDescriptor_Generic", "control");
	TSet<FName> ButtonTypes = TSet<FName>({
		"SButton",
		"SPrimaryButton",
		"SCheckbox"
		});

	FText ChildText = FText();

	if (!WidgetPath.IsValid())
	{
		return OutName;
	}

	// Is it a graph node?
	// Look up for an SGraphEditor and then down for an SGraphPanel. Whatever is the child of that SGraphPanel is our SGraphNode.
	// Note that this rests on the assumption that SGraphPanel only contains children that are SGraphNodes. This assumption is also
	// made in the code of SGraphPanel itself.
	TSharedPtr<SGraphEditor> GraphEditor;
	for (int32 WidgetIndex = WidgetPath.Widgets.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
	{
		const FArrangedWidget* ArrangedWidget = &WidgetPath.Widgets[WidgetIndex];
		const TSharedRef<SWidget>& ThisWidget = ArrangedWidget->Widget;
		if (ThisWidget->GetType() == "SGraphEditor")
		{
			for (int32 EditorDescendantIndex = WidgetIndex; EditorDescendantIndex < WidgetPath.Widgets.Num(); EditorDescendantIndex++)
			{
				const FArrangedWidget* DescendantArrangedWidget = &WidgetPath.Widgets[EditorDescendantIndex];
				const TSharedRef<SWidget>& ThisDescendantWidget = DescendantArrangedWidget->Widget;
				if (ThisDescendantWidget->GetType() == "SGraphPanel" && (EditorDescendantIndex + 1 < WidgetPath.Widgets.Num()))
				{
					const FArrangedWidget* NodeArrangedWidget = &WidgetPath.Widgets[EditorDescendantIndex+1];
					const TSharedRef<SWidget>& ThisNodeWidget = NodeArrangedWidget->Widget;
					if (ThisNodeWidget->GetType() == "SNiagaraOverviewStackNode")
					{
						// Niagara emitters don't typically have useful node names
						continue;
					}
					const TSharedPtr<SGraphNode> AsGraphNode = StaticCastSharedPtr<SGraphNode>(ThisNodeWidget.ToSharedPtr());
					OutName = AsGraphNode->GetNodeObj()->GetNodeTitle(ENodeTitleType::MenuTitle);
					OutDescriptor = LOCTEXT("ItemDescriptor_GraphNode", "graph node");
					SlateQueryContext.LastPickedWidget = ThisNodeWidget.ToSharedPtr();
				}
			}
		}
	}


	// Is it a button? Look up for an SButton and then down for the text.
	TSharedPtr<SWidget> Button;
	for (int32 WidgetIndex = WidgetPath.Widgets.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
	{
		const FArrangedWidget* ArrangedWidget = &WidgetPath.Widgets[WidgetIndex];
		const TSharedRef<SWidget>& ThisWidget = ArrangedWidget->Widget;
		if (ButtonTypes.Contains(ThisWidget->GetType()))
		{
			Button = ThisWidget.ToSharedPtr();
		}
	}
	// Disabled buttons *contain* the menu entry block.
	if (!Button.IsValid() && WidgetPath.GetLastWidget()->GetChildren()->Num() > 0)
	{
		for (int32 ChildIdx = 0; ChildIdx < WidgetPath.GetLastWidget()->GetChildren()->Num(); ChildIdx++)
		{
			TSharedRef<SWidget> ThisWidget = WidgetPath.GetLastWidget()->GetChildren()->GetChildAt(ChildIdx);
			if (ButtonTypes.Contains(ThisWidget->GetType()))
			{
				Button = ThisWidget.ToSharedPtr();
			}
		}
	}
	if (Button.IsValid())
	{
		ChildText = FindChildWidgetWithText(Button);
		if (!ChildText.IsEmpty() || !SlateQueryContext.CurrentToolTipText.IsEmpty())
		{
			if (!ChildText.IsEmpty())
			{
				OutName = ChildText;
			}
			else
			{
				// The tool tip can be used in this case.
				OutName = SlateQueryContext.CurrentToolTipText;
			}
			OutDescriptor = LOCTEXT("ItemDescriptor_Button", "button");
			SlateQueryContext.LastPickedWidget = Button;
			SlateQueryContext.bIsUIWidget = true;
		}
	}


	// Is it a toolbar button? Look for an SToolbarButtonBlock.
	TSharedRef<SWidget> ToolbarItemBlock = FindClosestWidgetOfType(WidgetPath, "SToolBarButtonBlock");
	if (ToolbarItemBlock->GetType() == "SNullWidgetContent")
	{
		ToolbarItemBlock = FindClosestWidgetOfType(WidgetPath, "SToolBarComboButtonBlock");
	}
	// Disabled items *contain* the entry block.
	if (ToolbarItemBlock->GetType() == "SNullWidgetContent")
	{
		if (WidgetPath.GetLastWidget()->GetChildren()->Num() > 0)
		{
			for (int32 ChildIdx = 0; ChildIdx < WidgetPath.GetLastWidget()->GetChildren()->Num(); ChildIdx++)
			{
				TSharedRef<SWidget> ThisWidget = WidgetPath.GetLastWidget()->GetChildren()->GetChildAt(ChildIdx);
				if (ThisWidget->GetType() == "SToolBarButtonBlock" || ThisWidget->GetType() == "SToolBarComboButtonBlock")
				{
					ToolbarItemBlock = ThisWidget;
				}
			}
		}
	}
	if (ToolbarItemBlock->GetType() != "SNullWidgetContent")
	{
		ChildText = FindChildWidgetWithText(ToolbarItemBlock);
		if (!ChildText.IsEmpty() || !SlateQueryContext.CurrentToolTipText.IsEmpty())
		{
			if (!ChildText.IsEmpty())
			{
				OutName = ChildText;
			}
			else
			{
				// The tool tip can be used in this case.
				OutName = SlateQueryContext.CurrentToolTipText;
			}
			OutDescriptor = LOCTEXT("ItemDescriptor_ToolbarButton", "toolbar button");
			SlateQueryContext.LastPickedWidget = ToolbarItemBlock.ToSharedPtr();
			SlateQueryContext.bIsUIWidget = true;
		}
	}


	// Is it a search/filter field?
	FText HintText;
	TSharedRef<SWidget> SearchBox = FindClosestWidgetOfType(WidgetPath, "SSearchBox");
	if (SearchBox->GetType() != "SNullWidgetContent")
	{
		TSharedRef<SSearchBox> ThisSearchBoxCast = StaticCastSharedRef<SSearchBox>(SearchBox);
		HintText = ThisSearchBoxCast->GetHintText();
	}
	else
	{
		SearchBox = FindClosestWidgetOfType(WidgetPath, "SFilterSearchBox");
		if (SearchBox->GetType() != "SNullWidgetContent")
		{
			TSharedRef<SWidget> EditableText = FindChildWidgetOfType(SearchBox, "SEditableText");
			if (EditableText->GetType() != "SNullWidgetContent")
			{
				TSharedRef<SEditableText> ThisEditableTextCast = StaticCastSharedRef<SEditableText>(EditableText);
				HintText = ThisEditableTextCast->GetHintText();
			}
		}
	}
	if (SearchBox->GetType() != "SNullWidgetContent")
	{
		OutName = FText::Format(LOCTEXT("SearchBoxFormat", "{0}"), (HintText.IsEmpty()) ? LOCTEXT("SearchBoxDefaultName", "default") : HintText);
		OutDescriptor = LOCTEXT("ItemDescriptor_SearchBox", "search box");
		SlateQueryContext.LastPickedWidget = SearchBox.ToSharedPtr();
		SlateQueryContext.bIsUIWidget = true;
	}

	// Is it a console input box?
	TSharedRef<SWidget> ConsoleInputBox = FindClosestWidgetOfType(WidgetPath, "SConsoleInputBox");
	if (ConsoleInputBox->GetType() != "SNullWidgetContent")
	{
		TSharedRef<SWidget> EditableText = FindChildWidgetOfType(ConsoleInputBox, "SMultiLineEditableTextBox");
		if (EditableText->GetType() != "SNullWidgetContent")
		{
			TSharedRef<SMultiLineEditableTextBox> ThisMultiLineEditableTextBoxCast = StaticCastSharedRef<SMultiLineEditableTextBox>(EditableText);
			HintText = ThisMultiLineEditableTextBoxCast->GetHintText();
		}
		OutName = FText::Format(LOCTEXT("ConsoleInputBoxFormat", "{0}"), (HintText.IsEmpty() ? LOCTEXT("ConsoleInputBoxDefaultName","Console") : HintText));
		OutDescriptor = LOCTEXT("ItemDescriptor_ConsoleInputBox", "input box");
		SlateQueryContext.LastPickedWidget = ConsoleInputBox.ToSharedPtr();
		SlateQueryContext.bIsUIWidget = true;
	}

	// Is it a menu item? Look up for an SMenuEntryBlock or SWidgetBlock and then down for the text.
	TSharedRef<SWidget> MenuItemBlock = FindClosestMenuItem(WidgetPath);
	if (MenuItemBlock->GetType() != "SNullWidgetContent")
	{
		ChildText = FindChildWidgetWithText(MenuItemBlock);
		if (!ChildText.IsEmpty())
		{
			OutName = ChildText;
			OutDescriptor = LOCTEXT("ItemDescriptor_Menu", "menu item");
			SlateQueryContext.LastPickedWidget = MenuItemBlock.ToSharedPtr();
			SlateQueryContext.bIsUIWidget = true;
		}
	}

	// Is it a details panel property? Look up for an SDetailSingleItemRow [could be others?] then down through the SPropertyNameWidget.
	TSharedRef<SWidget> PropertyRow = FindClosestWidgetOfType(WidgetPath, "SDetailSingleItemRow");
	if (PropertyRow->GetType() != "SNullWidgetContent")
	{
		TSharedRef<SWidget> PropertyNameWidget = FindChildWidgetOfType(PropertyRow, "SPropertyNameWidget");
		if (PropertyNameWidget->GetType() != "SNullWidgetContent")
		{
			ChildText = FindChildWidgetWithText(PropertyNameWidget);
			if (!ChildText.IsEmpty())
			{
				OutName = ChildText;
				OutDescriptor = LOCTEXT("ItemDescriptor_Property", "setting");
				SlateQueryContext.LastPickedWidget = PropertyRow.ToSharedPtr();
			}
		}
	}

	// Is it a breadcrumb trail button?
	TSharedRef<SWidget> BreadcrumbTrail = FindClosestWidgetOfType(WidgetPath, "SBreadcrumbTrail<FNavigationCrumb>");
	if (BreadcrumbTrail->GetType() != "SNullWidgetContent")
	{
		TSharedRef<SWidget> BreadcrumbButton = FindClosestWidgetOfType(WidgetPath, "SButton");
		ChildText = FindChildWidgetWithText(BreadcrumbButton);
		if (!ChildText.IsEmpty())
		{
			OutName = ChildText;
			OutDescriptor = LOCTEXT("ItemDescriptor_Breadcrumb", "navigation breadcrumb");
			SlateQueryContext.LastPickedWidget = BreadcrumbButton.ToSharedPtr();
			SlateQueryContext.bIsUIWidget = true;
		}
	}

	// Is it an asset tile item?
	TSharedRef<SWidget> AssetTileItem = FindClosestWidgetOfType(WidgetPath, "SAssetTileItem");
	if (AssetTileItem->GetType() != "SNullWidgetContent")
	{
		TSharedRef<SWidget> AssetThumbnail = FindClosestWidgetOfType(WidgetPath, "SAssetThumbnail");
		if (AssetThumbnail->GetType() != "SNullWidgetContent")
		{
			ChildText = FindChildWidgetWithText(AssetThumbnail); // This returns TYPE of asset instead of name
			if (!ChildText.IsEmpty())
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("AssetType"), ChildText);
				OutDescriptor = FText::Format(LOCTEXT("ItemDescriptor_Asset", "{AssetType} asset"), Args);
			}

			if (!SlateQueryContext.CurrentToolTipText.IsEmpty())
			{
				// We can use the tooltip for the asset's name.
				OutName = SlateQueryContext.CurrentToolTipText;
			}
			SlateQueryContext.LastPickedWidget = AssetThumbnail.ToSharedPtr();
			SlateQueryContext.bIsObject = true;
		}
		else
		{
			ChildText = FindChildWidgetWithText(AssetTileItem);
			if (!ChildText.IsEmpty())
			{
				OutName = ChildText;
				OutDescriptor = LOCTEXT("ItemDescriptor_Folder", "asset folder");
				SlateQueryContext.LastPickedWidget = AssetTileItem.ToSharedPtr();
				SlateQueryContext.bIsObject = true;
			}
		}
	}

	// If in Outliner, treat unnamed widgets as actor instances.
	TSharedRef<SWidget> Outliner = FindClosestWidgetOfType(WidgetPath, "SSceneOutliner");
	if (Outliner->GetType() != "SNullWidgetContent")
	{
		SlateQueryContext.bInOutliner = true;
	}
	// If in SubobjectInstanceEditor, treat unnamed widgets as actor instances.
	TSharedRef<SWidget> SubobjectInstanceEditor = FindClosestWidgetOfType(WidgetPath, "SSubobjectInstanceEditor");
	if (SubobjectInstanceEditor->GetType() != "SNullWidgetContent")
	{
		SlateQueryContext.bInOutliner = true;
	}

	if (!OutName.IsEmpty())
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ItemName"), OutName);
		Args.Add(TEXT("ItemDescriptor"), OutDescriptor);
		return FText::Format(LOCTEXT("ItemName_Format", " the \"{ItemName}\" {ItemDescriptor}"), Args);
	}

	return OutName;
}


static FText FindTabName(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	FText OutName = FText();

	TSharedRef<SWidget> TabStack = FindClosestWidgetOfType(WidgetPath, "SDockingTabStack");
	if (TabStack->GetType() != "SNullWidgetContent")
	{
		TArray<TSharedRef<SWidget>> DockTabs;
		FindChildWidgetsOfType(DockTabs, TabStack.ToSharedPtr(), "SDockTab");
		for (auto& ThisTab : DockTabs)
		{
			TSharedRef<SDockTab> ThisTabCast = StaticCastSharedRef<SDockTab>(ThisTab);
			if (ThisTabCast->GetTabRole() == ETabRole::MajorTab || ThisTabCast->GetTabRole() == ETabRole::DocumentTab)
			{
				return OutName;
			}
			if (ThisTabCast->IsForeground())
			{
				SlateQueryContext.LastPickedWidget = TabStack.ToSharedPtr();
				OutName = ThisTabCast->GetTabLabel();
			}
		}
	}

	if (!OutName.IsEmpty())
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("PanelName"), OutName);
		return FText::Format(LOCTEXT("TabName_Format", " the {PanelName} panel"), Args);
	}
	
	return OutName;
}


static FText GenerateDetailsViewContext(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	FText OutContext = FText();

	// if we're in a details panel, include the class of actor or object that we're editing
	const TSharedRef<SWidget> DetailsView = FindClosestWidgetOfType(WidgetPath, "SDetailsView");
	if (DetailsView->GetType() != "SNullWidgetContent")
	{
		const TSharedRef<IDetailsView> DetailsViewCast = StaticCastSharedRef<IDetailsView>(DetailsView);
		FText QueryContextDetailsObjectAddition =
			LOCTEXT("QueryContextDetailsObjectAddition",
					"The property panel I'm working in is editing at least one actor of the class \"{ClassName}\".");
		FFormatNamedArguments ClassNameArgs;
		if (DetailsViewCast->GetSelectedActors().Num() > 0)
		{
			FString ActorName;
			DetailsViewCast->GetSelectedActors()[0]->GetClass()->GetName(ActorName);
			ClassNameArgs.Add(TEXT("ClassName"), FText::FromString(ActorName));
			SlateQueryContext.GeneratedContextItems.Add(FText::Format(QueryContextDetailsObjectAddition, ClassNameArgs));
		}
		else if (DetailsViewCast->GetSelectedObjects().Num() > 0)
		{
			FString ObjectName;
			DetailsViewCast->GetSelectedObjects()[0]->GetClass()->GetName(ObjectName);
			ClassNameArgs.Add(TEXT("ClassName"), FText::FromString(ObjectName));
			SlateQueryContext.GeneratedContextItems.Add(FText::Format(QueryContextDetailsObjectAddition, ClassNameArgs));
		}
	}

	return OutContext;
}


static FText FindEditorName(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	FText OutName = FText();

	if (!WidgetPath.IsValid())
	{
		return OutName;
	}

	// is it in the status bar?
	TSharedRef<SWidget> StatusBar = FindClosestWidgetOfType(WidgetPath, "SStatusBar");
	if (StatusBar->GetType() != "SNullWidgetContent")
	{
		SlateQueryContext.LastPickedWidget = StatusBar.ToSharedPtr();
		return LOCTEXT("TabName_StatusBar", " the Status Bar");
	}

	TSet<FEditorModeID> BlockedModeIDs = TSet<FEditorModeID>({
		FBuiltinEditorModes::EM_Default,
		"EditMode.SubTrackEditMode"
		});

	TSharedRef<SWidget> DockingArea = FindFirstWidgetOfType(WidgetPath, "SDockingArea");
	if (DockingArea->GetType() != "SNullWidgetContent")
	{
		TSharedRef<SWidget> TabStack = FindChildWidgetOfType(DockingArea.ToSharedPtr(), "SDockingTabStack");
		if (TabStack->GetType() != "SNullWidgetContent")
		{
			// are we in the level editor?
			TSharedRef<SWidget> LevelEditor = FindChildWidgetOfType(TabStack, "SLevelEditor");
			if (LevelEditor->GetType() != "SNullWidgetContent")
			{
				FText ModeString;
				ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
				if (FEditorModeTools* ModeManager = LevelEditorSubsystem->GetLevelEditorModeManager())
				{
					UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
					TArray<FEditorModeInfo> ModeInfos = AssetEditorSubsystem->GetEditorModeInfoOrderedByPriority();
					for (const FEditorModeInfo& ModeInfo : ModeInfos)
					{
						if (ModeString.IsEmpty() && ModeManager->IsModeActive(ModeInfo.ID) && !BlockedModeIDs.Contains(ModeInfo.ID))
						{
							FFormatNamedArguments Args;
							Args.Add(TEXT("ModeName"), ModeInfo.Name);
							ModeString = FText::Format(LOCTEXT("EditorName_ModeFormat", "with {ModeName} mode active"), Args);
						}
					}
				}
				SlateQueryContext.LastPickedWidget = LevelEditor.ToSharedPtr();
				if (ModeString.IsEmpty())
				{
					return LOCTEXT("EditorName_LevelEditor", " the Level Editor");
				}
				else
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("WithModeActive"), ModeString);
					return FText::Format(LOCTEXT("EditorName_LevelEditorAndMode", " the Level Editor {WithModeActive}"), Args);
				}
			}

			// if not, are we in a different asset editor?
			TArray<TSharedRef<SWidget>> DockTabs;
			FindChildWidgetsOfType(DockTabs, TabStack.ToSharedPtr(), "SDockTab");
			for (auto& ThisTab : DockTabs)
			{
				TSharedRef<SDockTab> ThisTabCast = StaticCastSharedRef<SDockTab>(ThisTab);
				if (ThisTabCast->IsForeground())
				{
					TSharedPtr<FTabManager> PickedWidgetTabManager = ThisTabCast->GetTabManagerPtr();

					if (PickedWidgetTabManager.IsValid())
					{
						UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
						TArray<UObject*> AllEditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
						for (auto& ThisEditedAsset : AllEditedAssets)
						{
							IAssetEditorInstance* ThisEditor = AssetEditorSubsystem->FindEditorForAsset(ThisEditedAsset, false);
							if (ThisEditor->GetAssociatedTabManager() == PickedWidgetTabManager)
							{
								SlateQueryContext.LastPickedWidget = TabStack.ToSharedPtr();
								FFormatNamedArguments Args;
								Args.Add(TEXT("AssetEditorName"), FText::FromName(ThisEditor->GetEditorName()));
								return FText::Format(LOCTEXT("EditorName_AssetEditorFormat", " the {AssetEditorName} editor"), Args);
							}
						}
					}
				}
			}
		}
	}

	// Is it in a drawer overlay?
	TSharedRef<SWidget> DrawerOverlay = FindFirstWidgetOfType(WidgetPath, "SDrawerOverlay");
	if (DrawerOverlay->GetType() != "SNullWidgetContent")
	{
		FText DrawerName;

		TSharedRef<SWidget> DrawerWidget = FindChildWidgetOfType(DrawerOverlay, "SContentBrowser");
		if (DrawerWidget->GetType() != "SNullWidgetContent")
		{
			DrawerName = LOCTEXT("DrawerName_ContentBrowser", "ContentBrowser");
		}
		else
		{
			DrawerWidget = FindChildWidgetOfType(DrawerOverlay, "SOutputLog");
			if (DrawerWidget->GetType() != "SNullWidgetContent")
			{
				DrawerName = LOCTEXT("DrawerName_OutputLog", "OutputLog");
			}
		}
		// Are there other items that are commonly in drawers?

		if (!DrawerName.IsEmpty())
		{
			SlateQueryContext.LastPickedWidget = DrawerWidget.ToSharedPtr();
			FFormatNamedArguments Args;
			Args.Add(TEXT("DrawerName"), DrawerName);
			return FText::Format(LOCTEXT("DrawerNameFormat", "the {DrawerName} drawer"), Args);
		}
	}

	TSharedRef<SWindow> WidgetWindow = WidgetPath.GetWindow();
	// is it the main window that hosts the level editor?
	if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		if (WidgetWindow.ToSharedPtr() == MainFrameModule.GetParentWindow())
		{
			return LOCTEXT("WindowMainFrame_Title", " the editor's main window");
		}
	}
	
	// does the window have a title, like the Project Browser or Color Picker?
	FText WindowTitle = WidgetWindow->GetTitle();
	if (!WindowTitle.IsEmpty())
	{
		SlateQueryContext.LastPickedWidget = WidgetWindow.ToSharedPtr();
		FFormatNamedArguments Args;
		Args.Add(TEXT("WindowTitle"), WindowTitle);
		return FText::Format(LOCTEXT("WindowNameFormat_Title", " the {WindowTitle} window"), Args);
	}

	// TODO: how to find other editor contexts?
	return OutName;
}


static FText FindTextUnderCursor(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& AccumulatedSlateQueryContext)
{
	FText OutText;

	if (WidgetPath.IsValid())
	{
		const FRegexPattern AlphaPattern(TEXT("[A-Za-z]+"));
		TSharedRef<SWidget> WidgetToTest = WidgetPath.GetLastWidget();
		FText WidgetText;
		if (WidgetToTest->GetType() == "STextBlock")
		{
			TSharedRef<STextBlock> TextBlock = StaticCastSharedRef<STextBlock>(WidgetToTest);
			WidgetText = TextBlock->GetText();
		}
		else if (WidgetToTest->GetType() == "SRichTextBlock")
		{
			TSharedRef<SRichTextBlock> TextBlock = StaticCastSharedRef<SRichTextBlock>(WidgetToTest);
			WidgetText = TextBlock->GetText();
		}
		if (!WidgetText.IsEmpty())
		{
			FRegexMatcher AlphaMatcher(AlphaPattern, WidgetText.ToString());
			if (AlphaMatcher.FindNext())
			{
				OutText = WidgetText;
			}
		}

		if (!OutText.IsEmpty())
		{
			AccumulatedSlateQueryContext.LastPickedWidget = WidgetToTest;
		}
	}

	return OutText;
}


static FString CleanExtraWhiteSpaceFromString(const FString& InString)
{
	FString CleanString = "";
	static const FRegexPattern WhiteSpacePattern(TEXT("(\\S+)"));
	FRegexMatcher Matcher(WhiteSpacePattern, InString);
	while (Matcher.FindNext())
	{
		if (!CleanString.IsEmpty())
		{
			CleanString += " ";
		}
		
		CleanString += Matcher.GetCaptureGroup(1);
	}

	return MoveTemp(CleanString);
}


//
// UE::AIAssistant::SlateQuerier
//


void UE::AIAssistant::SlateQuerier::QueryAIAssistantAboutSlateWidgetUnderCursor()
{
	// Get the path to the widget under the cursor.

	FWidgetPath WidgetPath;
	{
		const FVector2f CursorPos = FSlateApplication::Get().GetCursorPos();
		const TArray<TSharedRef<SWindow>>& Windows = FSlateApplication::Get().GetTopLevelWindows();
		WidgetPath = FSlateApplication::Get().LocateWindowUnderMouse(CursorPos, Windows, true);
	}


	// We build this up, below.
	
	FAIAssistantSlateQueryContext SlateQueryContext;

	// Is there a current tool-tip?
	SlateQueryContext.CurrentToolTipText = FindCurrentToolTipText();

	// For menu items, we want to generate the context based on the root widget that spawned the menu,
	// not on the actual menu item itself.
	const FWidgetPath ContextWidgetPath = FindContextWidgetPath(WidgetPath);

	// Is there an identifiable asset editor, mode, or window?
	const FText WindowName = FindEditorName(ContextWidgetPath, SlateQueryContext);

	// Is there an identifiable tab?
	// Look up the chain for an SDockingTabStack, then look down to find an SDockTab.
	const FText TabName = FindTabName(ContextWidgetPath, SlateQueryContext);

	// Is there text under the cursor??
	const FText TextUnderCursor = FindTextUnderCursor(WidgetPath, SlateQueryContext);

	// Is there an identifiable item?
	const FText ItemName = FindItemName(WidgetPath, SlateQueryContext);

	// Special case for actor listings in Outliner.
	if (ItemName.IsEmpty() && SlateQueryContext.bInOutliner && !SlateQueryContext.bIsUIWidget)
	{
		SlateQueryContext.bIsObject = true;
	}

	{
		FText NameToQuery;
		bool bHasName = false;
		if (ItemName.IsEmpty() && !TextUnderCursor.IsEmpty())
		{
			NameToQuery = TextUnderCursor;
		}
		else if (!ItemName.IsEmpty() || !TabName.IsEmpty() || !WindowName.IsEmpty())
		{
			NameToQuery = (!ItemName.IsEmpty()) ? ItemName : (!TabName.IsEmpty()) ? TabName : WindowName;
			bHasName = true;
		}

		static const FText QueryFormatWidget = LOCTEXT("QueryFormat1", "I would like to know what {NameToQuery} does.");
		static const FText QueryFormatGeneral = LOCTEXT("QueryFormat2", "I would like to know what \"{NameToQuery}\" means.");
		static const FText QueryFormatObject = LOCTEXT("QueryFormat3", "I would like to know about {NameToQuery}.");
		static const FText QueryInstructionsWidgetFormat = LOCTEXT("QueryInstructionsFormat1",
			"Provide an easily readable, informative, accurate answer that describes what the widget does in the Unreal Editor UI. {QueryAdditionalInstructions} {StyleInstructions}");
		static const FText QueryInstructionsObjectFormat = LOCTEXT("QueryInstructionsFormat2",
			"Provide an easily readable, informative, accurate answer that describes what I should know about it in the Unreal Editor UI. {QueryAdditionalInstructions} {StyleInstructions}");
		static const FText QueryAdditionalInstructionsWidget = LOCTEXT("QueryAdditionalInstructions1",
			"Explain what happens if I select or click on it.");
		static const FText QueryAdditionalInstructionsObject = LOCTEXT("QueryAdditionalInstructions2",
			"Describe what the object is or what it does.");
		static const FText QueryAdditionalInstructionsGeneral = LOCTEXT("QueryAdditionalInstructions3",
			"If the object is clickable, like a button, then explain what happens if I click on it. If the object is something you select, like an asset or a blueprint node, describe what the object is or what it does.");
		static const FText StyleInstructionsWidget = LOCTEXT("StyleInstructions1",
			"Use bold text to emphasize Unreal Engine specific terms. Start with a quick overview paragraph, then add key points in one or two formatted sections with headers and bullet points. Don't include a summary at the end.");
		static const FText StyleInstructionsObject = LOCTEXT("StyleInstructions2",
			"Use bold text to emphasize Unreal Engine specific terms. Start with a quick overview paragraph, then add key points in one or two formatted sections with headers and bullet points. Don't include a summary at the end.");

		if (!NameToQuery.IsEmpty())
		{
			FFormatNamedArguments QueryArgs;
			QueryArgs.Add(TEXT("NameToQuery"), NameToQuery);
			FFormatNamedArguments InstructionArgs;

			if (SlateQueryContext.bIsObject)
			{
				SlateQueryContext.GeneratedQuery = FText::Format(QueryFormatObject, QueryArgs);
				InstructionArgs.Add(TEXT("QueryAdditionalInstructions"), QueryAdditionalInstructionsObject);
				InstructionArgs.Add(TEXT("StyleInstructions"), StyleInstructionsObject);
				SlateQueryContext.GeneratedQueryInstructions = FText::Format(QueryInstructionsObjectFormat, InstructionArgs);
			}
			else if (SlateQueryContext.bIsUIWidget)
			{
				SlateQueryContext.GeneratedQuery = FText::Format(QueryFormatWidget, QueryArgs);
				InstructionArgs.Add(TEXT("QueryAdditionalInstructions"), QueryAdditionalInstructionsWidget);
				InstructionArgs.Add(TEXT("StyleInstructions"), StyleInstructionsWidget);
				SlateQueryContext.GeneratedQueryInstructions = FText::Format(QueryInstructionsWidgetFormat, InstructionArgs);
			}
			else
			{
				SlateQueryContext.GeneratedQuery = FText::Format(QueryFormatWidget, QueryArgs);
				InstructionArgs.Add(TEXT("QueryAdditionalInstructions"), QueryAdditionalInstructionsGeneral);
				InstructionArgs.Add(TEXT("StyleInstructions"), StyleInstructionsWidget);
				SlateQueryContext.GeneratedQueryInstructions = FText::Format(QueryInstructionsWidgetFormat, InstructionArgs);
			}
		}

		if (SlateQueryContext.GeneratedQuery.IsEmpty())
		{
			UE_LOG(LogAIAssistant, Warning, TEXT("Could not generate query for widget."));
			return;
		}
	}

	{
		if (!TabName.IsEmpty() || !WindowName.IsEmpty())
		{
			FFormatNamedArguments Args;
			if (!TabName.IsEmpty() && !WindowName.IsEmpty())
			{
				Args.Add(TEXT("TabName"), TabName);
				Args.Add(TEXT("WindowName"), WindowName);
				SlateQueryContext.GeneratedContextItems.Add(FText::Format(LOCTEXT("QueryContextTabWindowLocation", "I am working in {TabName} of {WindowName}."), Args));
			}
			else
			{
				Args.Add(TEXT("MaybeEmptyTabName"), TabName);
				Args.Add(TEXT("MaybeEmptyWindowName"), WindowName);
				SlateQueryContext.GeneratedContextItems.Add(FText::Format(LOCTEXT("QueryContextTabOrWindowLocation", "I am working in {MaybeEmptyTabName}{MaybeEmptyWindowName}."), Args));
			}

			if (const FText ModeToolContext = GenerateModeToolContext(WidgetPath);
				!ModeToolContext.IsEmpty())
			{
				SlateQueryContext.GeneratedContextItems.Add(ModeToolContext);
			}
		}

		if (const FText DetailsViewContext = GenerateDetailsViewContext(WidgetPath, SlateQueryContext);
			!DetailsViewContext.IsEmpty())
		{
			SlateQueryContext.GeneratedContextItems.Add(DetailsViewContext);
		}

		if (!SlateQueryContext.CurrentToolTipText.IsEmpty())
		{
			FText ToolTipContextFormat = LOCTEXT("QueryContextToolTipAddition", "The text of the current tooltip is: \"{ToolTipText}\", but you don't need to mention it.");
			FFormatNamedArguments Args;
			Args.Add(TEXT("ToolTipText"), SlateQueryContext.CurrentToolTipText);
			FText ToolTipContext = FText::Format(ToolTipContextFormat, Args);
			SlateQueryContext.GeneratedContextItems.Add(ToolTipContext);
		}

		if (!SlateQueryContext.GeneratedContextItems.IsEmpty())
		{
			for (const FText& ContextItem : SlateQueryContext.GeneratedContextItems)
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("GeneratedContextSoFar"), SlateQueryContext.GeneratedContext);
				Args.Add(TEXT("ContextItemToAdd"), ContextItem);
				SlateQueryContext.GeneratedContext = FText::Format(LOCTEXT("LastGeneratedContentFormat", "{GeneratedContextSoFar} {ContextItemToAdd}"), Args);
			}
		}
	}

	// OPTIONAL - We're done using these members. We can clear them to reduce size as they're
	// not used below.
	SlateQueryContext.LastPickedWidget.Reset();
	SlateQueryContext.GeneratedContextItems.Empty();

	// Send widget query to AI Assistant.
	TSharedPtr<SAIAssistantWebBrowser> WebBrowser =
		UAIAssistantSubsystem::GetAIAssistantWebBrowserWidget();
	WebBrowser->CreateConversation();
				
	const FString VisiblePromptString =
		CleanExtraWhiteSpaceFromString(SlateQueryContext.GeneratedQuery.ToString());

	const FString HiddenContextString = 
		CleanExtraWhiteSpaceFromString(SlateQueryContext.GeneratedQueryInstructions.ToString()) +
		FText(LOCTEXT("ContextPrefix", "(Context: ")).ToString() +
		CleanExtraWhiteSpaceFromString(SlateQueryContext.GeneratedContext.ToString()) +
		FText(LOCTEXT("ContextPostfix", ")")).ToString();

	WebBrowser->AddUserMessageToConversation(VisiblePromptString, HiddenContextString);
}


#undef LOCTEXT_NAMESPACE
