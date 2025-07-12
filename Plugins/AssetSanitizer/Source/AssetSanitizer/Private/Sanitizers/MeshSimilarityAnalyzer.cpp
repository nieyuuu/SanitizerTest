#include "MeshSimilarityAnalyzer.h"

#include "JsonObjectConverter.h"

bool FAnalyzeResults::SaveTo(FAnalyzeResults& InResults, const FString& InSaveFileName)
{
	if (!InSaveFileName.EndsWith(".json"))
	{
		return false;
	}

	FString JsonString;
	if (!FJsonObjectConverter::UStructToFormattedJsonObjectString<TCHAR, TPrettyJsonPrintPolicy>(FAnalyzeResults::StaticStruct(), &InResults, JsonString))
	{
		UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to convert analyze results to json string."));
		return false;
	}

	if (!FFileHelper::SaveStringToFile(JsonString, *InSaveFileName))
	{
		UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to save analyze results to [%s]."), *InSaveFileName);
		return false;
	}

	return true;
}

bool FAnalyzeResults::LoadFrom(FAnalyzeResults& InResults, const FString& InLoadFileName)
{
	if (!InLoadFileName.EndsWith(TEXT(".json")))
	{
		return false;
	}

	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*InLoadFileName))
	{
		UE_LOG(LogAssetSanitizer, Error, TEXT("Json file %s does not exist."), *InLoadFileName);
		return false;
	}

	TUniquePtr<FArchive> JsonFileReader = TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*InLoadFileName));
	if (JsonFileReader.Get() == nullptr)
	{
		UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to create file reader when loading analyze results from %s"), *InLoadFileName);
		return false;
	}

	FString LoadedJsonString;
	if (!FFileHelper::LoadFileToString(LoadedJsonString, *JsonFileReader.Get()))
	{
		UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to load analyze results from [%s]."), *InLoadFileName);
		return false;
	}

	if (!FJsonObjectConverter::JsonObjectStringToUStruct(LoadedJsonString, &InResults))
	{
		UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to convert json string to analyze results."));
		return false;
	}

	return true;
}

namespace Analyzer
{
	bool IMeshSimilarityAnalyzer::Analyzes(const TSet<FPreprocessRegistry*>& InRegistries, FAnalyzeResults& OutResults)const
	{
		if (InRegistries.Num() == 0)
		{
			UE_LOG(LogAssetSanitizer, Warning, TEXT("Empty registry set"));
			return true;
		}

		TMap<int32, TArray<const FPreprocessedStaticMesh*>> ClassifiedStaticMeshes;
		if (!ClassifyStaticMeshes(InRegistries, ClassifiedStaticMeshes))
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to classify static meshes"));
			return false;
		}

		const double StartTime = FPlatformTime::Seconds();

		TArray<TArray<const FPreprocessedStaticMesh*>> Results;
		for (auto Iterator = ClassifiedStaticMeshes.CreateConstIterator(); Iterator; ++Iterator)
		{
			AnalyzesSubset(Iterator->Value, Results);
		}
		
		//Assemble final results
		OutResults.QuantizationExponent = (*InRegistries.begin())->GetQuantizationExponent();
		OutResults.ObjectPathToErrorStatus.Empty();
		for (const FPreprocessRegistry* Registry : InRegistries)
		{
			OutResults.ObjectPathToErrorStatus.Append(Registry->GetObjectPathToErrorStatus());
		}
		OutResults.SimilarGroups.Empty(Results.Num());
		for (const TArray<const FPreprocessedStaticMesh*>& SubResult : Results)
		{
			FSimilarGroup SimilarGroup;
			check(SubResult.Num() >= 2);
			SimilarGroup.NumOfVertices = SubResult[0]->GetQuantizedPositionBuffer().Num();
			
			SimilarGroup.SimilarStaticMeshes.Reserve(SubResult.Num());
			for (const FPreprocessedStaticMesh* StaticMesh : SubResult)
			{
				SimilarGroup.SimilarStaticMeshes.Add(FSoftObjectPath(StaticMesh->GetStaticMeshObjectPath()));
			}

			OutResults.SimilarGroups.Add(MoveTemp(SimilarGroup));
		}

		const double EndTime = FPlatformTime::Seconds();

