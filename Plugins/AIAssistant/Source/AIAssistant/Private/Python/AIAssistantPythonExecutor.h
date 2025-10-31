// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"
#include "Utils/ICodeExecutor.h"

namespace UE::AIAssistant
{
class PythonExecutor : public ICodeExecutor
{
public:
	virtual FCodeExecutionResult Execute(const FString& CodeString) override;

private:
	FText MakeTransactionTitle();
};
	
}
