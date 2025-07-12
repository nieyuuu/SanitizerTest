#pragma once

#include "LogAssetSanitizer.h"

#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"

constexpr int32 MIN_EXPONENT = -2; //Meters
constexpr int32 MAX_EXPONENT = 7;
constexpr int32 DEFAULT_EXPONENT = 0; //Centimeters

UENUM()
enum class EPreprocessStatus :uint8
{
	Unknown,
	NoError,
	InvalidObjectPath,
	LOD0SourceModelNotFound,
	MeshDescriptionNotFound,
	PositionBufferContainsNaN
};

//Settings defining the behaviors when preprocessing static meshes.
struct FPreprocessSettings
{
public:
	FPreprocessSettings(int32 InQuantizationExponent = DEFAULT_EXPONENT)
	{
		QuantizationExponent = FMath::Clamp(InQuantizationExponent, MIN_EXPONENT, MAX_EXPONENT);
	}

	inline int32 GetQuantizationExponent()const
	{
		return QuantizationExponent;
	}

private:
	//The Exponent when quantizing vertices. Should be in a range incase quantization overflow/underflow (eg. [-2,7]).
	//Probably using customized exponent based on the vertex positons and the bounding box size of static meshes because of floating point precision.
	int32 QuantizationExponent = DEFAULT_EXPONENT;
};

namespace StaticMeshPreprocessing
{
	struct FQuantizedVector
	{
		int64 X = 0;
		int64 Y = 0;
		int64 Z = 0;

		FQuantizedVector() = default;
		FQuantizedVector(int64 InX, int64 InY, int64 InZ) :X(InX), Y(InY), Z(InZ) {}

		inline bool operator==(const FQuantizedVector& InOther)const
		{
			return X == InOther.X && Y == InOther.Y && Z == InOther.Z;
		}

		inline bool operator!=(const FQuantizedVector& InOther)const
		{
			return X != InOther.X || Y != InOther.Y || Z != InOther.Z;
		}

		inline bool operator<(const FQuantizedVector& InOther)const
		{
			return X != InOther.X ? X < InOther.X :
				   Y != InOther.Y ? Y < InOther.Y :
				   Z < InOther.Z;
		}

		friend FArchive& operator<<(FArchive& InAr, FQuantizedVector& InVector)
		{
			InAr << InVector.X;
			InAr << InVector.Y;
			InAr << InVector.Z;

			return InAr;
		}
	};

	struct FQuantizedStaticMesh
	{
		friend struct FPreprocessRegistry;

	public:
		inline int32 GetQuantizationExponent()const
		{
			return QuantizationExponent;
		}
		inline const FString& GetStaticMeshObjectPath()const
		{
			return StaticMeshObjectPath;
		}
		inline const FBox& GetBoundingBox()const
		{
			return BoundingBox;
		}
		inline const TArray<FQuantizedVector>& GetQuantizedPositionBuffer()const
		{
			return QuantizedPositionBuffer;
		}

		inline FBox& FetchBoundingBox()
		{
			return BoundingBox;
		}
		inline TArray<FQuantizedVector>& FetchQuantizedPositionBuffer()
		{
			return QuantizedPositionBuffer;
		}

		friend FArchive& operator<<(FArchive& InAr, FQuantizedStaticMesh& InQuantizedStaticMesh)
		{
			InAr << InQuantizedStaticMesh.QuantizationExponent;
			InAr << InQuantizedStaticMesh.StaticMeshObjectPath;
			InAr << InQuantizedStaticMesh.BoundingBox;
			InAr << InQuantizedStaticMesh.QuantizedPositionBuffer;

			return InAr;
		}

	private:
		//Private constructor
		FQuantizedStaticMesh() = default;

		int32 QuantizationExponent = DEFAULT_EXPONENT;
		FString StaticMeshObjectPath;
		FBox BoundingBox;
		TArray<FQuantizedVector> QuantizedPositionBuffer;
	};

	//Registry which holds the preprocess results.
	//Elements in same registry are unique and their quantization exponents equal with the registry's exponent
	struct FPreprocessRegistry
	{
	public:
		FPreprocessRegistry(int32 InQuantizationExponent = DEFAULT_EXPONENT) :QuantizationExponent(InQuantizationExponent) {}
		virtual ~FPreprocessRegistry()
		{
			for (int i = 0; i < ProcessedStaticMeshes.Num(); ++i)
			{
				if (ProcessedStaticMeshes[i] != nullptr)
				{
					delete ProcessedStaticMeshes[i];
				}
			}
		}

		//Copy constructor(Deleted)
		FPreprocessRegistry(const FPreprocessRegistry& InOther) = delete;
		//Copy assignment operator(Deleted)
		FPreprocessRegistry& operator=(const FPreprocessRegistry& InOther) = delete;

