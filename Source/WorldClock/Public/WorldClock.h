// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

WORLDCLOCK_API DECLARE_LOG_CATEGORY_EXTERN(LogWorldClock, Log, All);

class FWorldClockModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
