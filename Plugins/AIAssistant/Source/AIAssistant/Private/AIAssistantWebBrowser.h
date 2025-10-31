// Copyright Epic Games, Inc. All Rights Reserved.
	

#pragma once


#include "Misc/Optional.h"
#include "SWebBrowser.h"

#include "AIAssistantConfig.h"
#include "AIAssistantConsole.h"
#include "AIAssistantConversationReadyExecutor.h"
#include "AIAssistantExecuteWhenReady.h"
#include "AIAssistantWebJavaScriptDelegateBinder.h"
#include "AIAssistantWebJavaScriptExecutor.h"
#include "AIAssistantWebApi.h"


//
// SAIAssistantWebBrowser
//
// See all NOTE_JAVASCRIPT_CPP_FUNCTIONS.
// A UObject can be bound to SWebBrowser.
// That will make that Object's UFUNCTIONs available for calling in JavaScript.
// They will be available as "window.ue.namespace.functionname".
// In our case, we may choose to bind the UAIAssistantSubsystem to this web browser, as 'aiassistantsubsystem'.
// So, for example, JavaScript would then be able to call - "window.ue.aiassistantsubsystem.executepythonscriptviajavascript(code)".
// IMPORTANT - Yes, it must be all lowercase, or the call will fail.
//


class SAIAssistantWebBrowser :
	public SCompoundWidget,
	public UE::AIAssistant::IWebJavaScriptDelegateBinder,
	public UE::AIAssistant::IWebJavaScriptExecutor
{
public:
	SLATE_BEGIN_ARGS(SAIAssistantWebBrowser) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	
	/**
	 * Called when widget becomes closed.
	 */
	void OnClosed();
	
	
	/** Loads a URL.
	 * @param Url Url to load.
	 * @param bOpenInExternalBrowser to open the URL in an external web browser.
	 */
	bool LoadUrl(const FString& Url, const bool bOpenInExternalBrowser) const;
	
	
	/**
	 * JavaScript string to immediately execute in the web browser.
	 * @param JavaScript 
	 */
	virtual void ExecuteJavaScript(const FString& JavaScript) override;

	// See IWebBrowserWindow::BindUObject() / IWebBrowserWindow::UnbindUObject().
	virtual void BindUObject(const FString& Name, UObject* Object, bool bIsPermanent = true) override;
	virtual void UnbindUObject(const FString& Name, UObject* Object, bool bIsPermanent = true) override;
	// Treat all keys as handled by this widget when it has focus. This prevents hotkeys from firing when users chat with the assistant.
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override { return FReply::Handled(); }
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override { return FReply::Handled(); }


	// High level conversation API.

	// Asynchronously create a new conversation.
	void CreateConversation();

	// Add a message to the existing conversation.
	// If a new conversation is being currently being created, clear enqueued messages and
	// enqueue the specified message.
	void AddUserMessageToConversation(
		const FString& VisiblePrompt, const FString& HiddenContext = FString());
	
private:

	// FExecuteWhenReady interface
	UE::AIAssistant::FExecuteWhenReady::EExecuteWhenReadyState GetExecuteWhenReadyState() const;

	enum class EWebBrowserLoadState : uint8
	{
		Default = 0,
		LoadStarted,
		LoadError,
		LoadComplete,
	};

	// Current load state of web browser page.
	EWebBrowserLoadState WebBrowserLoadState = EWebBrowserLoadState::Default;
	
	// Determine whether a URL can be loaded.
	bool CanLoadUrl(const FString& Url) const;
	
	// Load or reload the assistant configuration.
	void LoadConfig();

	// Initialize conversation ready executor.
	void InitializeConversationReadyExecutor();

	/**
	 * Whether the AI assistant page has loaded.
	 * @return Whether we have a valid AI Assistant web state.
	 */
	bool IsAssistantPageLoaded() const;

	/**
	 * Get the current low level web API.
	 * @return Web API.
	 */
	UE::AIAssistant::FWebApi& GetWebApi();

	// Set / update the agent environment.
	void UpdateAgentEnvironment(bool bUseUefnMode);

	// Update the current browser state.
	void UpdateWebBrowserLoadState(const EWebBrowserLoadState InWebBrowserLoadState);

	// Handle language / culture changed notification.
	void OnCultureChanged();
	
	// Widgets.
	TSharedPtr<SWebBrowser> WebBrowserWidget;
	
	// Configuration.
	FAIAssistantConfig Config;
	TArray<FRegexPattern> AllowedUrlRegexPatterns;

	// Previously opened URL.
	FString LastOpenedUrl;
	// Whether navigation changed.
	bool bOpenedUrlChanged = false;
	// Previously loaded URL that was not a redirect.
	FString LastOpenedNonRedirectUrl;
	// Interface for the assistant web application.
	TOptional<UE::AIAssistant::FWebApi> WebApi;
	// Whether the agent environment has been configured since loading the page.
	TOptional<bool> bAgentEnvironmentIsUefn;
	// Subscription to a cvar that controls the mode of the assistant.
	TOptional<UE::AIAssistant::FUefnModeSubscription> UefnModeSubscription;
	// Handles deferring adding messages until a conversation is ready.
	TOptional<UE::AIAssistant::FConversationReadyExecutor> ConversationReadyExecutor;
};
