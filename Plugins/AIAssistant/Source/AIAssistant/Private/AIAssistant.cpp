// Copyright Epic Games, Inc. All Rights Reserved.


#include "AIAssistant.h"

#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Framework/Application/SlateApplication.h"

#include "UI/AIAssistantCommands.h"
#include "UI/AIAssistantInputProcessor.h"
#include "UI/AIAssistantSlateQuerier.h"
#include "UI/AIAssistantStyle.h"
#include "UI/AIAssistantWebBrowser.h"

#define LOCTEXT_NAMESPACE "FAIAssistantModule"


//
// Statics.
//


static const FName AIAssistantTabName("AIAssistant");


//
// FAIAssistantModule
//


/*virtual*/ void FAIAssistantModule::StartupModule()
{
	FAIAssistantStyle::Initialize();
	FAIAssistantStyle::ReloadTextures();
	
	
	FAIAssistantCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FAIAssistantCommands::Get().OpenAIAssistantTab,
		FExecuteAction::CreateRaw(this, &FAIAssistantModule::OnOpenPluginTab),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FAIAssistantCommands::Get().SlateQueryCommand,
		FExecuteAction::CreateRaw(this, &FAIAssistantModule::OnSlateQuery),
		FCanExecuteAction());
	
	
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAIAssistantModule::RegisterMenus));

	
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AIAssistantTabName, FOnSpawnTab::CreateRaw(this, &FAIAssistantModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FAIAssistantTabTitle", "AI Assistant"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon("AIAssistantStyle","AIAssistant.OpenPluginWindow"));

	
	InputProcessor = MakeShared<FAIAssistantInputProcessor>(PluginCommands);

	// Some scenarios, like commandlets, don't have slate initialized
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
	}
}


/*virtual*/ void FAIAssistantModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	}
	InputProcessor.Reset();

	
	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FAIAssistantStyle::Shutdown();

	FAIAssistantCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AIAssistantTabName);
}


void FAIAssistantModule::OnOpenPluginTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(AIAssistantTabName);
}


void FAIAssistantModule::OnSlateQuery()
{
	OnOpenPluginTab(); // ..idempotent, call before query to ensure tab is open and guarantee a valid AIAssistantWebBrowserWidget

	UE::AIAssistant::SlateQuerier::QueryAIAssistantAboutSlateWidgetUnderCursor();
}


TSharedPtr<SAIAssistantWebBrowser> FAIAssistantModule::GetAIAssistantWebBrowserWidget() 
{
	return AIAssistantWebBrowserWidget;
}


void FAIAssistantModule::ShowContextMenu(const FString& SelectedString, const FVector2f& ClientLocation) const
{
	if (!AIAssistantWebBrowserWidget.IsValid())
	{
		return;
	}
	
	const bool bIsSelectedTextValid = (!SelectedString.IsEmpty());

	
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		FText::FromString("Copy"),
		FText::FromString("Copy selected text to the clipboard."),
		FSlateIcon("AIAssistantStyle", "AIAssistant.Copy"),
		FUIAction(
			FExecuteAction::CreateLambda([SelectedString]() -> void
			{
				// When chosen.

				FPlatformApplicationMisc::ClipboardCopy(*SelectedString);
			}),
			FCanExecuteAction::CreateLambda([bIsSelectedTextValid]() -> bool
			{
				// Whether is enabled.

				return bIsSelectedTextValid;
			})
		)
	);

	
	const FGeometry Geometry = AIAssistantWebBrowserWidget->GetCachedGeometry();
	const FVector2f ScreenLocation = Geometry.LocalToAbsolute(ClientLocation);
	
	FSlateApplication::Get().PushMenu(
		AIAssistantWebBrowserWidget.ToSharedRef(),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		ScreenLocation,
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
	);
}


void FAIAssistantModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner().
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");

		FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");

		Section.AddMenuEntryWithCommandList(FAIAssistantCommands::Get().OpenAIAssistantTab, PluginCommands);
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");

		FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Settings");

		FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			FAIAssistantCommands::Get().OpenAIAssistantTab));

		Entry.SetCommandList(PluginCommands);
	}
}


TSharedRef<SDockTab> FAIAssistantModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	AIAssistantWebBrowserWidget = SNew(SAIAssistantWebBrowser);
	
	
	TSharedRef<SDockTab> DockTabWidget = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.OnTabClosed_Lambda([this](TSharedRef<SDockTab>) -> void
		{
			AIAssistantWebBrowserWidget->OnClosed();			
		})
		[
			AIAssistantWebBrowserWidget.ToSharedRef()
		];

	
	return DockTabWidget;
}


#undef LOCTEXT_NAMESPACE


IMPLEMENT_MODULE(FAIAssistantModule, AIAssistant)
