// Copyright Epic Games, Inc. All Rights Reserved.

#include "AIAssistantConfig.h"

#include "Containers/Set.h"
#include "Misc/FileHelper.h"
#include "Misc/Optional.h"
#include "Misc/Paths.h"
#include "Templates/UnrealTemplate.h"

#include "AIAssistantLog.h"

const FString FAIAssistantConfig::DefaultFilename("AIAssistant.json");

const FString FAIAssistantConfig::DefaultMainUrl(
	"https://dev.epicgames.com/community/assistant/embedded");
// URL patterns (regular expressions) that can be opened in the assistant view.
// This allow-list is intended to allow non-redirect URLs required to perform operations like
// authentication.
const TArray<FString> FAIAssistantConfig::DefaultAllowedUrlRegexes =
{
	// All data is allowed.
	TEXT(R"regex(^data:.*)regex"),
	// All local files are allowed.
	TEXT(R"regex(^file:.*)regex"),
	// Community sign-in API.
	TEXT(R"regex(^https://dev\.epicgames\.com/community/api/user_identity/.*)regex"),
	// Epic online services sign-in API.
	TEXT(R"regex(^https://www\.epicgames\.com/id/.*)regex"),

#if WITH_AIASSISTANT_EPIC_INTERNAL // ..see Build.cs
	// Allow the Captcha service used by Epic online service sign-in.
	TEXT(R"regex(^https://newassets\.hcaptcha\.com/captcha/.*)regex"),
	// Required to access staging.
	TEXT(R"regex(^https://artstation\.cloudflareaccess\.com/.*)regex"),
#endif
};

FString FAIAssistantConfig::GetMainUrlAsRegexString() const
{
	static const TSet<FString::ElementType> CharactersToEscape{
		'.', '*', '+', '?', '(', ')', '[', ']', '{', '}', '^', '$', '|', '\\',
	};
	FString Escaped;
	Escaped.Reserve((MainUrl.Len() * 2) + 2 /* Allow for regex anchors */);
	Escaped += FString::ElementType('^');
	for (const FString::ElementType Character : MainUrl)
	{
		if (CharactersToEscape.Contains(Character))
		{
			Escaped += FString::ElementType('\\');
		}
		Escaped += Character;
	}
	Escaped += FString::ElementType('$');
	Escaped.Shrink();
	return Escaped;
}

TArray<FString> FAIAssistantConfig::GetAllAllowedUrlRegexes() const
{
	const TArray<const TArray<FString>*> AllowedRegexStringArrays{
		&DefaultAllowedUrlRegexes,
		&AllowedUrlRegexes };
	TArray<FString> AllowedRegexStrings{ GetMainUrlAsRegexString() };
	for (const auto& AllowedRegexStringArray : AllowedRegexStringArrays)
	{
		for (const auto& AllowedRegexString : *AllowedRegexStringArray)
		{
			AllowedRegexStrings.Emplace(AllowedRegexString);
		}
	}
	return AllowedRegexStrings;
}

TArray<FRegexPattern> FAIAssistantConfig::GetAllowedUrlRegexPatterns() const
{
	TArray<FRegexPattern> Regexes;
	TArray<FString> AllAllowedRegexes = GetAllAllowedUrlRegexes();
	Regexes.Reserve(AllAllowedRegexes.Num());
	for (const FString& AllowedRegexString : AllAllowedRegexes)
	{
		Regexes.Emplace(AllowedRegexString);
	}
	return Regexes;
}

TArray<FString> FAIAssistantConfig::GetDefaultSearchDirectories()
{
	TArray<FString> BaseSearchPaths =
	{
		FPaths::EngineConfigDir(),
		FPaths::EngineUserDir(),
		FPaths::EngineVersionAgnosticUserDir(),
	};
	TArray<FString> AllPathsToSearch;
	TArray<TOptional<FPaths::EPathConversion>> PathConversions =
	{
		TOptional(FPaths::EPathConversion::Engine_NotForLicensees),
		TOptional(FPaths::EPathConversion::Engine_NoRedist),
		TOptional(FPaths::EPathConversion::Engine_LimitedAccess),
		TOptional<FPaths::EPathConversion>(),
	};
	for (const TOptional<FPaths::EPathConversion>& PathConversion : PathConversions)
	{
		for (const FString& BaseSearchPath : BaseSearchPaths)
		{
			AllPathsToSearch.Emplace(
				PathConversion.IsSet()
				? FPaths::ConvertPath(BaseSearchPath, PathConversion.GetValue())
				: BaseSearchPath);
		}
		
	}
	return AllPathsToSearch;
}

FString FAIAssistantConfig::FindConfigFile(const TArray<FString>& SearchDirectories)
{
	for (auto SearchDirectory : SearchDirectories)
	{
		auto FullFilename = FPaths::Combine(SearchDirectory, DefaultFilename);
		UE_LOG(
			LogAIAssistant, Display /* Verbose */, TEXT("Searching for AI assistant config in %s"),
			*FullFilename);
		if (FPaths::FileExists(FullFilename))
		{
			return FullFilename;
		}
	}
	return FString();
}

FAIAssistantConfig FAIAssistantConfig::Load(const FString& Filename)
{
	FString Json(TEXT("{}"));
	if (!Filename.IsEmpty() && !FFileHelper::LoadFileToString(Json, *Filename))
	{
		UE_LOG(
			LogAIAssistant, Error, TEXT("Failed to load AI assistant config from \"%s\"."),
			*Filename);
	}
	FAIAssistantConfig Config;
	if (!Config.FromJson(Json))
	{
		UE_LOG(
			LogAIAssistant, Error, TEXT("Failed to load AI assistant config \"%s\" from \"%s\"."),
			*Json, *Filename);
		// Use the default config.
		(void)Config.FromJson(FString());
	}
	return Config;
}