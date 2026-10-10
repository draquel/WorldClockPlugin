// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/** Native gameplay tags for the world clock. Phase tags are what other systems match against. */
namespace WorldClockTags
{
	WORLDCLOCK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Time_Phase);
	WORLDCLOCK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Time_Phase_Dawn);
	WORLDCLOCK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Time_Phase_Day);
	WORLDCLOCK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Time_Phase_Dusk);
	WORLDCLOCK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Time_Phase_Night);
}
