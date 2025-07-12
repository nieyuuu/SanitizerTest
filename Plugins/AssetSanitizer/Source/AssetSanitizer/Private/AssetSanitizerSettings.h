#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "Sanitizers/MeshSimilarityAnalyzer.h"

#include "AssetSanitizerSettings.generated.h"

UCLASS(Config = EditorPerProjectUserSettings, Meta = (DisplayName = "AssetSanitizer"))
class UAssetSanitizerSettings :public UDeveloperSettings
{
	GENERATED_BODY()
	
public:
	UPROPERTY(Config, EditAnywhere, Category = "Mesh Similarity Analyze | Balancer", Meta = (DisplayName = "Consider Disk Size"))
	bool bConsiderDiskSize;

	UPROPERTY(Config, EditAnywhere, Category = "Mesh Similarity Analyze | Balancer", Meta = (DisplayName = "Paths to Analyze"))
	TSet<FString> PathsToAnalyze;

	UPROPERTY(Config, EditAnywhere, Category = "Mesh Similarity Analyze | Balancer", Meta = (DisplayName = "Number of Batches", ClipMin = MIN_NUM_OF_BATCHES))
	int32 NumOfBatches;

	UPROPERTY(Config, EditAnywhere, Category = "Mesh Similarity Analyze | Balancer", Meta = (DisplayName = "Balancer Output Directory"))
	FDirectoryPath BalancerOutputDir;

	UPROPERTY(Config, EditAnywhere, Category = "Mesh Similarity Analyze | Preprocessor", Meta = (DisplayName = "Quantization Exponent", ClipMin = MIN_EXPONENT, ClipMax = MAX_EXPONENT))
	int32 QuantizationExponent;

	UPROPERTY(Config, EditAnywhere, Category = "Mesh Similarity Analyze | Analyzer", Meta = (DisplayName = "Analyzer Type"))
	EAnalyzerType AnalyzerType;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif

	virtual FName GetCategoryName()const
	{
		return FName(TEXT("Plugins"));
	}
};
