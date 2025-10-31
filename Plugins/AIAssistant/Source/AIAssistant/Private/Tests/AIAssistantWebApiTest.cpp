// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Internationalization/Regex.h"
#include "Misc/AutomationTest.h"
#include "Templates/Tuple.h"

#include "AIAssistantFakeWebJavaScriptExecutor.h"
#include "AIAssistantFakeWebJavaScriptDelegateBinder.h"
#include "AIAssistantTestFlags.h"
#include "WebAPI/AIAssistantWebApi.h"
#include "AIAssistantWebJavaScriptResultDelegateAccessor.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace UE::AIAssistant;

class FTestWebApi : public FWebApi
{
public:
	struct FExecutedAsyncFunction {
		FString FunctionName;
		FString Arguments;
		FString HandlerId;
	};

public:
	using FWebApi::FWebApi;
	virtual ~FTestWebApi() = default;

	// Ensure a JavaScript function was called with the specified arguments.
	bool TestExpectAsyncFunctionCall(
		FAutomationTestBase& TestCase,
		const TCHAR* FunctionName,
		const TCHAR* Arguments = TEXT("")) const
	{
		if (!TestCase.TestEqual(FunctionName, 1, ExecutedAsyncFunctions.Num()))
		{
			return false;
		}

		const auto& ExecutedAsyncFunction = ExecutedAsyncFunctions[0];
		bool bCalled;
		bCalled = TestCase.TestEqual(
			FString::Printf(TEXT("%s_Called"), FunctionName),
			ExecutedAsyncFunction.FunctionName, FunctionName);
		bCalled = TestCase.TestEqual(
			FString::Printf(TEXT("%s_Args"), FunctionName),
			ExecutedAsyncFunction.Arguments, Arguments) && bCalled;
		return bCalled;
	}

	// If possible, get the value of a result as JSON.  If the value cannot be converted to JSON
	// return an empty string.
	template<typename ValueType>
	FString GetValueOrEmptyString(const TValueOrError<ValueType, FString>& ValueOrError)
	{
		return ValueOrError.GetValue().ToJson(false);
	}

	template<>
	FString GetValueOrEmptyString<void>(const TValueOrError<void, FString>& UnusedValueOrError)
	{
		return FString();
	}

	// Ensure a JavaScript function was called with the specified arguments and complete it
	// with ResultJson as a result of error if bResultJsonIsError is true.
	template<typename ResultFutureType>
	bool TestExpectAsyncFunctionCallAndComplete(
		FAutomationTestBase& TestCase,
		const TCHAR* FunctionName,
		const TCHAR* Arguments,
		ResultFutureType& ResultFuture,
		const TCHAR* ResultJson,
		bool bResultJsonIsError)
	{
		if (!TestExpectAsyncFunctionCall(TestCase, FunctionName, Arguments) || 
			!TestCase.TestFalse(
				FString::Printf(TEXT("%s_CompleteBeforeHandler"), FunctionName),
				ResultFuture.IsReady()))
		{
			return false;
		}
		
		FWebJavaScriptResultDelegateAccessor::CallHandleResult(
			*WebJavaScriptResultDelegate,
			ExecutedAsyncFunctions[0].HandlerId,
			ResultJson, bResultJsonIsError);

		if (!TestCase.TestTrue(
				FString::Printf(TEXT("%s_CompleteAfterHandler"), FunctionName),
				ResultFuture.IsReady()))
		{
			return false;
		}
		const auto& Result = ResultFuture.Get();
		bool bCompletedAsExpected;
		bCompletedAsExpected = TestCase.TestEqual(
			FString::Printf(TEXT("%s_ResultHasError"), FunctionName),
			Result.HasError(), bResultJsonIsError);
		bCompletedAsExpected = TestCase.TestEqual(
			FString::Printf(TEXT("%s_HasExpectedResult"), FunctionName),
			bResultJsonIsError ? Result.GetError() : GetValueOrEmptyString(Result),
			ResultJson) && bCompletedAsExpected;
		return bCompletedAsExpected;
	}

protected:
	void ExecuteAsyncFunction(
		const TCHAR* FunctionName, const TCHAR* Arguments, const TCHAR* HandlerId) override
	{
		FWebApi::ExecuteAsyncFunction(FunctionName, Arguments, HandlerId);
		ExecutedAsyncFunctions.Emplace(
			FExecutedAsyncFunction{ FunctionName, Arguments, HandlerId });
	}

public:
	TArray<FExecutedAsyncFunction> ExecutedAsyncFunctions;
};

