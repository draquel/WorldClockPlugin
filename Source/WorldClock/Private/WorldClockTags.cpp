// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldClockTags.h"

namespace WorldClockTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Time_Phase,       "Time.Phase",       "Root of the day / night phases");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Time_Phase_Dawn,  "Time.Phase.Dawn",  "Twilight before sunrise");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Time_Phase_Day,   "Time.Phase.Day",   "Sun up");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Time_Phase_Dusk,  "Time.Phase.Dusk",  "Twilight after sunset");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Time_Phase_Night, "Time.Phase.Night", "Sun down");
}
