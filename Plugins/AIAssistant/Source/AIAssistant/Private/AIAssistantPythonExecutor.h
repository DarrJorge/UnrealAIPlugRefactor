// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"

#pragma once


// TODO - Change to follow pattern of IWebJavaScriptExecutor, add tests.

namespace UE::AIAssistant
{
	
struct FCodeExecutionResult
{
	bool bSuccess{false};
	FString Output;
	FText TransactionTitle;
};

class ICodeExecutor
{
public:
	virtual ~ICodeExecutor() = default;

	virtual FCodeExecutionResult Execute(const FString& CodeString) = 0;
};

class PythonExecutor : public ICodeExecutor
{
public:
	virtual FCodeExecutionResult Execute(const FString& CodeString) override;

private:
	FText MakeTransactionTitle();
};
	
}
