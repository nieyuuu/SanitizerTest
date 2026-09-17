#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "MeshSimilarityAnalyzeCommandlet.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMeshSimilarityAnalyzeCommandlet, All, All);

namespace StaticMeshAnalyzer
{
	enum class EType :uint8;
}

UCLASS()
class UMeshSimilarityAnalyzeCommandlet :public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& InCmdLineParams)override;

private:
	int32 RunBalancerMode(int32 InNumOfBatches, bool InConsiderDiskSize, const TArray<FString>& InDirectoriesToProcess, const FString& InOutputDir);
	int32 RunPreprocessorMode(int32 InQuantizationExponent, const FString& InInputFile, const FString& InOutputFile);
	int32 RunAnalyzerMode(StaticMeshAnalyzer::EType InAnalyzerType, const TArray<FString>& InRegistryPaths, const FString& InOutputFile);
};
