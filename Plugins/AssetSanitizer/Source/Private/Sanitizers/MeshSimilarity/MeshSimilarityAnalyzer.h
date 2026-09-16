#pragma once

#include "MeshPreprocessor.h"

#include "MeshSimilarityAnalyzer.generated.h"

uint32 GetTypeHash(const FXxHash64& InHash);
uint32 GetTypeHash(const FXxHash128& InHash);

namespace StaticMeshAnalyzer
{
	UENUM()
	enum class EStaticMeshAnalyzerType :uint8
	{
		PerVertex,
		XxHash64,
		XxHash128
	};

	USTRUCT()
	struct FStaticMeshSimilarGroup
	{
		GENERATED_BODY()

		UPROPERTY()
		int32 NumOfVertices = 0;

		UPROPERTY()
		TArray<FSoftObjectPath> StaticMeshes;
	};

	USTRUCT()
	struct FStaticMeshAnalyzeResults
	{
		GENERATED_BODY()

		UPROPERTY()
		int32 QuantizationExponent = DEFAULT_EXPONENT;

		UPROPERTY()
		TArray<FStaticMeshSimilarGroup> SimilarGroups;

		UPROPERTY()
		TMap<FString, StaticMeshPreprocessor::EStaticMeshStatus> ObjectPathToErrorStatus;

		//Save to disk eg. [D:\MyWorkspace\UnrealProjects\SanitizerTest\Saved\AnalyzeResults.json]
		static bool SaveTo(FStaticMeshAnalyzeResults& InResults, const FString& InSaveFileName);

		//Load from disk eg. [D:\MyWorkspace\UnrealProjects\SanitizerTest\Saved\AnalyzeResults.json]
		static bool LoadFrom(FStaticMeshAnalyzeResults& InResults, const FString& InLoadFileName);
	};

	class IMeshSimilarityAnalyzer
	{
	public:
		IMeshSimilarityAnalyzer() = default;
		virtual ~IMeshSimilarityAnalyzer() = default;

		bool Analyzes(const TArray<StaticMeshPreprocessor::FRegistry*>& InRegistries, FStaticMeshAnalyzeResults& OutResults)const;

	protected:
		//Checks all registries' QuantizationExponent are equal and elements in all registries are unique.
		//Classify static meshes by their vertex counts and filter those which have only one static mesh.
		bool ClassifyStaticMeshes(const TArray<StaticMeshPreprocessor::FRegistry*>& InRegistries, TMap<int32, TArray<const StaticMeshPreprocessor::FStaticMesh*>>& OutClassifiedStaticMeshes)const;

		//Analyze a subset of the classified static meshes which have same number of vertices.
		virtual void AnalyzesSubset(const TArray<const StaticMeshPreprocessor::FStaticMesh*>& InStaticMeshSubset, TArray<TArray<const StaticMeshPreprocessor::FStaticMesh*>>& OutResults)const = 0;
	};

	class FPerVertexAnalyzer :public IMeshSimilarityAnalyzer
	{
	public:
		FPerVertexAnalyzer() = default;

	private:
		virtual void AnalyzesSubset(const TArray<const StaticMeshPreprocessor::FStaticMesh*>& InStaticMeshSubset, TArray<TArray<const StaticMeshPreprocessor::FStaticMesh*>>& OutResults)const override;

		bool PerVertexCompareStaticMeshes(const StaticMeshPreprocessor::FStaticMesh* A, const StaticMeshPreprocessor::FStaticMesh* B)const;
	};

	namespace MemoryHashPrivate
	{
		template<typename FHashType>
		struct TKey
		{
			int32 Exponent = DEFAULT_EXPONENT;
			int32 NumOfVertices = 0;
			FHashType Hash;

			static TKey<FHashType> Construct(int32 InExponent, int32 InNumOfVertices, FHashType InHash)
			{
				TKey<FHashType> Result;
				Result.Exponent = InExponent;
				Result.NumOfVertices = InNumOfVertices;
				Result.Hash = InHash;
				return Result;
			}

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
		inline TKey<FHashType> MakeKey(int32 InExponent, int32 InNumOfVertex, const void* InRawData)
		{
			FHashType Hash = FHashType::HashBuffer(InRawData, InNumOfVertex * sizeof(FInt64Vector3));
			return TKey<FHashType>::Construct(InExponent, InNumOfVertex, Hash);
		}

		template<typename FHashType>
		struct TMemoryHashContext
		{
			struct TMemoryHashResult
			{
				TKey<FHashType> Key;
				const StaticMeshPreprocessor::FStaticMesh* StaticMesh = nullptr;
			};

			TArray<TMemoryHashContext<FHashType>::TMemoryHashResult> ResultsInThisContext;
		};
	}

	template<typename FHashType>
	class TMemoryHashAnalyzer :public IMeshSimilarityAnalyzer
	{
	public:
		TMemoryHashAnalyzer() = default;

	protected:
		virtual void AnalyzesSubset(const TArray<const StaticMeshPreprocessor::FStaticMesh*>& InStaticMeshSubset, TArray<TArray<const StaticMeshPreprocessor::FStaticMesh*>>& OutResults)const override
		{
			TArray<MemoryHashPrivate::TMemoryHashContext<FHashType>> Contexts;

			auto LoopBody = [&](MemoryHashPrivate::TMemoryHashContext<FHashType>& Context, int32 Index) {
				FTaskTagScope TaskTag(ETaskTag::EParallelGameThread);

				const StaticMeshPreprocessor::FStaticMesh* StaticMesh = InStaticMeshSubset[Index];
				const int32 Exponent = StaticMesh->GetQuantizationExponent();
				const TArray<FInt64Vector3>& QuantizedPositionBuffer = StaticMesh->GetQuantizedPositionBuffer();

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
				256,
				MoveTemp(LoopBody),
				EParallelForFlags::None
			);

			TMap<MemoryHashPrivate::TKey<FHashType>, TArray<const StaticMeshPreprocessor::FStaticMesh*>> HashMap;
			for (const auto& Context : Contexts)
			{
				for (const auto& Result : Context.ResultsInThisContext)
				{
					HashMap.FindOrAdd(Result.Key).Add(Result.StaticMesh);
				}
			}

			for (TPair<MemoryHashPrivate::TKey<FHashType>, TArray<const StaticMeshPreprocessor::FStaticMesh*>>& Iterator : HashMap)
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
						UE_LOG(LogAssetSanitizer, Warning, TEXT("=== Begin Hash Collision Static Mesh Group ==="));
						for (int i = 0; i < Iterator.Value.Num(); ++i)
						{
							UE_LOG(LogAssetSanitizer, Warning, TEXT("Static Mesh: %s"), *Iterator.Value[i]->GetStaticMeshObjectPath());
						}
						UE_LOG(LogAssetSanitizer, Warning, TEXT("=== End Hash Collision Static Mesh Group ==="));
					}
				}
			}
		}
	};

	bool AnalyzeMeshSimilarity(EStaticMeshAnalyzerType InAnalyzerType, const TArray<StaticMeshPreprocessor::FRegistry*>& InRegistries, FStaticMeshAnalyzeResults& OutResults);
}
