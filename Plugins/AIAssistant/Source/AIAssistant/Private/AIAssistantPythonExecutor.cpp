// Copyright Epic Games, Inc. All Rights Reserved.


#include "AIAssistantPythonExecutor.h"

#include "Containers/UnrealString.h"
#include "Editor.h"
#include "Internationalization/Text.h"
#include "IPythonScriptPlugin.h"
#include "PythonScriptTypes.h"


using namespace UE::AIAssistant;


FText PythonExecutor::MakeTransactionTitle()
{
	// TODO Use proper LOCTEXT. But what should it be?
	return FText::FromString(FString::Printf(TEXT("AI Assistant Code Execution %s"), *FDateTime::Now().ToString()));
}

FCodeExecutionResult PythonExecutor::Execute(const FString& CodeString)
{
	FCodeExecutionResult Result;
	if (CodeString.IsEmpty())
	{
		return Result;
	}
	
	FPythonCommandEx PythonCommand;
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	PythonCommand.Command = CodeString;
	
	Result.TransactionTitle = MakeTransactionTitle();
	const int32 TransactionIndex = GEditor->BeginTransaction(Result.TransactionTitle);
	Result.bSuccess = IPythonScriptPlugin::Get()->ExecPythonCommandEx(PythonCommand);
	
	if (Result.bSuccess)
	{
		GEditor->EndTransaction();
	}
	else
	{
		GEditor->CancelTransaction(TransactionIndex);
	}
	
	if (!Result.bSuccess)
	{
		// TODO Not sure why, but failing Python code, and it's errors, only appear in the log, and I don't see them in the loop below!
		Result.Output.Append("Code did not execute successfully. See log for details.");
		if (PythonCommand.LogOutput.Num())
		{
			Result.Output.Append("\n");
		}
	}

	const int32 Num = PythonCommand.LogOutput.Num();
	for (int32 i = 0; i < Num; i++)
	{
		const FPythonLogOutputEntry& PythonLogOutputEntry = PythonCommand.LogOutput[i];
		Result.Output.Append(PythonLogOutputEntry.Output);
		
		if (PythonLogOutputEntry.Type == EPythonLogOutputType::Error)
		{
			Result.Output.Append(" # ERROR");
		}
		else if (PythonLogOutputEntry.Type == EPythonLogOutputType::Warning)
		{
			Result.Output.Append(" # WARNING");
		}
		if (i < Num - 1)
		{
			Result.Output.Append("\n");
		}
	}
	
	return Result;
}
