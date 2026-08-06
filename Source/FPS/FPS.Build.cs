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
			"UMG", "Slate", "SlateCore",

			// Enemy AI. AIModule brings AAIController and path following; NavigationSystem is needed for the
			// reachability and random-reachable-point queries the bot uses to pick reposition targets;
			// GameplayTasks is a hard dependency of AIModule's move requests.
			"AIModule", "NavigationSystem", "GameplayTasks"

		});

		PrivateDependencyModuleNames.AddRange(new string[] { "EnhancedInput" });
		
	}
}
