// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class FPS : ModuleRules
{
	public FPS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", 
			"CoreUObject",
			"Engine",
			"InputCore",
			"GameplayTags",
			"PhysicsCore",
			"UMG", "Slate", "SlateCore"
			
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "EnhancedInput" });
		
	}
}
