#pragma once

#include "LogAssetSanitiser.h"
#include "PreprocessStaticMeshes.h"

//Forward declare the friend functions
uint32 GetTypeHash(const FXxHash64& InHash);
uint32 GetTypeHash(const FXxHash128& InHash);

namespace Analyzer
{
	enum class EAnalyzerType :uint8
	{
		PerVertex,
		XxHash64,
		XxHash128
	};

	class IMeshSimilarityAnalyzer
	{
	public:
		IMeshSimilarityAnalyzer(const TSet<FPreprocessRegistry*>& InRegistries) :Registries(InRegistries) {}
		
		virtual ~IMeshSimilarityAnalyzer() = default;

		void Analyzes(TArray<TArray<FString>>& OutResults)
		{
			TArray<TArray<const FPreprocessedStaticMesh*>> Results;
			Analyzes(Results);

			OutResults.Empty(Results.Num());

			for (const TArray<const FPreprocessedStaticMesh*>& SubResult : Results)
			{
				TArray<FString> SimilarGroup;
				SimilarGroup.Reserve(SubResult.Num());

				for (const FPreprocessedStaticMesh* StaticMesh : SubResult)
				{
					SimilarGroup.Add(StaticMesh->GetStaticMeshObjectPath());
				}

				OutResults.Add(MoveTemp(SimilarGroup));
			}
		}
		
		void Analyzes(TArray<TArray<const FPreprocessedStaticMesh*>>& OutResults)
		{
			OutResults.Empty();

			UE_LOG(LogAssetSanitiser, Display, TEXT("Start analyzing static meshe similarity."));
			
			const double StartTime = FPlatformTime::Seconds();

			TMap<int32, TArray<const FPreprocessedStaticMesh*>> ClassifiedStaticMeshes;
			if (!ClassifyStaticMeshes(ClassifiedStaticMeshes))
			{
				UE_LOG(LogAssetSanitiser, Warning, TEXT("Failed to classify static meshes"));
				return;
			}
			
			for (auto Iterator = ClassifiedStaticMeshes.CreateConstIterator(); Iterator; ++Iterator)
			{
				AnalyzesSubset(Iterator->Value, OutResults);
			}

			const double EndTime = FPlatformTime::Seconds();

			UE_LOG(LogAssetSanitiser, Display, TEXT("Finish analyzing static meshe similarity in %f seconds."), EndTime - StartTime);
		}

