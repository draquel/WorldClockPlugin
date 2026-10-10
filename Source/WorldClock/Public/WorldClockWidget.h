// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WorldClockWidget.generated.h"

class UTextBlock;

/**
 * Minimal HUD clock built in code (no UMG asset): "Day 1  08:24" with the phase under it. Reads the
 * world clock subsystem each tick. Add it anywhere (the demo controller anchors it top-centre).
 */
UCLASS()
class WORLDCLOCK_API UWorldClockWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The line currently shown (for tests / scripts). */
	UFUNCTION(BlueprintPure, Category = "WorldClock|UI")
	FText GetClockText() const;

	/** Show the phase name under the time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldClock|UI")
	bool bShowPhase = true;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	void BuildWidgetTree();
	void Refresh();

	UPROPERTY()
	TObjectPtr<UTextBlock> ClockText;

	UPROPERTY()
	TObjectPtr<UTextBlock> PhaseText;
};
