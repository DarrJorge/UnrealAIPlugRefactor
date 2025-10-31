#pragma once

#include "CoreMinimal.h"
#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"

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
}
