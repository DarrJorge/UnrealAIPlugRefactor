// Copyright Epic Games, Inc. All Rights Reserved.

#include "AIAssistantWebApi.h"

#include "Async/Future.h"
#include "Containers/UnrealString.h"
#include "Misc/AssertionMacros.h"
#include "Templates/SharedPointer.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Object.h"

namespace UE::AIAssistant
{
	UE_ENUM_METADATA_DEFINE(EMessageRole, UE_AI_ASSISTANT_MESSAGE_ROLE_ENUM);
	UE_ENUM_METADATA_DEFINE(EMessageContentType, UE_AI_ASSISTANT_MESSAGE_CONTENT_TYPE_ENUM);

	const FString FWebApi::WebApiObjectName = TEXT("window.eda");

	const FString FWebApi::FunctionCallFormatTemplate = TEXT(R"js(
try {
  Promise.resolve({WebApiObjectName}.{FunctionName}({Arguments})).then(
    (result) => {
      {NotifyHandlerOfResult}
    },
    (error) => {
      {NotifyHandlerOfError}
    });
} catch (error) {
  {NotifyHandlerOfError}
}
)js");

	FWebApi::FWebApi(
		IWebJavaScriptExecutor& JavaScriptExecutor,
		IWebJavaScriptDelegateBinder& JavaScriptDelegateBinder) :
		WebJavaScriptExecutor(JavaScriptExecutor),
		WebJavaScriptDelegateBinder(JavaScriptDelegateBinder),
		WebJavaScriptResultDelegate(NewObject<UAIAssistantWebJavaScriptResultDelegate>())
	{
		WebJavaScriptResultDelegate->Bind(*this);
	}

	FWebApi::~FWebApi()
	{
		// Unfortunately on shutdown the delegate can be garbage collected even though a strong
		// reference to it is held.
		if (WebJavaScriptResultDelegate.IsValid())
		{
			WebJavaScriptResultDelegate->Unbind();
		}
	}

	void FWebApi::AddMessageToConversation(const FAddMessageToConversationOptions& Options)
	{
		(void)ExecuteFunctionWithJsonArgument(TEXT("addMessageToConversation"), Options);
	}

	TFuture<TValueOrError<void, FString>> FWebApi::CreateConversation()
	{
		return ExecutionFunctionParseJson<void>(TEXT("createConversation"));
	}

	TFuture<TValueOrError<FAgentEnvironmentHandle, FString>> FWebApi::AddAgentEnvironment(
		const FAgentEnvironment& AgentEnvironment)
	{
		return ExecutionFunctionParseJson<FAgentEnvironmentHandle>(
			TEXT("addAgentEnvironment"), AgentEnvironment);
	}

	void FWebApi::SetAgentEnvironment(const FAgentEnvironmentId& AgentEnvironmentId)
	{
		(void)ExecuteFunctionWithJsonArgument(TEXT("setAgentEnvironment"), AgentEnvironmentId);
	}

	void FWebApi::UpdateGlobalLocale(const FString& LocaleString)
	{
		(void)ExecuteFunction(TEXT("updateGlobalLocale"), *FString::Printf(TEXT("\"%s\""), *LocaleString));
	}

	FString FWebApi::FormatFunctionCall(
		const TCHAR* FunctionName, const TCHAR* Arguments, const FString& HandlerId)
	{
		check(FunctionName);
		check(Arguments);
		auto Handlers = FormatResultAndErrorHandlers(HandlerId);
		return FString::Format(
			*FunctionCallFormatTemplate,
			{
				{ TEXT("WebApiObjectName"), WebApiObjectName },
				{ TEXT("FunctionName"), FunctionName },
				{ TEXT("Arguments"), Arguments },
				{ TEXT("NotifyHandlerOfResult"), Handlers.Key },
				{ TEXT("NotifyHandlerOfError"), Handlers.Value },
			});
	}

	TPair<FString, FString> FWebApi::FormatResultAndErrorHandlers(const FString& HandlerId)
	{
		if (!HandlerId.IsEmpty())
		{
			FString HandlerFormat =
				WebJavaScriptResultDelegate->FormatJavaScriptHandler(HandlerId);
			return TPair<FString, FString>(
				FString::Format(*HandlerFormat, { TEXT("result"), TEXT("false") }),
				FString::Format(*HandlerFormat, { TEXT("error"), TEXT("true") }));
		}
		return TPair<FString, FString>();
	}

	TFuture<UAIAssistantWebJavaScriptResultDelegate::FResult> FWebApi::ExecuteFunction(
		const TCHAR* FunctionName, const TCHAR* Arguments)
	{
		auto HandlerIdAndFuture = WebJavaScriptResultDelegate->RegisterResultHandlerForFuture();
		ExecuteAsyncFunction(FunctionName, Arguments, *HandlerIdAndFuture.Key);
		return MoveTemp(HandlerIdAndFuture.Value);
	}

	void FWebApi::ExecuteAsyncFunction(
		const TCHAR* FunctionName, const TCHAR* Arguments, const TCHAR* HandlerId)
	{
		WebJavaScriptExecutor.ExecuteJavaScript(
			FormatFunctionCall(FunctionName, Arguments, HandlerId));
	}

	void FWebApi::BindUObject(const FString& Name, UObject* Object, bool bIsPermanent)
	{
		WebJavaScriptDelegateBinder.BindUObject(Name, Object, bIsPermanent);
	}

	void FWebApi::UnbindUObject(const FString& Name, UObject* Object, bool bIsPermanent)
	{
		WebJavaScriptDelegateBinder.UnbindUObject(Name, Object, bIsPermanent);
		WebJavaScriptResultDelegate.Reset();
	}

}  // namespace UE::AIAssistant