		//Move constructor
		FPreprocessRegistry(FPreprocessRegistry&& InOther)
		{
			QuantizationExponent = InOther.QuantizationExponent;
			bFromDiskFile = InOther.bFromDiskFile;
			FileName = MoveTemp(InOther.FileName);
			ObjectPathToErrorStatus = MoveTemp(InOther.ObjectPathToErrorStatus);
			ProcessedStaticMeshes = MoveTemp(InOther.ProcessedStaticMeshes);

			InOther.QuantizationExponent = DEFAULT_EXPONENT;
			InOther.bFromDiskFile = false;
			InOther.FileName.Empty();
			InOther.ObjectPathToErrorStatus.Empty();
			InOther.ProcessedStaticMeshes.Empty();
		}
		//Move assignment operator
		FPreprocessRegistry& operator=(FPreprocessRegistry&& InOther)
		{
			if (this != &InOther)
			{
				ObjectPathToErrorStatus.Empty(InOther.ObjectPathToErrorStatus.Num());
				for (int i = 0; i < ProcessedStaticMeshes.Num(); ++i)
				{
					if (ProcessedStaticMeshes[i] != nullptr)
					{
						delete ProcessedStaticMeshes[i];
					}
				}
				ProcessedStaticMeshes.Empty(InOther.ProcessedStaticMeshes.Num());

				QuantizationExponent = InOther.QuantizationExponent;
				bFromDiskFile = InOther.bFromDiskFile;
				FileName = MoveTemp(InOther.FileName);
				ObjectPathToErrorStatus = MoveTemp(InOther.ObjectPathToErrorStatus);
				ProcessedStaticMeshes = MoveTemp(InOther.ProcessedStaticMeshes);

				InOther.QuantizationExponent = DEFAULT_EXPONENT;
				InOther.bFromDiskFile = false;
				InOther.FileName.Empty();
				InOther.ObjectPathToErrorStatus.Empty();
				InOther.ProcessedStaticMeshes.Empty();
			}

			return *this;
		}

		inline int32 GetQuantizationExponent()const
		{
			return QuantizationExponent;
		}

		//Try to find the quantized static mesh pointer in this registry.
		//Returns nullptr if not found:
		//1.Static mesh is not in this registry
		//2.Static mesh is in this registry but error occurred when preprocessing it
		const FQuantizedStaticMesh* TryFind(const FString& InStaticMeshObjectPath)const;

		inline const TArray<FQuantizedStaticMesh*>& GetProcessedStaticMeshes()const
		{
			return ProcessedStaticMeshes;
		}

		//Get the status of a static mesh in this registry
		EPreprocessStatus GetStatusInThisRegistry(const FString& InStaticMeshObjectPath)const;

		inline const TMap<FString, EPreprocessStatus>& GetObjectPathToErrorStatus()const
		{
			return ObjectPathToErrorStatus;
		}

		inline bool IsFromDiskFile()const
		{
			return bFromDiskFile;
		}

		inline FName GetFileName()const
		{
			if (bFromDiskFile)
			{
				check(FileName.EndsWith(TEXT(".bin")));
				return FName(*FileName);
			}
			else
			{
				check(FileName.Len() == 0);
				return NAME_None;
			}
		}

		//Save to disk eg. [D:\MyWorkspace\UnrealProjects\SanitizerTest\Saved\Test.bin]
		static bool SaveTo(TUniquePtr<FPreprocessRegistry>& InRegistry, const FString& InSaveFileName);

		//Load from disk eg. [D:\MyWorkspace\UnrealProjects\SanitizerTest\Saved\Test.bin]
		static bool LoadFrom(TUniquePtr<FPreprocessRegistry>& InRegistry, const FString& InLoadFileName);

		static TUniquePtr<FPreprocessRegistry> PreprocessStaticMeshes(const TSet<const UStaticMesh*>& InStaticMeshesToProcess, FPreprocessSettings InSettings);

		//FString is default case insensitive but this might be ok because there cant be two assets SM_Asset/SM_asset under same folder
		//And there cant be two sub-folsers Folder/folder under same folder
		//TSet<FString, FLocKeySetFuncs, FDefaultSetAllocator> will handle case sensitive of FString
		static TUniquePtr<FPreprocessRegistry> PreprocessStaticMeshes(const TSet<FString>& InStaticMeshObjectPaths, FPreprocessSettings InSettings);

		friend FArchive& operator<<(FArchive& InAr, FPreprocessRegistry& InRegistry)
		{
			if (InAr.IsLoading())
			{
				checkf(!InRegistry.ObjectPathToErrorStatus.Num() && !InRegistry.ProcessedStaticMeshes.Num(), TEXT("Attempt to deserialze an archive to non-empty registry."));
			}

			InAr << InRegistry.QuantizationExponent;
			InAr << InRegistry.ObjectPathToErrorStatus;

			if (InAr.IsLoading())
			{
				int32 NumOfStaticMeshesInArch = 0;
				InAr << NumOfStaticMeshesInArch;

				InRegistry.ProcessedStaticMeshes.Reserve(NumOfStaticMeshesInArch);

				for (int i = 0; i < NumOfStaticMeshesInArch; ++i)
				{
					FQuantizedStaticMesh* QuantizedStaticMesh = InRegistry.Allocate();
					InAr << *QuantizedStaticMesh;
					checkf(QuantizedStaticMesh->GetQuantizationExponent() == InRegistry.QuantizationExponent &&
						   InRegistry.TryFind(QuantizedStaticMesh->GetStaticMeshObjectPath()) == nullptr,
						   TEXT("The archive may be corrupted. Consider remove the corrupted archive and retry preprocess static meshes."));
					InRegistry.ProcessedStaticMeshes.Add(QuantizedStaticMesh);

					//RTTI is disabled in UE by default
					/*FArchiveFileReaderGeneric* FileReader = dynamic_cast<FArchiveFileReaderGeneric*>(&InAr);
					if (FileReader)
					{
						InRegistry.bFromDiskFile = true;
						InRegistry.FileName = FileReader->GetArchiveName();
					}*/
				}
			}
			else
			{
				int32 NumOfStaticMeshes = InRegistry.ProcessedStaticMeshes.Num();
				InAr << NumOfStaticMeshes;

				for (int i = 0; i < NumOfStaticMeshes; ++i)
				{
					InAr << *InRegistry.ProcessedStaticMeshes[i];
				}
			}

			return InAr;
		}

