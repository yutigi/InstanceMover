#include "InstanceMoverModule.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogInstanceMover);

#pragma region Lifecycle

void FInstanceMoverModule::StartupModule()
{
}

void FInstanceMoverModule::ShutdownModule()
{
}

#pragma endregion

IMPLEMENT_MODULE(FInstanceMoverModule, InstanceMover)
