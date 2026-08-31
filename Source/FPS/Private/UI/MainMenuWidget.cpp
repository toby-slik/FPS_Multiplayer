// Copyright Druid Mechanics

#include "UI/MainMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Campaign/CampaignLevelSet.h"
#include "Campaign/CampaignSaveGame.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

namespace MainMenuPalette
{
	// From the GDD art direction: board-formed concrete ground, water teal for interaction, sunlit gold
	// for the title. The menu is the first thing the player sees, so it states the palette rather than
	// defaulting to grey-on-grey.
	static const FLinearColor Background   (0.035f, 0.042f, 0.047f, 1.f);
	static const FLinearColor Title        (0.94f,  0.76f,  0.40f,  1.f);
	static const FLinearColor Subtitle     (0.42f,  0.62f,  0.63f,  1.f);
	static const FLinearColor ButtonNormal (0.055f, 0.145f, 0.160f, 1.f);
	static const FLinearColor ButtonHover  (0.090f, 0.290f, 0.310f, 1.f);
	static const FLinearColor ButtonPress  (0.140f, 0.400f, 0.420f, 1.f);
	static const FLinearColor ButtonLabel  (0.88f,  0.93f,  0.93f,  1.f);
	static const FLinearColor Progress     (0.55f,  0.60f,  0.61f,  1.f);
}

UMainMenuWidget::UMainMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Set here rather than in NativeConstruct: focusability is read when the underlying SWidget is built,
	// which AddToViewport has already done by the time NativeConstruct runs. A non-focusable menu makes
	// FInputModeUIOnly::SetWidgetToFocus fail outright, which leaves input routed nowhere.
	SetIsFocusable(true);
}

TSharedRef<SWidget> UMainMenuWidget::RebuildWidget()
{
	// A Blueprint-authored widget always arrives with a root. Only the pure C++ class does not, and that is
	// exactly the case the code-authored layout is for - so a WBP derived from this class is left alone.
	if (IsValid(WidgetTree) && !IsValid(WidgetTree->RootWidget))
	{
		BuildDefaultLayout();
	}

	return Super::RebuildWidget();
}

void UMainMenuWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	UImage* Backdrop = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetColorAndOpacity(MainMenuPalette::Background);
	if (UCanvasPanelSlot* BackdropSlot = Root->AddChildToCanvas(Backdrop))
	{
		BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackdropSlot->SetOffsets(FMargin(0.f));
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	if (UCanvasPanelSlot* ColumnSlot = Root->AddChildToCanvas(Column))
	{
		// Left-aligned and slightly above centre. A centred stack reads as a placeholder; this sits closer
		// to where the shipping menu should sit against a level backdrop once there is one.
		ColumnSlot->SetAnchors(FAnchors(0.f, 0.5f, 0.f, 0.5f));
		ColumnSlot->SetAlignment(FVector2D(0.f, 0.5f));
		ColumnSlot->SetAutoSize(true);
		ColumnSlot->SetPosition(FVector2D(160.f, -40.f));
	}

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Title"));
	Title->SetText(TitleText);
	Title->SetColorAndOpacity(FSlateColor(MainMenuPalette::Title));
	{
		FSlateFontInfo Font = Title->GetFont();
		Font.Size = 64;
		Font.LetterSpacing = 220;
		Title->SetFont(Font);
	}
	Column->AddChildToVerticalBox(Title);

	UTextBlock* Subtitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Subtitle"));
	Subtitle->SetText(SubtitleText);
	Subtitle->SetColorAndOpacity(FSlateColor(MainMenuPalette::Subtitle));
	{
		FSlateFontInfo Font = Subtitle->GetFont();
		Font.Size = 16;
		Font.LetterSpacing = 600;
		Subtitle->SetFont(Font);
	}
	if (UVerticalBoxSlot* SubtitleSlot = Column->AddChildToVerticalBox(Subtitle))
	{
		SubtitleSlot->SetPadding(FMargin(6.f, 0.f, 0.f, 48.f));
	}

	Button_Continue    = MakeMenuButton(Column, FText::FromString(TEXT("CONTINUE CAMPAIGN")));
	Button_NewCampaign = MakeMenuButton(Column, FText::FromString(TEXT("NEW CAMPAIGN")));

	if (!VersusLevel.IsNull())
	{
		Button_Versus = MakeMenuButton(Column, FText::FromString(TEXT("1V1 MATCH")));
	}

	Button_Quit = MakeMenuButton(Column, FText::FromString(TEXT("QUIT")));

	Text_Progress = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Progress"));
	Text_Progress->SetColorAndOpacity(FSlateColor(MainMenuPalette::Progress));
	{
		FSlateFontInfo Font = Text_Progress->GetFont();
		Font.Size = 14;
		Text_Progress->SetFont(Font);
	}
	if (UVerticalBoxSlot* ProgressSlot = Column->AddChildToVerticalBox(Text_Progress))
	{
		ProgressSlot->SetPadding(FMargin(6.f, 28.f, 0.f, 0.f));
	}
}

