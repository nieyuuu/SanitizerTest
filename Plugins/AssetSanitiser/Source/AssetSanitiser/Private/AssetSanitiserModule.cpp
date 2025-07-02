#include "AssetSanitiserModule.h"
#include "TestCode.h"

#define LOCTEXT_NAMESPACE "FAssetSanitiserModule"

void FAssetSanitiserModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	GetAllStaticMeshes();
}

void FAssetSanitiserModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssetSanitiserModule, AssetSanitiser)