struct FFakeWebApi
{
	FFakeWebApi() : WebApi(WebJavaScriptExecutor, WebJavaScriptDelegateBinder) {}

	FFakeWebJavaScriptExecutor WebJavaScriptExecutor;
	FFakeWebJavaScriptDelegateBinder WebJavaScriptDelegateBinder;
	FTestWebApi WebApi;

	FTestWebApi& operator*() { return WebApi; }
	FTestWebApi* operator->() { return &WebApi; }
};

namespace UE::AIAssistant
{
	class FWebApiAccessor
	{
	public:
		static FString FormatFunctionCall(
			FWebApi& WebApi, const TCHAR* FunctionName, const TCHAR* Arguments = TEXT(""),
			const FString& HandlerId = FString())
		{
			return WebApi.FormatFunctionCall(FunctionName, Arguments, HandlerId);
		}

		static TPair<FString, FString> FormatResultAndErrorHandlers(
			FWebApi& WebApi, const FString& HandlerId)
		{
			return WebApi.FormatResultAndErrorHandlers(HandlerId);
		}
		
		static UAIAssistantWebJavaScriptResultDelegate& GetJavaScriptResultDelegate(
			FWebApi& WebApi)
		{
			return *WebApi.WebJavaScriptResultDelegate;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestFormatFunctionCallNoArgs,
	"AI.Assistant.WebApi.FormatFunctionCallNoArgs",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestFormatFunctionCallNoArgs::RunTest(const FString& UnusedParameters)
{
	FFakeWebApi WebApi;
	return TestEqual(
		TEXT("FormatFunctionCallNoArgs"),
		FWebApiAccessor::FormatFunctionCall(*WebApi, TEXT("test"), TEXT("")),
		TEXT(R"js(
try {
  Promise.resolve(window.eda.test()).then(
    (result) => {
      
    },
    (error) => {
      
    });
} catch (error) {
  
}
)js"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestFormatFunctionCallWithArgs,
	"AI.Assistant.WebApi.FormatFunctionCallWithArgs",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestFormatFunctionCallWithArgs::RunTest(const FString& UnusedParameters)
{
	FFakeWebApi WebApi;
	return TestEqual(
		TEXT("FormatFunctionCallWithArgs"),
		FWebApiAccessor::FormatFunctionCall(*WebApi, TEXT("test"), TEXT("{foo: 'bar'}")),
		TEXT(R"js(
try {
  Promise.resolve(window.eda.test({foo: 'bar'})).then(
    (result) => {
      
    },
    (error) => {
      
    });
} catch (error) {
  
}
)js"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestFormatResultAndErrorHandlers,
	"AI.Assistant.WebApi.FormatResultAndErrorHandlers",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestFormatResultAndErrorHandlers::RunTest(
	const FString& UnusedParameters)
{
	FFakeWebApi WebApi;
	const FString FakeHandlerId = TEXT("foobar");
	const auto Handlers = FWebApiAccessor::FormatResultAndErrorHandlers(*WebApi, FakeHandlerId);
	const auto& JavaScriptResultDelegate = FWebApiAccessor::GetJavaScriptResultDelegate(*WebApi);
	(void)TestEqual(
		TEXT("ResultHandler"),
		Handlers.Key,
		FString::Format(
			*JavaScriptResultDelegate.FormatJavaScriptHandler(FakeHandlerId),
			{ TEXT("result"), TEXT("false") }));
	(void)TestEqual(
		TEXT("ErrorHandler"),
		Handlers.Value,
		FString::Format(
			*JavaScriptResultDelegate.FormatJavaScriptHandler(FakeHandlerId),
			{ TEXT("error"), TEXT("true") }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestFormatFunctionCallWithResultHandler,
	"AI.Assistant.WebApi.FormatFunctionCallWithResultHandler",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestFormatFunctionCallWithResultHandler::RunTest(
	const FString& UnusedParameters)
{
	FFakeWebApi WebApi;
	const FString FakeHandlerId = TEXT("foobar");
	const auto Handlers = FWebApiAccessor::FormatResultAndErrorHandlers(*WebApi, FakeHandlerId);
	return TestEqual(
		TEXT("FormatFunctionCallWithResultHandler"),
		FWebApiAccessor::FormatFunctionCall(*WebApi, TEXT("test"), TEXT(""), FakeHandlerId),
		FString::Format(
			TEXT(R"js(
try {
  Promise.resolve(window.eda.test()).then(
    (result) => {
      {HandleResult}
    },
    (error) => {
      {HandleError}
    });
} catch (error) {
  {HandleError}
}
)js"),
			{
				{ TEXT("HandleResult"), *Handlers.Key },
				{ TEXT("HandleError"), *Handlers.Value },
			}));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestCreateConversation,
	"AI.Assistant.WebApi.CreateConversation",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestCreateConversation::RunTest(const FString& UnusedParameters)
{
	FFakeWebApi WebApi;
	auto Result = WebApi->CreateConversation();
	return WebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, TEXT("createConversation"), TEXT(""), Result, TEXT(""), false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestAddMessageToConversation,
	"AI.Assistant.WebApi.AddMessageToConversation",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestAddMessageToConversation::RunTest(const FString& UnusedParameters)
{
	FFakeWebApi WebApi;
	FAddMessageToConversationOptions Options;
	Options.ConversationId.Emplace().Id = TEXT("convo");
	Options.Message.MessageRole = EMessageRole::User;
	FMessageContent& MessageContent = Options.Message.MessageContent.AddDefaulted_GetRef();
	MessageContent.ContentType = EMessageContentType::Text;
	MessageContent.Content.Emplace<FTextMessageContent>();
	MessageContent.Content.Get<FTextMessageContent>().Text = TEXT("Hello");
	WebApi->AddMessageToConversation(Options);
	return WebApi->TestExpectAsyncFunctionCall(
		*this, TEXT("addMessageToConversation"), *Options.ToJson(false));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestAddAgentEnvironment,
	"AI.Assistant.WebApi.AddAgentEnvironment",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestAddAgentEnvironment::RunTest(const FString& UnusedParameters)
{
	FFakeWebApi WebApi;
	FAgentEnvironment AgentEnvironment;
	AgentEnvironment.Descriptor.EnvironmentName = TEXT("UE");
	AgentEnvironment.Descriptor.EnvironmentVersion = TEXT("5.7.0");
	auto Result = WebApi->AddAgentEnvironment(AgentEnvironment);

	FAgentEnvironmentHandle AgentEnvironmentHandle;
	AgentEnvironmentHandle.Id.Id = TEXT("fakeId");
	AgentEnvironmentHandle.Hash.Hash = TEXT("fakeHash");

	return WebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, TEXT("addAgentEnvironment"), *AgentEnvironment.ToJson(false),
		Result, *AgentEnvironmentHandle.ToJson(false), false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestAddAgentEnvironmentFailed,
	"AI.Assistant.WebApi.AddAgentEnvironmentFailed",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestAddAgentEnvironmentFailed::RunTest(const FString& UnusedParameters)
{
	FFakeWebApi WebApi;
	FAgentEnvironment AgentEnvironment;
	auto Result = WebApi->AddAgentEnvironment(AgentEnvironment);

	return WebApi->TestExpectAsyncFunctionCallAndComplete(
		*this, TEXT("addAgentEnvironment"), *AgentEnvironment.ToJson(false),
		Result, TEXT("failed"), true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestSetAgentEnvironment,
	"AI.Assistant.WebApi.SetAgentEnvironment",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestSetAgentEnvironment::RunTest(const FString& UnusedParameters)
{
	FFakeWebApi WebApi;
	FAgentEnvironmentId Id;
	Id.Id = TEXT("fakeId");
	WebApi->SetAgentEnvironment(Id);
	return WebApi->TestExpectAsyncFunctionCall(
		*this, TEXT("setAgentEnvironment"), *Id.ToJson(false));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantWebApiTestUpdateGlobalLocale,
	"AI.Assistant.WebApi.UpdateGlobalLocale",
	AIAssistantTest::Flags);

bool FAIAssistantWebApiTestUpdateGlobalLocale::RunTest(const FString& UnusedParameters)
{
	FFakeWebApi WebApi;
	FString LocaleFromSetting = TEXT("fr");
	WebApi->UpdateGlobalLocale(LocaleFromSetting);
	return WebApi->TestExpectAsyncFunctionCall(
		*this, TEXT("updateGlobalLocale"), *FString::Printf(TEXT("\"%s\""), *LocaleFromSetting));
}

#endif  // WITH_DEV_AUTOMATION_TESTS