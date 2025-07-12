#include "AssetSanitizerSettings.h"

#if WITH_EDITOR
void UAssetSanitizerSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	SaveConfig();
}
#endif