UButton* UMainMenuWidget::MakeMenuButton(UVerticalBox* Parent, const FText& Label)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());

	FButtonStyle Style = Button->GetStyle();
	Style.Normal.TintColor  = FSlateColor(MainMenuPalette::ButtonNormal);
	Style.Hovered.TintColor = FSlateColor(MainMenuPalette::ButtonHover);
	Style.Pressed.TintColor = FSlateColor(MainMenuPalette::ButtonPress);
	Button->SetStyle(Style);

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(Label);
	Text->SetColorAndOpacity(FSlateColor(MainMenuPalette::ButtonLabel));
	{
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = 22;
		Font.LetterSpacing = 120;
		Text->SetFont(Font);
	}
	Button->AddChild(Text);

	if (UVerticalBoxSlot* ButtonSlot = Parent->AddChildToVerticalBox(Button))
	{
		ButtonSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	return Button;
}

void UMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(Button_Continue))
	{
		Button_Continue->OnClicked.AddUniqueDynamic(this, &ThisClass::OnContinueClicked);

		// Collapsed rather than disabled: on a fresh install there is nothing to continue, and a greyed
		// first entry is worse than a two-item menu.
		Button_Continue->SetVisibility(HasCampaignProgress() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(Button_NewCampaign))
	{
		Button_NewCampaign->OnClicked.AddUniqueDynamic(this, &ThisClass::OnNewCampaignClicked);
	}
	if (IsValid(Button_Versus))
	{
		Button_Versus->OnClicked.AddUniqueDynamic(this, &ThisClass::OnVersusClicked);
		Button_Versus->SetVisibility(VersusLevel.IsNull() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(Button_Quit))
	{
		Button_Quit->OnClicked.AddUniqueDynamic(this, &ThisClass::OnQuitClicked);
	}

	if (IsValid(Text_Progress))
	{
		const UCampaignSaveGame* Save = UCampaignSaveGame::LoadOrCreate();
		const int32 LevelCount = IsValid(LevelSet) ? LevelSet->GetLevelCount() : 0;

		if (IsValid(Save) && HasCampaignProgress() && LevelCount > 0)
		{
			Text_Progress->SetText(FText::FromString(FString::Printf(
				TEXT("CAMPAIGN  -  LEVEL %d OF %d"),
				FMath::Min(Save->CurrentLevelIndex + 1, LevelCount), LevelCount)));
		}
		else
		{
			Text_Progress->SetText(FText::FromString(TEXT("NO CAMPAIGN PROGRESS")));
		}
	}
}

void UMainMenuWidget::SetLevelSet(UCampaignLevelSet* InLevelSet)
{
	LevelSet = InLevelSet;
}

bool UMainMenuWidget::HasCampaignProgress() const
{
	const UCampaignSaveGame* Save = UCampaignSaveGame::LoadOrCreate();
	return IsValid(Save) && Save->CurrentLevelIndex > 0;
}

void UMainMenuWidget::OnContinueClicked()
{
	ContinueCampaign();
}

void UMainMenuWidget::OnNewCampaignClicked()
{
	StartNewCampaign();
}

void UMainMenuWidget::OnVersusClicked()
{
	StartVersus();
}

void UMainMenuWidget::OnQuitClicked()
{
	QuitGame();
}

void UMainMenuWidget::ContinueCampaign()
{
	const UCampaignSaveGame* Save = UCampaignSaveGame::LoadOrCreate();
	OpenCampaignLevel(IsValid(Save) ? Save->CurrentLevelIndex : 0);
}

void UMainMenuWidget::StartNewCampaign()
{
	if (UCampaignSaveGame* Save = UCampaignSaveGame::LoadOrCreate())
	{
		// Reset and write immediately, so a player who backs out of level 1 does not find the previous
		// run's progress still waiting behind Continue.
		Save->ResetProgress();
		Save->Save();
	}

	OpenCampaignLevel(0);
}

void UMainMenuWidget::StartVersus()
{
	if (VersusLevel.IsNull()) return;

	UGameplayStatics::OpenLevelBySoftObjectPtr(this, VersusLevel);
}

void UMainMenuWidget::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UMainMenuWidget::OpenCampaignLevel(int32 LevelIndex)
{
	if (!IsValid(LevelSet))
	{
		UE_LOG(LogTemp, Error, TEXT("Main menu has no campaign level set; cannot start the campaign"));
		return;
	}

	const int32 Clamped = FMath::Clamp(LevelIndex, 0, LevelSet->GetLevelCount() - 1);
	if (!LevelSet->IsValidLevelIndex(Clamped))
	{
		UE_LOG(LogTemp, Error, TEXT("Campaign level set is empty; cannot start the campaign"));
		return;
	}

	const TSoftObjectPtr<UWorld>& Destination = LevelSet->Levels[Clamped].Level;
	if (Destination.IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT("Campaign level %d has no map assigned"), Clamped);
		return;
	}

	UGameplayStatics::OpenLevelBySoftObjectPtr(this, Destination);
}
