// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

class UButton;
class UCampaignLevelSet;
class UTextBlock;
class UVerticalBox;

/**
 * The front-end menu. Its only real job is letting the player into the single-player campaign - the 1v1
 * ladder has no matchmaking yet, so its entry point only appears when a versus map has been set.
 *
 * The layout is built in C++ rather than authored as a WBP, so the menu exists and works from a compile
 * with no asset to create first. Deriving a WBP from this class still works: RebuildWidget only builds the
 * default layout when the widget tree is empty, which is never true for a Blueprint-authored widget. The
 * BindWidgetOptional handles below are what a WBP would fill in, and every action is BlueprintCallable, so
 * the styling pass can move to UMG later without touching this file.
 */
UCLASS()
class FPS_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	UMainMenuWidget(const FObjectInitializer& ObjectInitializer);

	/** Handed in by AMainMenuGameMode. Without it the menu can still be shown, but the campaign buttons
	 *  have nowhere to send the player and say so. */
	void SetLevelSet(UCampaignLevelSet* InLevelSet);

	/** Opens the campaign at the saved level. */
	UFUNCTION(BlueprintCallable, Category = "FPS|Menu")
	void ContinueCampaign();

	/** Wipes campaign progress and opens the first level. */
	UFUNCTION(BlueprintCallable, Category = "FPS|Menu")
	void StartNewCampaign();

	/** Opens VersusLevel. Does nothing when none is set. */
	UFUNCTION(BlueprintCallable, Category = "FPS|Menu")
	void StartVersus();

	UFUNCTION(BlueprintCallable, Category = "FPS|Menu")
	void QuitGame();

	/** True when there is campaign progress worth resuming - a save sitting on level 0 is not progress. */
	UFUNCTION(BlueprintPure, Category = "FPS|Menu")
	bool HasCampaignProgress() const;

	/** Title shown above the buttons. The working title is still undecided, so it is authored, not baked. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Menu")
	FText TitleText = FText::FromString(TEXT("GUN THIEF"));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Menu")
	FText SubtitleText = FText::FromString(TEXT("MOVEMENT SHOOTER"));

	/** Optional 1v1 map. Left unset the versus button is not built at all, rather than being built and
	 *  disabled - a dead button in a three-item menu reads as a bug. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPS|Menu", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> VersusLevel;

protected:

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Continue;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_NewCampaign;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Versus;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Quit;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Progress;

	UFUNCTION()
	void OnContinueClicked();

	UFUNCTION()
	void OnNewCampaignClicked();

	UFUNCTION()
	void OnVersusClicked();

	UFUNCTION()
	void OnQuitClicked();

private:

	/** Builds the code-authored layout. Only runs when nothing else has built a tree. */
	void BuildDefaultLayout();

	/** One menu button: a coloured UButton wrapping a centred label. */
	UButton* MakeMenuButton(UVerticalBox* Parent, const FText& Label);

	/** Opens the map for LevelIndex, or logs why it cannot. */
	void OpenCampaignLevel(int32 LevelIndex);

	UPROPERTY()
	TObjectPtr<UCampaignLevelSet> LevelSet;
};
