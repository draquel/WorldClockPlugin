// Copyright Daniel Raquel. All Rights Reserved.

using UnrealBuildTool;

public class WorldClock : ModuleRules
{
	public WorldClock(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				// Phases are gameplay tags (Time.Phase.*) so consumers can match without linking us.
				"GameplayTags",
				// The HUD clock widget.
				"UMG",
				// Settings in Project Settings / DefaultGame.ini.
				"DeveloperSettings",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"NetCore",
				"Slate",
				"SlateCore",
			}
		);
	}
}
