// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once


#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"
#include "Framework/Commands/UICommandList.h"


class FAIAssistantInputProcessor;
class FSpawnTabArgs;
class SAIAssistantWebBrowser;
class SDockTab;
class UAIAssistantSubsystem;


//
// FAIAssistantModule
//


class FAIAssistantModule : public IModuleInterface
{
	friend class UAIAssistantSubsystem;

	
public:

	
	// IModuleInterface interface 
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	

	/**
	 * Get the AI Assistant's web browser widget.
	 * @return The AI Assistant's web browser widget.
	 */
	TSharedPtr<SAIAssistantWebBrowser> GetAIAssistantWebBrowserWidget();


private:

	
	/**
	 * This function will be bound to a Command. By default, it will bring up plugin tab.
	 */
	void OnOpenPluginTab();

	/**
	 * This function will be bound to a Command. It will use AI Assistant to query the Slate UI at the current mouse position.
	 */
	void OnSlateQuery();

	
	/**
	 * DEPRECATED?
	 * Everything left in this method can be done in JavaScript.
	 * 
	 * Pops up a context menu related to the contents in the AI Assistant web browser.
	 * We usually get here via JavaScript, which provides these parameters.
	 * @param SelectedString String for that is selected in web browser.
	 * @param ClientLocation The location where the menu should appear.
	 */
	void ShowContextMenu(const FString& SelectedString, const FVector2f& ClientLocation) const;

	void RegisterMenus();
	
	TSharedRef<SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs);

	TSharedPtr<FUICommandList> PluginCommands;
	TSharedPtr<FAIAssistantInputProcessor> InputProcessor;
	TSharedPtr<SAIAssistantWebBrowser> AIAssistantWebBrowserWidget;
};
