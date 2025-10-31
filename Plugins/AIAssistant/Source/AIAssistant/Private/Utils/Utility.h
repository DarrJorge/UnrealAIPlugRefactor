#pragma once

#include "CoreMinimal.h"
#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"

namespace UE::AIAsistant
{
	inline FString GetUrlAsRegexString(const FString& Url)
	{
		static const FString CharactersToEscape = TEXT(".*+?()[]{}^$|\\");
	
		FString Escaped;
		Escaped.Reserve(Url.Len() * 2 + 2 /* Allow for regex anchors */);
		Escaped.AppendChar('^');
	
		for (const TCHAR Character : Url)
		{
			if (CharactersToEscape.Contains(FString::Chr(Character)))
			{
				Escaped.AppendChar('\\');
			}
			Escaped += Character;
		}
		Escaped.AppendChar('$');
		Escaped.Shrink();
		return Escaped;
	}
}