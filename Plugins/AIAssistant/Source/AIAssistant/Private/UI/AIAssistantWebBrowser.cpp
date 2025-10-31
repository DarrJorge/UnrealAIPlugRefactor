// Copyright Epic Games, Inc. All Rights Reserved.
	

#include "AIAssistantWebBrowser.h"

#include "Editor.h"
#include "WebBrowserModule.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Regex.h"
#include "IWebBrowserWindow.h"
#include "Misc/AssertionMacros.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"

#include "Core/AIAssistantLog.h"

using namespace UE::AIAssistant;


//
// Macros.
//


// SWebBrowser::InitialURL() does not seem to load our initial URL. Loading after the SWebBrowser is created works instead. TODO - Investigate.
#define UE_AIA_SET_INITIAL_URL_VIA_WEB_BROWSER_SLATE_ARG 0


//
// SAIAssistantWebBrowser
//


void SAIAssistantWebBrowser::Construct(const FArguments& InArgs)
{
	// Load config.
	LoadConfig();


	// We need to call this mainly to initialize CEF (Chromium Embedded Framework.)
	// NOTE - We have not enabled the WebBrowserWidget for this plugin. If we had, then this would not be necessary, and this would have been taken
	// care of for us.

	TSharedPtr<IWebBrowserWindow> WebBrowserWindow;
	{
		FCreateBrowserWindowSettings WindowSettings;
		WindowSettings.bUseTransparency = true;
		WindowSettings.BrowserFrameRate = 60;

		IWebBrowserModule& WebBrowserModule = FModuleManager::LoadModuleChecked<IWebBrowserModule>("WebBrowser");
		IWebBrowserSingleton* WebBrowserSingleton = WebBrowserModule.GetSingleton();
		WebBrowserWindow = WebBrowserSingleton->CreateBrowserWindow(WindowSettings);
	}
	
	// Web Browser.

	SAssignNew(WebBrowserWidget, SWebBrowser, /*passed to SWebBrowser ctr..*/ WebBrowserWindow)
#if UE_AIA_SET_INITIAL_URL_VIA_WEB_BROWSER_SLATE_ARG
		.InitialURL(Config.MainUrl) 
#endif
		.ShowControls(false)
		.Visibility(EVisibility::Visible)
		.OnBeforeNavigation_Lambda([this](const FString& Url, const FWebNavigationRequest& Request) -> bool
		{
			bOpenedUrlChanged = Url != LastOpenedUrl;
			LastOpenedUrl = Url;
			
			// 'Navigation' means an attempt to redirect the current browser window.
			bool bOpenInThisBrowser = CanLoadUrl(Url);
				
			// If this is a redirect and the last non-redirect page loaded is in the allow-list, allow the
			// page to load.
				
			if (!bOpenInThisBrowser && Request.bIsRedirect)
			{
				bOpenInThisBrowser = CanLoadUrl(LastOpenedNonRedirectUrl);
			}
				
			if (bOpenInThisBrowser)
			{
				if (!Request.bIsRedirect)
				{
					LastOpenedNonRedirectUrl = Url;
				}
				
				return false; // ..means don't block navigation in THIS browser
			}
			else
			{
				(void) LoadUrl(Url, true); // ..open link externally instead

				return true; // ..means block navigation in THIS browser
			}
		})
		.OnBeforePopup_Lambda([this](/*no const&*/FString Url, /*no const&*/FString Frame) -> bool
		{
			// 'Popup' means an attempt to open a new browser window.

			(void) LoadUrl(Url, true); // ..open link externally instead

			return true; // ..means block navigation in THIS browser
		})
		.OnConsoleMessage_Lambda([](const FString& Message, const FString& Source, int32 Line, EWebBrowserConsoleLogSeverity WebBrowserConsoleLogSeverity) -> void
		{
			// Logs messages from JavaScript.
								
			if (WebBrowserConsoleLogSeverity == EWebBrowserConsoleLogSeverity::Error || WebBrowserConsoleLogSeverity == EWebBrowserConsoleLogSeverity::Fatal)
			{
				UE_LOG(LogAIAssistant, Error, TEXT("JavaScript Error - '%s' @ %s:%d"), *Message, *Source, Line);
			}
			else if (WebBrowserConsoleLogSeverity == EWebBrowserConsoleLogSeverity::Warning)
			{
				UE_LOG(LogAIAssistant, Warning, TEXT("JavaScript Warning - '%s' @ %s:%d"), *Message, *Source, Line);
			}
			else
			{
				UE_LOG(LogAIAssistant, Display, TEXT("JavaScript - '%s' @ %s:%d"), *Message, *Source, Line);
			}
		})
		.OnLoadStarted_Lambda([this]() -> void
		{
			UpdateWebBrowserLoadState(EWebBrowserLoadState::LoadStarted);
		})
		.OnLoadError_Lambda([this]() -> void
		{
			UpdateWebBrowserLoadState(EWebBrowserLoadState::LoadError);
		})
		.OnLoadCompleted_Lambda([this]() -> void
		{
			// If UEFN mode changes aren't being monitored, subscribe to updates.
			if (!UefnModeSubscription.IsSet())
			{
				UefnModeSubscription.Emplace(
					[this](bool bIsUefnMode) -> void
					{
						UpdateAgentEnvironment(bIsUefnMode);
					});
			}
			// If the URL changed, try updating the agent environment mode.
			if (bOpenedUrlChanged)
			{
				UpdateAgentEnvironment(
					bAgentEnvironmentIsUefn.IsSet() ? bAgentEnvironmentIsUefn.GetValue() : false);
			}
		});
	
	
	check(!WebApi.IsSet());
	WebApi.Emplace(*this, *this);
	InitializeConversationReadyExecutor();

	FInternationalization::Get().OnCultureChanged().AddSP(SharedThis(this), &SAIAssistantWebBrowser::OnCultureChanged);
	
#if !UE_AIA_SET_INITIAL_URL_VIA_WEB_BROWSER_SLATE_ARG

	// Load main URL.
	
	(void) LoadUrl(Config.MainUrl, false);
	
#endif
	
	
	// Widget tree.
	
	ChildSlot
	[
		WebBrowserWidget.ToSharedRef()
	];
}


