#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogInstanceMover, Log, All);

/**
 * Editor-only module. It has nothing to register by hand: UInstanceMoverEdMode is a UEdMode subclass, and the
 * asset editor subsystem discovers every non-abstract UEdMode class on its own and adds it to the Modes palette.
 */
class FInstanceMoverModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
