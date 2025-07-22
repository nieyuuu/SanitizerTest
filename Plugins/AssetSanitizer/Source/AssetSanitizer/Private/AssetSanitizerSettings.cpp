#include "AssetSanitizerSettings.h"

#if WITH_EDITOR
void UAssetSanitizerSettings::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);
	
	SaveConfig();
}
#endif