void SAIAssistantWebBrowser::OnClosed()
{
	WebBrowserLoadState = EWebBrowserLoadState::Default;
	ConversationReadyExecutor.Reset();
}


void SAIAssistantWebBrowser::UpdateWebBrowserLoadState(const EWebBrowserLoadState InWebBrowserLoadState)
{
	WebBrowserLoadState = InWebBrowserLoadState; // ..set this first

	ConversationReadyExecutor->UpdateExecuteWhenReady();
}


void SAIAssistantWebBrowser::InitializeConversationReadyExecutor()
{
	ConversationReadyExecutor.Emplace(
		[this]() -> UE::AIAssistant::FExecuteWhenReady::EExecuteWhenReadyState
		{
			return GetExecuteWhenReadyState();
		});
}

void SAIAssistantWebBrowser::UpdateAgentEnvironment(bool bUseUefnMode)
{
	bool bUefnModeChanged =
		bAgentEnvironmentIsUefn.IsSet() && bAgentEnvironmentIsUefn.GetValue() != bUseUefnMode;
	if (IsAssistantPageLoaded() && (bUefnModeChanged || !bAgentEnvironmentIsUefn.IsSet()))
	{
		bAgentEnvironmentIsUefn.Emplace(bUseUefnMode);

		// Configure the agent's environment.
		FAgentEnvironment AgentEnvironment;
		AgentEnvironment.Descriptor.EnvironmentName = bUseUefnMode ? TEXT("UEFN") : TEXT("UE");
		AgentEnvironment.Descriptor.EnvironmentVersion = FEngineVersion::Current().ToString();
		GetWebApi().AddAgentEnvironment(AgentEnvironment).Then(
			[this, bUseUefnMode](const auto& ResultFuture) mutable -> void
			{
				const auto& Result = ResultFuture.Get();
				if (Result.HasError())
				{
					UE_LOG(LogAIAssistant, Error, TEXT("%s"), *Result.GetError());
					bAgentEnvironmentIsUefn.Reset();
					InitializeConversationReadyExecutor();

					UpdateWebBrowserLoadState(EWebBrowserLoadState::LoadError);
				}
				else
				{
					GetWebApi().SetAgentEnvironment(Result.GetValue().Id);

					ConversationReadyExecutor->NotifyAgentEnvironmentConfigured();
					UpdateWebBrowserLoadState(EWebBrowserLoadState::LoadComplete);
				}
			});
		SAIAssistantWebBrowser::OnCultureChanged();
	}
	else
	{
		UpdateWebBrowserLoadState(EWebBrowserLoadState::LoadComplete);
	}
}


bool SAIAssistantWebBrowser::LoadUrl(const FString& Url, const bool bOpenInExternalBrowser) const
{
	if (bOpenInExternalBrowser)
	{
		FString ErrorString;
		FPlatformProcess::LaunchURL(*Url, TEXT(""), &ErrorString);
		if (!ErrorString.IsEmpty())
		{
			UE_LOG(LogAIAssistant, Error, TEXT("%hs() - Could not open URL '%s' - '%s'."), __func__, *Url, *ErrorString);

			return false;
		}
	}
	else
	{
		WebBrowserWidget->LoadURL(Url);
	}
	

	return true;
}