	private:
		int32 QuantizationExponent = DEFAULT_EXPONENT;
		bool bFromDiskFile = false;
		FString FileName;
		//FString is default case insensitive but this might be ok because there cant be two assets SM_Asset/SM_asset under same folder
		//And there cant be two sub-folsers Folder/folder under same folder
		//TMap<FString, EPreprocessStatus, FDefaultSetAllocator, FLocKeyMapFuncs<EPreprocessStatus>> will handle case sensitive of FString
		TMap<FString, EPreprocessStatus> ObjectPathToErrorStatus;
		TArray<FQuantizedStaticMesh*> ProcessedStaticMeshes;

		//Allocate an instance of FQuantizedStaticMesh and set its QuantizationExponent
		[[nodiscard]] FQuantizedStaticMesh* Allocate()const;

		//Allocate an instance of FQuantizedStaticMesh and add it to ProcessedStaticMeshes
		FQuantizedStaticMesh* AllocateAndAddToThisRegistry(const FString& InStaticMeshObjectPath);
	};
}

constexpr int32 MIN_NUM_OF_BATCHES = 1;

struct FDefaultWeightCalculator
{
	double operator()(const FAssetData& InAssetData);
};

struct FDiskSizeWeightCalculator
{
	double operator()(const FAssetData& InAssetData);
};

struct FPreprocessBalancer
{
public:
	static TArray<TArray<FString>> BalanceStaticMeshes(const TArray<FString>& InDirsToProcess, int32 InNumOfBatches = MIN_NUM_OF_BATCHES);
	static TArray<TArray<FString>> BalanceStaticMeshesBasedOnDiskSize(const TArray<FString>& InDirsToProcess, int32 InNumOfBatches = MIN_NUM_OF_BATCHES);

private:
	template<typename WeightCalculatorType>
	static TArray<TArray<FString>> BalanceStaticMeshesImp(const TArray<FString>& InDirsToProcess, int32 InNumOfBatches);
};

template<typename WeightCalculatorType>
inline TArray<TArray<FString>> FPreprocessBalancer::BalanceStaticMeshesImp(const TArray<FString>& InDirsToProcess, int32 InNumOfBatches)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName);
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.SearchAllAssets(true/* bSynchronousSearch */);

	FARFilter Filter;
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Append(InDirsToProcess);
	Filter.bRecursivePaths = true;
	Filter.bIncludeOnlyOnDiskAssets = true;

	TArray<FAssetData> StaticMeshAssetDatas;
	AssetRegistry.GetAssets(Filter, StaticMeshAssetDatas);

	if (StaticMeshAssetDatas.Num() == 0)
	{
		UE_LOG(LogAssetSanitizer, Warning, TEXT("Empty static mesh asset datas."));
		return TArray<TArray<FString>>();
	}

	int32 ActualNumOfBatches = FMath::Max(InNumOfBatches, MIN_NUM_OF_BATCHES);
	ActualNumOfBatches = ActualNumOfBatches > StaticMeshAssetDatas.Num() ? StaticMeshAssetDatas.Num() : ActualNumOfBatches;

	TArray<double> WeightSums;
	TArray<TArray<FString>> BalancedBatches;
	
	WeightSums.AddDefaulted(ActualNumOfBatches);
	BalancedBatches.AddDefaulted(ActualNumOfBatches);

	WeightCalculatorType WeightCalculator{};

	for (int i = 0; i < StaticMeshAssetDatas.Num(); ++i)
	{
		int32 MinWeightIndex = WeightSums.Find(*Algo::MinElement(WeightSums));

		BalancedBatches[MinWeightIndex].Add(StaticMeshAssetDatas[i].GetObjectPathString());
		WeightSums[MinWeightIndex] += WeightCalculator(StaticMeshAssetDatas[i]);
	}

	return MoveTemp(BalancedBatches);
}

typedef StaticMeshPreprocessing::FQuantizedVector     FPreprocessedPosition;
typedef StaticMeshPreprocessing::FQuantizedStaticMesh FPreprocessedStaticMesh;
typedef StaticMeshPreprocessing::FPreprocessRegistry  FPreprocessRegistry;
