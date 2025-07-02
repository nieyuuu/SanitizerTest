#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PreprocessStaticMeshesCommandlet.generated.h"

UCLASS()
class UPreprocessStaticMeshesCommandlet : public UCommandlet
{
	GENERATED_BODY()
	
public:
	virtual int32 Main(const FString& CmdLineParams) override;
};