FCodeExecutionResult SAIAssistantWebBrowser::Execute(const FString& JavaScript)
{
	check(WebBrowserWidget);
	WebBrowserWidget->ExecuteJavascript(JavaScript);
	return FCodeExecutionResult{true};
}


void SAIAssistantWebBrowser::BindUObject(const FString& Name, UObject* Object, bool bIsPermanent)
{
	check(WebBrowserWidget);
	WebBrowserWidget->BindUObject(Name, Object, bIsPermanent);
}


void SAIAssistantWebBrowser::UnbindUObject(const FString& Name, UObject* Object, bool bIsPermanent)
{
	check(WebBrowserWidget);
	WebBrowserWidget->UnbindUObject(Name, Object, bIsPermanent);
}


bool SAIAssistantWebBrowser::CanLoadUrl(const FString& Url) const
{
	for (const FRegexPattern& Pattern : AllowedUrlRegexPatterns)
	{
		if (FRegexMatcher(Pattern, Url).FindNext()) return true;
	}
	return false;
}


void SAIAssistantWebBrowser::LoadConfig()
{
	Config = FAIAssistantConfig::Load();
	
	// Cache regex patterns to avoid reallocation / compilation.
	AllowedUrlRegexPatterns = Config.GetAllowedUrlRegexPatterns();
}


void SAIAssistantWebBrowser::CreateConversation()
{
	if (!ConversationReadyExecutor->SetCreatingConversation(true))
	{
		GetWebApi().CreateConversation().Then(
			[this](const TFuture<TValueOrError<void, FString>>& UnusedResult) -> void
			{
				ConversationReadyExecutor->SetCreatingConversation(false);
			});
	}
}


void SAIAssistantWebBrowser::AddUserMessageToConversation(
	const FString& VisiblePrompt, const FString& HiddenContext)
{
	FAddMessageToConversationOptions Options;
	auto& Message = Options.Message;
	Message.MessageRole = EMessageRole::User;
	auto& MessageContent = Message.MessageContent;
	for (const auto& PromptAndVisible : {
			TPair<const FString&, bool>(VisiblePrompt, true),
			TPair<const FString&, bool>(HiddenContext, false),
		})
	{
		const auto& Prompt = PromptAndVisible.Key;
		bool bVisible = PromptAndVisible.Value;
		if (Prompt.IsEmpty()) continue;

		auto& MessageContentItem = MessageContent.Emplace_GetRef();
		MessageContentItem.bVisibleToUser = bVisible;
		MessageContentItem.ContentType = EMessageContentType::Text;
		MessageContentItem.Content.Emplace<FTextMessageContent>();
		MessageContentItem.Content.Get<FTextMessageContent>().Text = Prompt;
	}

	ConversationReadyExecutor->ExecuteWhenReady(
		[this, Options = MoveTemp(Options)]() -> void
		{
			GetWebApi().AddMessageToConversation(Options);
		});
}

FWebApi& SAIAssistantWebBrowser::GetWebApi()
{
	return *WebApi;
}

bool SAIAssistantWebBrowser::IsAssistantPageLoaded() const
{
	return LastOpenedUrl.StartsWith(Config.MainUrl);
}


UE::AIAssistant::FExecuteWhenReady::EExecuteWhenReadyState SAIAssistantWebBrowser::GetExecuteWhenReadyState() const
{
	if (WebBrowserLoadState == EWebBrowserLoadState::LoadComplete)
	{
		// We assume that the deferred functions only apply while having a valid AI Assistant web state. Executing them otherwise might result in
		// undefined behavior.
		
		if (IsAssistantPageLoaded())
		{
			return FExecuteWhenReady::EExecuteWhenReadyState::Execute;
		}
		else
		{
			// Keep the deferred functions, if any, for whenever we do arrive at a valid AI Assistant web state.
			// (If this becomes a problem, reject instead.)
			
			return FExecuteWhenReady::EExecuteWhenReadyState::Wait;
		}
	}
	else if (WebBrowserLoadState == EWebBrowserLoadState::LoadError)
	{
		return FExecuteWhenReady::EExecuteWhenReadyState::Reject;
	}

	return FExecuteWhenReady::EExecuteWhenReadyState::Wait;
}

void SAIAssistantWebBrowser::OnCultureChanged()
{
	FString LanguageCode = FInternationalization::Get().GetCurrentLanguage()->GetName();
	WebApi->UpdateGlobalLocale(LanguageCode);
}
