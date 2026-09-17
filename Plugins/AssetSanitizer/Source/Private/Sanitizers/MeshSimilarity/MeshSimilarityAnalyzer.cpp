#include "MeshSimilarityAnalyzer.h"

namespace StaticMeshAnalyzer
{
	FString TypeToString(EType InType)
	{
		switch (InType)
		{
		case EType::PerVertex:
			return TEXT("PerVertex");
		case EType::XxHash64:
			return TEXT("XxHash64");
		case EType::XxHash128:
			return TEXT("XxHash128");
		default:
			check(false);
			break;
		}

		return TEXT("");
	}

	bool FAnalyzeResults::SaveTo(FAnalyzeResults& InResults, const FString& InSaveFileName)
	{
		if (!InSaveFileName.EndsWith(".bin"))
		{
			return false;
		}

		TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*InSaveFileName));
		if (FileWriter.Get() == nullptr)
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to create file writer when saving static mesh analyze results to %s"), *InSaveFileName);
			return false;
		}

		*FileWriter << InResults;
		if (!FileWriter->Close())
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to save static mesh analyze results to %s"), *InSaveFileName);
			return false;
		}

		return true;
	}

	bool FAnalyzeResults::LoadFrom(FAnalyzeResults& InResults, const FString& InLoadFileName)
	{
		if (!InLoadFileName.EndsWith(TEXT(".bin")))
		{
			return false;
		}

		if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*InLoadFileName))
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Bin file %s does not exist."), *InLoadFileName);
			return false;
		}

		TUniquePtr<FArchive> FileReader = TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*InLoadFileName));
		if (FileReader.Get() == nullptr)
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to create file reader when loading static mesh analyze results from %s"), *InLoadFileName);
			return false;
		}

		*FileReader << InResults;
		if (!FileReader->Close())
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to load static mesh analyze results from %s"), *InLoadFileName);
			return false;
		}

		return true;
	}

	bool ISimilarityAnalyzer::Analyzes(const TArray<StaticMeshPreprocessor::FRegistry*>& InRegistries, FAnalyzeResults& OutResults)const
	{
		if (InRegistries.Num() == 0)
		{
			UE_LOG(LogAssetSanitizer, Warning, TEXT("No input registry"));
			return true;
		}

		TMap<int32, TArray<const StaticMeshPreprocessor::FStaticMesh*>> ClassifiedStaticMeshes;
		if (!ClassifyStaticMeshes(InRegistries, ClassifiedStaticMeshes))
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to classify static meshes"));
			return false;
		}

		const double StartTime = FPlatformTime::Seconds();

		TArray<TArray<const StaticMeshPreprocessor::FStaticMesh*>> Results;
		for (auto Iterator = ClassifiedStaticMeshes.CreateConstIterator(); Iterator; ++Iterator)
		{
			AnalyzesSubset(Iterator->Value, Results);
		}

		//Assemble final results
		OutResults.QuantizationExponent = InRegistries[0]->GetQuantizationExponent();
		OutResults.ObjectPathToErrorStatus.Empty();
		for (const StaticMeshPreprocessor::FRegistry* Registry : InRegistries)
		{
			OutResults.ObjectPathToErrorStatus.Append(Registry->GetObjectPathToErrorStatus());
		}
		OutResults.SimilarGroups.Empty(Results.Num());
		for (const TArray<const StaticMeshPreprocessor::FStaticMesh*>& SubResult : Results)
		{
			FSimilarGroup SimilarGroup;
			check(SubResult.Num() >= 2);
			SimilarGroup.NumOfVertices = SubResult[0]->GetQuantizedPositionBuffer().Num();

			SimilarGroup.StaticMeshes.Reserve(SubResult.Num());
			for (const StaticMeshPreprocessor::FStaticMesh* StaticMesh : SubResult)
			{
				SimilarGroup.StaticMeshes.Add(StaticMesh->GetStaticMeshObjectPath());
			}

			OutResults.SimilarGroups.Add(MoveTemp(SimilarGroup));
		}

		const double EndTime = FPlatformTime::Seconds();

		UE_LOG(LogAssetSanitizer, Display, TEXT("Finish analyzing static meshe similarity in %f seconds."), EndTime - StartTime);

		return true;
	}

	bool ISimilarityAnalyzer::ClassifyStaticMeshes(const TArray<StaticMeshPreprocessor::FRegistry*>& InRegistries, TMap<int32, TArray<const StaticMeshPreprocessor::FStaticMesh*>>& OutClassifiedStaticMeshes)const
	{
		OutClassifiedStaticMeshes.Empty();

		TSet<FString> UniqueObjectPathSet;
		TSet<int32> UniqueExponentSet;

		for (const StaticMeshPreprocessor::FRegistry* Registry : InRegistries)
		{
			//All pointers need to be valid
			if (Registry == nullptr)
			{
				UE_LOG(LogAssetSanitizer, Error, TEXT("Found a null registry pointer during classify static meshes"));
				return false;
			}

			UniqueExponentSet.Add(Registry->GetQuantizationExponent());

			const TMap<FString, StaticMeshPreprocessor::EStatus>& ObjectPathToErrorStatus = Registry->GetObjectPathToErrorStatus();
			const TArray<StaticMeshPreprocessor::FStaticMesh*>& ProcessedStaticMeshes = Registry->GetProcessedStaticMeshes();
			for (const TPair<FString, StaticMeshPreprocessor::EStatus>& KeyValuePair : ObjectPathToErrorStatus)
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
			for (const StaticMeshPreprocessor::FStaticMesh* StaticMesh : ProcessedStaticMeshes)
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
			for (int i = 0; i < InRegistries.Num(); ++i)
			{
				UE_LOG(LogAssetSanitizer, Error, TEXT("Registry [%d], exponent: [%d]."), i, InRegistries[i]->GetQuantizationExponent());
			}
			return false;
		}

		//Zero NumOfVertices of static mesh found
		if (OutClassifiedStaticMeshes.Contains(0))
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Zero Number of Vertices Detected"));
			return false;
		}

		OutClassifiedStaticMeshes = OutClassifiedStaticMeshes.FilterByPredicate([](const TPair<int32, TArray<const StaticMeshPreprocessor::FStaticMesh*>>& InKeyValuePair) {
			return InKeyValuePair.Value.Num() >= 2;
			});

		return true;
	}

	void FPerVertexAnalyzer::AnalyzesSubset(const TArray<const StaticMeshPreprocessor::FStaticMesh*>& InStaticMeshSubset, TArray<TArray<const StaticMeshPreprocessor::FStaticMesh*>>& OutResults)const
	{
		TArray<TArray<const StaticMeshPreprocessor::FStaticMesh*>> SubResult;

		TSet<const StaticMeshPreprocessor::FStaticMesh*> ProcessedIdenticalStaticMeshes;
		for (int i = 0; i < InStaticMeshSubset.Num(); ++i)
		{
			if (ProcessedIdenticalStaticMeshes.Contains(InStaticMeshSubset[i]))
			{
				continue;
			}

			TArray<const StaticMeshPreprocessor::FStaticMesh*> SimilarGroup;
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
				for (const StaticMeshPreprocessor::FStaticMesh* Mesh : SimilarGroup)
				{
					ProcessedIdenticalStaticMeshes.Add(Mesh);
				}
				SubResult.Add(MoveTemp(SimilarGroup));
			}
		}

		OutResults.Append(MoveTemp(SubResult));
	}

	bool FPerVertexAnalyzer::PerVertexCompareStaticMeshes(const StaticMeshPreprocessor::FStaticMesh* A, const StaticMeshPreprocessor::FStaticMesh* B)const
	{
		const TArray<FInt64Vector3>& PositionBufferA = A->GetQuantizedPositionBuffer();
		const TArray<FInt64Vector3>& PositionBufferB = B->GetQuantizedPositionBuffer();

		const int32 NumberOfVertexA = PositionBufferA.Num();
		const int32 NumberOfVertexB = PositionBufferB.Num();
		check(NumberOfVertexA == NumberOfVertexB);

		//Bounding box test first since they are most likly to be different
		const FBox& BoundingBoxA = A->GetBoundingBox();
		const FBox& BoundingBoxB = B->GetBoundingBox();
		if (!BoundingBoxA.Equals(BoundingBoxB))
		{
			return false;
		}
		else
		{
			UE_LOG(LogAssetSanitizer, VeryVerbose, TEXT("Static mesh [%s] and [%s] are potentially identical because their bounding boxes are equal."),
				*A->GetStaticMeshObjectPath(), *B->GetStaticMeshObjectPath());
		}

		//Do a random position test
		const int32 RandomIndex = FMath::RandRange(0, NumberOfVertexA - 1);
		if (PositionBufferA[RandomIndex] != PositionBufferB[RandomIndex])
		{
			return false;
		}
		else
		{
			UE_LOG(LogAssetSanitizer, VeryVerbose, TEXT("Static mesh [%s] and [%s] are potentially identical because random position test (%lld,%lld,%lld) == (%lld,%lld,%lld)."),
				*A->GetStaticMeshObjectPath(), *B->GetStaticMeshObjectPath(),
				PositionBufferA[RandomIndex].X, PositionBufferA[RandomIndex].Y, PositionBufferA[RandomIndex].Z,
				PositionBufferB[RandomIndex].X, PositionBufferB[RandomIndex].Y, PositionBufferB[RandomIndex].Z);
		}

		std::atomic<bool> bIdentical = true;

		auto LoopBody = [&](int32 Index) {
			FTaskTagScope TaskTag(ETaskTag::EParallelGameThread);
			if (bIdentical.load(std::memory_order_relaxed))
			{
				if (PositionBufferA[Index] != PositionBufferB[Index])
				{
					UE_LOG(LogAssetSanitizer, VeryVerbose, TEXT("Static mesh [%s] and [%s] are different because position test (%lld,%lld,%lld) != (%lld,%lld,%lld)."),
						*A->GetStaticMeshObjectPath(), *B->GetStaticMeshObjectPath(),
						PositionBufferA[Index].X, PositionBufferA[Index].Y, PositionBufferA[Index].Z,
						PositionBufferB[Index].X, PositionBufferB[Index].Y, PositionBufferB[Index].Z);
					bIdentical.store(false, std::memory_order_relaxed);
				}
			}
			};

		ParallelFor(
			TEXT("ParallelPerVertexCompareStaticMeshes"),
			NumberOfVertexA,
			4096,
			MoveTemp(LoopBody),
			EParallelForFlags::Unbalanced
		);

		return bIdentical;
	}

	bool AnalyzeMeshSimilarity(EType InAnalyzerType, const TArray<StaticMeshPreprocessor::FRegistry*>& InRegistries, FAnalyzeResults& OutResults)
	{
		ISimilarityAnalyzer* MeshSimilarityAnalyzer = nullptr;

		switch (InAnalyzerType)
		{
		case EType::PerVertex:
			MeshSimilarityAnalyzer = new FPerVertexAnalyzer();
			break;
		case EType::XxHash64:
			MeshSimilarityAnalyzer = new TMemoryHashAnalyzer<FXxHash64>();
			break;
		case EType::XxHash128:
			MeshSimilarityAnalyzer = new TMemoryHashAnalyzer<FXxHash128>();
			break;
		default:
			check(0);
			break;
		}

		struct FScopeGuard
		{
			FScopeGuard(ISimilarityAnalyzer* InMeshSimilarityAnalyzer)
			{
				MeshSimilarityAnalyzer = InMeshSimilarityAnalyzer;
			}
			~FScopeGuard()
			{
				if (MeshSimilarityAnalyzer)
				{
					delete MeshSimilarityAnalyzer;
				}
			}

			ISimilarityAnalyzer* MeshSimilarityAnalyzer;
		};

		FScopeGuard Guard(MeshSimilarityAnalyzer);

		if (!MeshSimilarityAnalyzer->Analyzes(InRegistries, OutResults))
		{
			return false;
		}

		return true;
	}
}
