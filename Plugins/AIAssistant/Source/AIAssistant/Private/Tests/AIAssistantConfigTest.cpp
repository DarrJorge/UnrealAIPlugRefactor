// Copyright Epic Games, Inc. All Rights Reserved.

#include "Math/UnrealMathUtility.h"
#include "Misc/AssertionMacros.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#include "Core/AIAssistantConfig.h"
#include "Tests/AIAssistantTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

// Create a temporary directory for the lifetime of this class.
class FTemporaryDirectory
{
public:
	
	// Create a temporary directory.
	FTemporaryDirectory() :
		// NOTE: This uses a similar path to ContentCommandlets as there doesn't seem to be functionality
		// to get the platform's temporary directory.
		Directory(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Temp"), FGuid::NewGuid().ToString())),
		FileManager(IFileManager::Get())
	{
		verify(FileManager.MakeDirectory(*Directory, true));
	}

	// Disable copy.
	FTemporaryDirectory(const FTemporaryDirectory&) = delete;
	FTemporaryDirectory& operator=(const FTemporaryDirectory&) = delete;

	// Delete a temporary directory.
	~FTemporaryDirectory()
	{
		// ProjectSavedDir()/Temp will still remain, but may be empty.
		FileManager.DeleteDirectory(*Directory, /* Exists */ 0, /* Tree */ 1);
	}

	// Get the temporary directory name.
	const FString& operator*() const { return Directory; }

private:
	
	FString Directory;
	IFileManager& FileManager;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConfigTestHasDefaultSearchDirectories,
	"AI.Assistant.Config.HasDefaultSearchDirectories",
	AIAssistantTest::Flags);

bool FAIAssistantConfigTestHasDefaultSearchDirectories::RunTest(const FString& UnusedParameters)
{
	const auto DefaultSearchDirectories = FAIAssistantConfig::GetDefaultSearchDirectories();
	(void)TestNotEqual(
		TEXT("NumDefaultSearchDirectories"), DefaultSearchDirectories.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConfigTestFindConfigFileExists,
	"AI.Assistant.Config.FindConfigFileExists",
	AIAssistantTest::Flags);

bool FAIAssistantConfigTestFindConfigFileExists::RunTest(const FString& UnusedParameters)
{
	const FTemporaryDirectory TemporaryDirectoryUnused;  // Should be ignored.
	const FTemporaryDirectory TemporaryDirectoryWithConfig;
	const FString ConfigFilename =
		FPaths::Combine(*TemporaryDirectoryWithConfig, FAIAssistantConfig::DefaultFilename);
	TestTrue(TEXT("WriteConfigFile"), FFileHelper::SaveStringToFile(TEXT(""), *ConfigFilename));
	(void)TestEqual(
		TEXT("FindConfigFileExists"),
		FAIAssistantConfig::FindConfigFile({ *TemporaryDirectoryUnused, *TemporaryDirectoryWithConfig }),
		ConfigFilename);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConfigTestFindConfigFileMissing,
	"AI.Assistant.Config.FindConfigFileMissing",
	AIAssistantTest::Flags);

bool FAIAssistantConfigTestFindConfigFileMissing::RunTest(const FString& UnusedParameters)
{
	const FTemporaryDirectory TemporaryDirectory;
	(void)TestEqual(
		TEXT("FindConfigFileMissing"),
		FAIAssistantConfig::FindConfigFile({ *TemporaryDirectory }),
		"");
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConfigTestLoadDefault,
	"AI.Assistant.Config.LoadDefault",
	AIAssistantTest::Flags);

bool FAIAssistantConfigTestLoadDefault::RunTest(const FString& UnusedParameters)
{
	const FAIAssistantConfig Config = FAIAssistantConfig::Load("");
	(void)TestEqual("main_url", Config.MainUrl, FAIAssistantConfig::DefaultMainUrl);
	const auto AllowedUrlRegexes = Config.GetAllAllowedUrlRegexes();
	
	if (TestEqual(
			TEXT("NumAllowedUrlRegexes"),
			AllowedUrlRegexes.Num(),
			// +1 as the main URL regex is included in the allowed regexes.
			FAIAssistantConfig::DefaultAllowedUrlRegexes.Num() + 1))
	{
		(void)TestEqual(
			TEXT("MainUrlRegex"), AllowedUrlRegexes[0],
			Config.GetMainUrlAsRegexString());
		// +1 to skip the main URL regex.
		for (int i = 1; i < AllowedUrlRegexes.Num(); ++i)
		{
			(void)TestEqual(
				FString::Format(TEXT("allowed_url_regexes[%d]"), { i }),
				FAIAssistantConfig::DefaultAllowedUrlRegexes[i - 1],
				AllowedUrlRegexes[i]);
		}
		// Unfortunately it's not possible to get the original string from a pattern.
		(void)TestEqual(
			TEXT("NumAllowedUrlRegexesMatchesPatterns"),
			Config.GetAllowedUrlRegexPatterns().Num(),
			AllowedUrlRegexes.Num());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConfigTestMainUrlRegex,
	"AI.Assistant.Config.MainUrlRegex",
	AIAssistantTest::Flags);

bool FAIAssistantConfigTestMainUrlRegex::RunTest(const FString& UnusedParameters)
{
	FAIAssistantConfig Config;
	Config.MainUrl = "https://just.a.test/foo/bar";
	(void)TestEqual(TEXT("MainUrlRegxString"), Config.GetMainUrlAsRegexString(),
		TEXT(R"regex(^https://just\.a\.test/foo/bar$)regex"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConfigTestLoadMainUrl,
	"AI.Assistant.Config.LoadMainUrl",
	AIAssistantTest::Flags);

bool FAIAssistantConfigTestLoadMainUrl::RunTest(const FString& UnusedParameters)
{
	const FTemporaryDirectory TemporaryDirectory;
	const FString ConfigFilename(FPaths::Combine(*TemporaryDirectory, FAIAssistantConfig::DefaultFilename));
	verify(
		FFileHelper::SaveStringToFile(
			FString(TEXT(R"json({"main_url": "https://localhost/assistant"})json")),
			*ConfigFilename));
	FAIAssistantConfig Config = FAIAssistantConfig::Load(ConfigFilename);
	(void)TestEqual(TEXT("main_url"), Config.MainUrl, "https://localhost/assistant");
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConfigTestLoadAllowedUrlRegexes,
	"AI.Assistant.Config.LoadAllowedUrlRegexes",
	AIAssistantTest::Flags);

bool FAIAssistantConfigTestLoadAllowedUrlRegexes::RunTest(const FString& UnusedParameters)
{
	const FTemporaryDirectory TemporaryDirectory;
	const FString ConfigFilename(FPaths::Combine(*TemporaryDirectory, FAIAssistantConfig::DefaultFilename));
	verify(
		FFileHelper::SaveStringToFile(
			// NOTE: \\ is unescaped as \ by the JSON parser.
			FString(
				TEXT(R"json({"allowed_url_regexes": ["^https://www\\.epic\\.com/new_login/endpoint.*"]})json")),
			*ConfigFilename));
	FAIAssistantConfig Config = FAIAssistantConfig::Load(ConfigFilename);
	const auto AllowedUrlRegexes = Config.GetAllAllowedUrlRegexes();
	if (TestEqual(
		TEXT("NumAllowedUrlRegexes"),
		AllowedUrlRegexes.Num(),
		// +2 for the main URL regex and the additional regex parsed from the test file.
		FAIAssistantConfig::DefaultAllowedUrlRegexes.Num() + 2))
	{
		(void)TestEqual(
			TEXT("allowed_url_regex"),
			AllowedUrlRegexes.Last(),
			TEXT(R"regex(^https://www\.epic\.com/new_login/endpoint.*)regex"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAIAssistantConfigTestIgnoresAdditionalFields,
	"AI.Assistant.Config.LoadIgnoresAdditionalFields",
	AIAssistantTest::Flags);

bool FAIAssistantConfigTestIgnoresAdditionalFields::RunTest(const FString& UnusedParameters)
{
	const FTemporaryDirectory TemporaryDirectory;
	const FString ConfigFilename(FPaths::Combine(*TemporaryDirectory, FAIAssistantConfig::DefaultFilename));
	verify(
		FFileHelper::SaveStringToFile(
			FString(TEXT(R"json({"ignore_me": "stuff to ignore"})json")),
			*ConfigFilename));
	const FAIAssistantConfig Config = FAIAssistantConfig::Load(ConfigFilename);
	(void)TestEqual(TEXT("main_url"), Config.MainUrl, FAIAssistantConfig::DefaultMainUrl);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
