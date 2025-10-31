// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Utils/ICodeExecutor.h"

namespace UE::AIAssistant
{
	// Executes JavaScript in a web browser.
	struct FFakeWebJavaScriptExecutor : public ICodeExecutor
	{	
		virtual ~FFakeWebJavaScriptExecutor() = default;

		FCodeExecutionResult Execute(const FString& JavaScriptText) override
		{
			ExecutedJavaScriptText.Add(JavaScriptText);
			return FCodeExecutionResult{true};
		}

		TArray<FString> ExecutedJavaScriptText;
	};
}
