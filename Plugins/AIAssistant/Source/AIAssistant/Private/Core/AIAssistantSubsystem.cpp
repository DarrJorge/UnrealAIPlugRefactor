// Copyright Epic Games, Inc. All Rights Reserved.


#include "AIAssistantSubsystem.h"

#include "Containers/UnrealString.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"

#include "AIAssistant.h"
#include "Python/AIAssistantPythonExecutor.h"
#include "UI/AIAssistantWebBrowser.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AIAssistantSubsystem)


using namespace UE::AIAssistant;

//
// Statics.
//

FAIAssistantModule& UAIAssistantSubsystem::GetAIAssistantModule()
{
	return FModuleManager::LoadModuleChecked<FAIAssistantModule>(UE_PLUGIN_NAME);
}

// Get the assistant web browser.
TSharedPtr<SAIAssistantWebBrowser> UAIAssistantSubsystem::GetAIAssistantWebBrowserWidget()
{
	auto AIAssistantWebBrowserWidget = GetAIAssistantModule().GetAIAssistantWebBrowserWidget();
	check(AIAssistantWebBrowserWidget.IsValid());
	return AIAssistantWebBrowserWidget;
}

//
// UAIAssistantSubsystem
//

void UAIAssistantSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAIAssistantSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

FString UAIAssistantSubsystem::ExecutePythonScriptViaJavaScript(const FString& Code)
{
	const TUniquePtr<ICodeExecutor> CodeExecutor = MakeUnique<PythonExecutor>();
	const FCodeExecutionResult Result = CodeExecutor->Execute(Code);

	if (Result.bSuccess && Result.Output.IsEmpty())
	{
		return TEXT("Code executed successfully.");
	}
	
	return Result.Output;
}


/*no:static*/ void UAIAssistantSubsystem::ShowContextMenuViaJavaScript(const FString& SelectedString, const int32 ClientX, const int32 ClientY) const
{
	GetAIAssistantModule().ShowContextMenu(SelectedString, FVector2f(ClientX, ClientY));
}

