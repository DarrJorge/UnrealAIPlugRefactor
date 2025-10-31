// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AIAssistant : ModuleRules
{
	public AIAssistant(ReadOnlyTargetRules Target) : base(Target)
	{
		// For IWYU audits..
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;
		bUseUnity = true;
		
		PublicDefinitions.Add("WITH_AIASSISTANT_EPIC_INTERNAL=1"); 
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
			);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"InputCore",
				"EditorFramework",
				"UnrealEd",
				"ToolMenus",
				"CoreUObject",
				"Engine",
				"DeveloperSettings",
				"Json",
				"JsonUtilities",
				"EditorSubsystem",
				"HTTP",
                "EditorScriptingUtilities",
                "Kismet",
                "KismetCompiler",
                "BlueprintGraph",
                "GraphEditor",
                "PropertyEditor",
				"UMG",
				"ContentBrowser",
				"AssetRegistry",
				"LevelEditor",
				"PythonScriptPlugin",
				"Slate",
				"SlateCore",
				"ApplicationCore",
				"WebBrowser"
			}
			);
	}
}