		UE_LOG(LogAssetSanitizer, Display, TEXT("Finish analyzing static meshe similarity in %f seconds."), EndTime - StartTime);

		return true;
	}

	bool IMeshSimilarityAnalyzer::ClassifyStaticMeshes(const TSet<FPreprocessRegistry*>& InRegistries, TMap<int32, TArray<const FPreprocessedStaticMesh*>>& OutClassifiedStaticMeshes)const
	{
		OutClassifiedStaticMeshes.Empty();

		TSet<FString> UniqueObjectPathSet;
		TSet<int32> UniqueExponentSet;

		for (const FPreprocessRegistry* Registry : InRegistries)
		{
			//All pointers need to be valid
			if (Registry == nullptr)
			{
				UE_LOG(LogAssetSanitizer, Error, TEXT("Found a null registry pointer during classify static meshes"));
				return false;
			}

			UniqueExponentSet.Add(Registry->GetQuantizationExponent());

			const TMap<FString, EPreprocessStatus>& ObjectPathToErrorStatus = Registry->GetObjectPathToErrorStatus();
			const TArray<FPreprocessedStaticMesh*>& ProcessedStaticMeshes = Registry->GetProcessedStaticMeshes();
			for (const TPair<FString, EPreprocessStatus>& KeyValuePair : ObjectPathToErrorStatus)
			{
				if (UniqueObjectPathSet.Contains(KeyValuePair.Key))
				{
					UE_LOG(LogAssetSanitizer, Error, TEXT("Duplicated element [%s] detected during classify static meshes"), *KeyValuePair.Key);
					return false;
				}
				else
				{
					UniqueObjectPathSet.Add(KeyValuePair.Key);
				}
			}
			for (const FPreprocessedStaticMesh* StaticMesh : ProcessedStaticMeshes)
			{
				if (UniqueObjectPathSet.Contains(StaticMesh->GetStaticMeshObjectPath()))
				{
					UE_LOG(LogAssetSanitizer, Error, TEXT("Duplicated element [%s] detected during classify static meshes"), *StaticMesh->GetStaticMeshObjectPath());
					return false;
				}
				else
				{
					UniqueObjectPathSet.Add(StaticMesh->GetStaticMeshObjectPath());
					OutClassifiedStaticMeshes.FindOrAdd(StaticMesh->GetQuantizedPositionBuffer().Num()).Add(StaticMesh);
				}
			}
		}

		if (UniqueExponentSet.Num() > 1)
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Detected %d different quantization exponents in %d registries. It is expected all registries have same quantization exponent."), UniqueExponentSet.Num(), InRegistries.Num());
			for (const FPreprocessRegistry* Registry : InRegistries)
			{
				UE_LOG(LogAssetSanitizer, Error, TEXT("Registry file name: [%s], exponent: [%d]."), *(Registry->GetFileName().ToString()), Registry->GetQuantizationExponent());
			}
			return false;
		}

		//Zero NumOfVertices of static mesh found
		if (OutClassifiedStaticMeshes.Contains(0))
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Zero Number of Vertices Detected"));
			return false;
		}

		OutClassifiedStaticMeshes = OutClassifiedStaticMeshes.FilterByPredicate([](const TPair<int32, TArray<const FPreprocessedStaticMesh*>>& InKeyValuePair) {
			return InKeyValuePair.Value.Num() >= 2;
		});

		return true;
	}

	void FPerVertexAnalyzer::AnalyzesSubset(const TArray<const FPreprocessedStaticMesh*>& InStaticMeshSubset, TArray<TArray<const FPreprocessedStaticMesh*>>& OutResults)const
	{
		TArray<TArray<const FPreprocessedStaticMesh*>> SubResult;

		TSet<const FPreprocessedStaticMesh*> ProcessedIdenticalStaticMeshes;
		for (int i = 0; i < InStaticMeshSubset.Num(); ++i)
		{
			if (ProcessedIdenticalStaticMeshes.Contains(InStaticMeshSubset[i]))
			{
				continue;
			}

			TArray<const FPreprocessedStaticMesh*> SimilarGroup;
			SimilarGroup.Add(InStaticMeshSubset[i]);

			for (int j = i + 1; j < InStaticMeshSubset.Num(); ++j)
			{
				if (ProcessedIdenticalStaticMeshes.Contains(InStaticMeshSubset[j]))
				{
					continue;
				}

				if (PerVertexCompareStaticMeshes(InStaticMeshSubset[i], InStaticMeshSubset[j]))
				{
					SimilarGroup.Add(InStaticMeshSubset[j]);
				}
			}

			if (SimilarGroup.Num() > 1)
			{
				for (const FPreprocessedStaticMesh* Mesh : SimilarGroup)
				{
					ProcessedIdenticalStaticMeshes.Add(Mesh);
				}
				SubResult.Add(MoveTemp(SimilarGroup));
			}
		}

		OutResults.Append(MoveTemp(SubResult));
	}

	bool FPerVertexAnalyzer::PerVertexCompareStaticMeshes(const FPreprocessedStaticMesh* A, const FPreprocessedStaticMesh* B)const
	{
		const TArray<FPreprocessedPosition>& PositionBufferA = A->GetQuantizedPositionBuffer();
		const TArray<FPreprocessedPosition>& PositionBufferB = B->GetQuantizedPositionBuffer();

		const int32 NumberOfVertexA = PositionBufferA.Num();
		const int32 NumberOfVertexB = PositionBufferB.Num();

		//Bounding box test first since they are most likly to be different
		const FBox& BoundingBoxA = A->GetBoundingBox();
		const FBox& BoundingBoxB = B->GetBoundingBox();
		if (!BoundingBoxA.Equals(BoundingBoxB))
		{
			/*UE_LOG(LogAssetSanitizer, VeryVerbose, TEXT("Static Mesh [%s] and [%s] are different because bounding boxes not matching."),
				*A->GetStaticMeshObjectPath(), *B->GetStaticMeshObjectPath());*/
			return false;
		}

		//Do a random position test
		const int32 RandomIndex = FMath::RandRange(0, NumberOfVertexA - 1);
		if (PositionBufferA[RandomIndex] != PositionBufferB[RandomIndex])
		{
			/*UE_LOG(LogAssetSanitizer, VeryVerbose, TEXT("Static Mesh [%s] and [%s] are different because random position test (%lld,%lld,%lld) != (%lld,%lld,%lld)."),
				*A->GetStaticMeshObjectPath(), *B->GetStaticMeshObjectPath(),
				PositionBufferA[RandomIndex].X, PositionBufferA[RandomIndex].Y, PositionBufferA[RandomIndex].Z,
				PositionBufferB[RandomIndex].X, PositionBufferB[RandomIndex].Y, PositionBufferB[RandomIndex].Z);*/
			return false;
		}

		struct FContext
		{
			bool bIdenticalInThisContext = true;
		};

		auto LoopBody = [&](FContext& Context, int32 Index) {
			FTaskTagScope TaskTag(ETaskTag::EParallelGameThread);
			if (Context.bIdenticalInThisContext == false)
			{
				return;
			}

			if (PositionBufferA[Index] != PositionBufferB[Index])
			{
				/*UE_LOG(LogAssetSanitizer, VeryVerbose, TEXT("Static Mesh [%s] and [%s] are different because position test (%lld,%lld,%lld) != (%lld,%lld,%lld)."),
					*A->GetStaticMeshObjectPath(), *B->GetStaticMeshObjectPath(),
					PositionBufferA[Index].X, PositionBufferA[Index].Y, PositionBufferA[Index].Z,
					PositionBufferB[Index].X, PositionBufferB[Index].Y, PositionBufferB[Index].Z);*/
				Context.bIdenticalInThisContext = false;
			}
		};

		TArray<FContext> Contexts;
		ParallelForWithTaskContext(
			TEXT("ParallelPerVertexCompareStaticMeshes"),
			Contexts,
			NumberOfVertexA,
			1024,
			MoveTemp(LoopBody),
			EParallelForFlags::Unbalanced
		);

		bool bIdenticalPositionBuffer = true;
		for (const FContext& Context : Contexts)
		{
			if (Context.bIdenticalInThisContext == false)
			{
				bIdenticalPositionBuffer = false;
			}

			if (bIdenticalPositionBuffer == false)
				break;
		}

		return bIdenticalPositionBuffer;
	}
}
