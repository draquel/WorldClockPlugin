// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldClockWidget.h"
#include "WorldClockSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/Pawn.h"

void UWorldClockWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UWorldClockWidget::BuildWidgetTree()
{
	if (!WidgetTree || ClockText)
	{
		return;
	}
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ClockRoot"));
	WidgetTree->RootWidget = Root;

	auto MakeLine = [&](const TCHAR* Name, int32 FontSize, const FLinearColor& Color) -> UTextBlock*
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FontSize;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetJustification(ETextJustify::Center);
		Text->SetShadowOffset(FVector2D(1.f, 1.f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
		if (UVerticalBoxSlot* LineSlot = Root->AddChildToVerticalBox(Text))
		{
			LineSlot->SetHorizontalAlignment(HAlign_Center);
		}
		return Text;
	};
	ClockText = MakeLine(TEXT("ClockText"), 18, FLinearColor(0.95f, 0.95f, 0.9f));
	PhaseText = MakeLine(TEXT("PhaseText"), 11, FLinearColor(0.8f, 0.85f, 0.95f));
	Refresh();
}

void UWorldClockWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

void UWorldClockWidget::Refresh()
{
	const UWorldClockSubsystem* Clock = UWorldClockSubsystem::Get(this);
	if (!ClockText)
	{
		return;
	}
	if (!Clock)
	{
		ClockText->SetText(FText::GetEmpty());
		if (PhaseText) { PhaseText->SetText(FText::GetEmpty()); }
		return;
	}
	// On a planet the local solar time where the player stands (spherical Phase 4, D12).
	const APawn* Pawn = Clock->HasPlanetFrame() ? GetOwningPlayerPawn() : nullptr;
	if (Pawn)
	{
		const FVector Here = Pawn->GetActorLocation();
		ClockText->SetText(FText::Format(NSLOCTEXT("WorldClock", "ClockLine", "Day {0}  {1}"), Clock->GetLocalDay(Here), Clock->GetLocalTimeText(Here)));
	}
	else
	{
		ClockText->SetText(FText::Format(NSLOCTEXT("WorldClock", "ClockLine", "Day {0}  {1}"), Clock->GetDay(), Clock->GetTimeText()));
	}
	if (PhaseText)
	{
		const EWorldClockPhase Phase = Pawn ? Clock->GetLocalPhase(Pawn->GetActorLocation()) : Clock->GetPhase();
		PhaseText->SetText(bShowPhase ? FText::FromString(UEnum::GetDisplayValueAsText(Phase).ToString()) : FText::GetEmpty());
		PhaseText->SetVisibility(bShowPhase ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

FText UWorldClockWidget::GetClockText() const
{
	return ClockText ? ClockText->GetText() : FText::GetEmpty();
}