	protected:
		bool ClassifyStaticMeshes(TMap<int32, TArray<const FPreprocessedStaticMesh*>>& OutClassifiedStaticMeshes)const
		{
			OutClassifiedStaticMeshes.Empty();

			TSet<FString> UniqueObjectPathSet;
			TSet<uint32> UniqueExponentSet;

			for (const FPreprocessRegistry* Registry : Registries)
			{
				if (Registry == nullptr)
					continue;

				UniqueExponentSet.Add(Registry->GetQuantizationExponent());

				const TMap<FString, EPreprocessStatus>& ObjectPathToErrorStatus = Registry->GetObjectPathToErrorStatus();
				const TArray<FPreprocessedStaticMesh*>& ProcessedStaticMeshes = Registry->GetProcessedStaticMeshes();

				for (const TPair<FString, EPreprocessStatus>& KeyValuePair : ObjectPathToErrorStatus)
				{
					if (UniqueObjectPathSet.Contains(KeyValuePair.Key))
					{
						UE_LOG(LogAssetSanitiser, Warning, TEXT("Duplicated element [%s] detected during classify static meshes"), *KeyValuePair.Key);
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
						UE_LOG(LogAssetSanitiser, Warning, TEXT("Duplicated element [%s] detected during classify static meshes"), *StaticMesh->GetStaticMeshObjectPath());
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
				UE_LOG(LogAssetSanitiser, Warning,
					TEXT("Detected %d different quantization exponent in %d registries. It is expected all registries have same quantization exponent."),
					UniqueExponentSet.Num(), Registries.Num());
				return false;
			}

			OutClassifiedStaticMeshes = OutClassifiedStaticMeshes.FilterByPredicate([](const TPair<int32, TArray<const FPreprocessedStaticMesh*>>& InKeyValuePair) {
				checkf(InKeyValuePair.Key > 0, TEXT("Zero number of vertices detected."));
				return InKeyValuePair.Value.Num() >= 2;
			});

			return true;
		}

		virtual void AnalyzesSubset(const TArray<const FPreprocessedStaticMesh*>& InStaticMeshSubset, TArray<TArray<const FPreprocessedStaticMesh*>>& OutResults) = 0;

		TSet<FPreprocessRegistry*> Registries;
	};

	class FPerVertexAnalyzer :public IMeshSimilarityAnalyzer
	{
	public:
		FPerVertexAnalyzer(const TSet<FPreprocessRegistry*>& InRegistries) :IMeshSimilarityAnalyzer(InRegistries) {}

	private:
		virtual void AnalyzesSubset(const TArray<const FPreprocessedStaticMesh*>& InStaticMeshSubset, TArray<TArray<const FPreprocessedStaticMesh*>>& OutResults)override
		{
			TArray<TArray<const FPreprocessedStaticMesh*>> SubResults;

			TSet<const FPreprocessedStaticMesh*> ProcessedIdenticalStaticMeshes;
			for (int i = 0; i < InStaticMeshSubset.Num(); ++i)
			{
				if (ProcessedIdenticalStaticMeshes.Contains(InStaticMeshSubset[i]))
				{
					continue;
				}

				TArray<const FPreprocessedStaticMesh*> IdenticalGroup;
				IdenticalGroup.Add(InStaticMeshSubset[i]);

				for (int j = i + 1; j < InStaticMeshSubset.Num(); ++j)
				{
					if (ProcessedIdenticalStaticMeshes.Contains(InStaticMeshSubset[j]))
					{
						continue;
					}

					if (PerVertexCompareStaticMeshes(InStaticMeshSubset[i], InStaticMeshSubset[j]))
					{
						IdenticalGroup.Add(InStaticMeshSubset[j]);
					}
				}

				if (IdenticalGroup.Num() > 1)
				{
					for (const FPreprocessedStaticMesh* Mesh : IdenticalGroup)
					{
						ProcessedIdenticalStaticMeshes.Add(Mesh);
					}
					SubResults.Add(MoveTemp(IdenticalGroup));
				}
			}

			OutResults.Append(MoveTemp(SubResults));
		}

		bool PerVertexCompareStaticMeshes(const FPreprocessedStaticMesh* A, const FPreprocessedStaticMesh* B)
		{
			const TArray<FPreprocessedVector>& PositionBufferA = A->GetQuantizedPositionBuffer();
			const TArray<FPreprocessedVector>& PositionBufferB = B->GetQuantizedPositionBuffer();

			const int32 NumberOfVertexA = PositionBufferA.Num();
			const int32 NumberOfVertexB = PositionBufferB.Num();

			//Bounding box test first since they are most likly to be different
			const FBox& BoundingBoxA = A->GetBoundingBox();
			const FBox& BoundingBoxB = B->GetBoundingBox();
			if (!BoundingBoxA.Equals(BoundingBoxB))
			{
				return false;
			}

			//Do a random position test
			const int32 RandomIndex = FMath::RandRange(0, NumberOfVertexA - 1);
			if (PositionBufferA[RandomIndex] != PositionBufferB[RandomIndex])
			{
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
	};

	namespace MemoryHashPrivate
	{
		template<typename FHashType>
		struct TKey
		{
			uint32 Exponent = DEFAULT_EXPONENT;
			int32 NumOfVertices = 0;
			FHashType Hash{};

			TKey<FHashType>() {}
			TKey<FHashType>(uint32 InExponent, uint32 InNumOfVertices, FHashType InHash) :Exponent(InExponent), NumOfVertices(InNumOfVertices), Hash(InHash) {}

			inline bool operator==(const TKey<FHashType>& InOther) const
			{
				return Exponent == InOther.Exponent && NumOfVertices == InOther.NumOfVertices && Hash == InOther.Hash;
			}
			inline bool operator!=(const TKey<FHashType>& InOther) const
			{
				return Exponent != InOther.Exponent || NumOfVertices != InOther.NumOfVertices || Hash != InOther.Hash;
			}
			inline bool operator<(const TKey<FHashType>& InOther) const
			{
				return Exponent != InOther.Exponent ? Exponent < InOther.Exponent :
					   NumOfVertices != InOther.NumOfVertices ? NumOfVertices < InOther.NumOfVertices :
					   Hash < InOther.Hash;
			}
		};

		template<typename FHashType>
		inline uint32 GetTypeHash(const TKey<FHashType>& InKey)
		{
			return HashCombine(::GetTypeHash(InKey.Exponent), HashCombine(::GetTypeHash(InKey.NumOfVertices), ::GetTypeHash(InKey.Hash)));
		}

		template<typename FHashType>
		inline TKey<FHashType> MakeKey(uint32 InExponent, int32 InNumOfVertex, const void* InRawData)
		{
			FHashType Hash = FHashType::HashBuffer(InRawData, InNumOfVertex * sizeof(FPreprocessedVector));
			return TKey<FHashType>(InExponent, InNumOfVertex, Hash);
		}

		template<typename FHashType>
		struct TMemoryHashContext
		{
			struct TMemoryHashResult
			{
				TKey<FHashType> Key;
				const FPreprocessedStaticMesh* StaticMesh = nullptr;
			};

			TArray<TMemoryHashContext<FHashType>::TMemoryHashResult> ResultsInThisContext;
		};
	}

	template<typename FHashType>
	class TMemoryHashAnalyzer :public IMeshSimilarityAnalyzer
	{
	public:
		TMemoryHashAnalyzer(const TSet<FPreprocessRegistry*>& InRegistries) :IMeshSimilarityAnalyzer(InRegistries) {}

	protected:
		virtual void AnalyzesSubset(const TArray<const FPreprocessedStaticMesh*>& InStaticMeshSubset, TArray<TArray<const FPreprocessedStaticMesh*>>& OutResults)override
		{
			TArray<MemoryHashPrivate::TMemoryHashContext<FHashType>> Contexts;

			auto LoopBody = [&](MemoryHashPrivate::TMemoryHashContext<FHashType>& Context, int32 Index) {
				FTaskTagScope TaskTag(ETaskTag::EParallelGameThread);

				const FPreprocessedStaticMesh* StaticMesh = InStaticMeshSubset[Index];
				const uint32 Exponent = StaticMesh->GetQuantizationExponent();
				const TArray<FPreprocessedVector>& QuantizedPositionBuffer = StaticMesh->GetQuantizedPositionBuffer();

				using MemoryHashResultType = MemoryHashPrivate::TMemoryHashContext<FHashType>::TMemoryHashResult;
				MemoryHashResultType HashResult{};
				HashResult.Key = MemoryHashPrivate::MakeKey<FHashType>(Exponent, QuantizedPositionBuffer.Num(), QuantizedPositionBuffer.GetData());
				HashResult.StaticMesh = StaticMesh;

				Context.ResultsInThisContext.Add(HashResult);
			};

			ParallelForWithTaskContext(
				TEXT("ParallelMemoryHashStaticMeshes"),
				Contexts,
				InStaticMeshSubset.Num(),
				512,
				MoveTemp(LoopBody),
				EParallelForFlags::None
			);

			TMap<MemoryHashPrivate::TKey<FHashType>, TArray<const FPreprocessedStaticMesh*>> HashMap;
			for (const auto& Context : Contexts)
			{
				for (const auto& Result : Context.ResultsInThisContext)
				{
					HashMap.FindOrAdd(Result.Key).Add(Result.StaticMesh);
				}
			}

			for (TPair<MemoryHashPrivate::TKey<FHashType>, TArray<const FPreprocessedStaticMesh*>>& Iterator : HashMap)
			{
				if (Iterator.Value.Num() >= 2)
				{
					//Add anyway but warn if bounding boxes not matching
					OutResults.Add(Iterator.Value);
					
					const FBox& BoundingBox = Iterator.Value[0]->GetBoundingBox();
					bool bAllMatching = true;

					for (int i = 1; i < Iterator.Value.Num(); ++i)
					{
						if (!BoundingBox.Equals(Iterator.Value[i]->GetBoundingBox()))
						{
							bAllMatching = false;
							break;
						}
					}

					if (!bAllMatching)
					{
						UE_LOG(LogAssetSanitiser, Warning, TEXT("=== Begin Hash Collision Static Mesh Group ==="));
						for (int i = 0; i < Iterator.Value.Num(); ++i)
						{
							UE_LOG(LogAssetSanitiser, Warning, TEXT("SM: %s"), *Iterator.Value[i]->GetStaticMeshObjectPath());
						}
						UE_LOG(LogAssetSanitiser, Warning, TEXT("=== End Hash Collision Static Mesh Group ==="));
					}
				}
			}
		}
	};
}

typedef Analyzer::EAnalyzerType					  EAnalyzerType;
typedef Analyzer::IMeshSimilarityAnalyzer		  IMeshSimilarityAnalyzer;
typedef Analyzer::FPerVertexAnalyzer			  FPerVertexAnalyzer;
typedef Analyzer::TMemoryHashAnalyzer<FXxHash64>  FXxHash64Analyzer;
typedef Analyzer::TMemoryHashAnalyzer<FXxHash128> FXxHash128Analyzer;
