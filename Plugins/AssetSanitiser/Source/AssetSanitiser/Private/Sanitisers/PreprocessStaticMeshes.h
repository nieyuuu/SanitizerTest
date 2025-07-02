#pragma once

#include "LogAssetSanitiser.h"

#include "CoreMinimal.h"

constexpr uint32 MIN_EXPONENT = 1;
constexpr uint32 MAX_EXPONENT = 7;
constexpr uint32 DEFAULT_EXPONENT = 4;

namespace StaticMeshPreprocessing
{
	//Settings defining the behaviors when processing static meshes.
	struct FSettings
	{
	public:
		FSettings(uint32 InExponent = DEFAULT_EXPONENT)
		{
			QuantizationExponent = FMath::Clamp(InExponent, MIN_EXPONENT, MAX_EXPONENT);
		}

		inline uint32 GetQuantizationExponent()const
		{
			return QuantizationExponent;
		}

	private:
		//The Exponent when quantizing vertices. Should be in a range incase quantization overflow/underflow (eg. [1,2,3,4,5,6,7]).
		//Maybe using customized exponent based on the vertex positons and the bounding box size of static meshes because of floating point precision.
		uint32 QuantizationExponent = DEFAULT_EXPONENT;
	};

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
		inline uint32 GetQuantizationExponent()const
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

		uint32 QuantizationExponent = DEFAULT_EXPONENT;
		FString StaticMeshObjectPath;
		FBox BoundingBox;
		TArray<FQuantizedVector> QuantizedPositionBuffer;
	};

	enum class EStatus :uint8
	{
		Unknown,
		NoError,
		InvalidObjectPath,
		LOD0SourceModelNotFound,
		MeshDescriptionNotFound,
		PositionBufferContainsNaN
	};

	//Registry which holds the preprocess results.
	//Elements in same registry are unique and their quantization exponents equal with the registry's exponent
	struct FPreprocessRegistry
	{
		friend FPreprocessRegistry PreprocessStaticMeshes(const TSet<const UStaticMesh*>& InStaticMeshesToProcess, FSettings InSettings);
		friend FPreprocessRegistry PreprocessStaticMeshes(const TSet<FString>& InStaticMeshObjectPaths, FSettings InSettings);

	public:
		FPreprocessRegistry(uint32 InQuantizationExponent = DEFAULT_EXPONENT) :QuantizationExponent(InQuantizationExponent) {}
		virtual ~FPreprocessRegistry()
		{
			for (int i = 0; i < ProcessedStaticMeshes.Num(); ++i)
			{
				delete ProcessedStaticMeshes[i];
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
			ObjectPathToErrorStatus = MoveTemp(InOther.ObjectPathToErrorStatus);
			ProcessedStaticMeshes = MoveTemp(InOther.ProcessedStaticMeshes);

			InOther.QuantizationExponent = DEFAULT_EXPONENT;
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
					delete ProcessedStaticMeshes[i];
				}
				ProcessedStaticMeshes.Empty(InOther.ProcessedStaticMeshes.Num());

				QuantizationExponent = InOther.QuantizationExponent;
				ObjectPathToErrorStatus = MoveTemp(InOther.ObjectPathToErrorStatus);
				ProcessedStaticMeshes = MoveTemp(InOther.ProcessedStaticMeshes);

				InOther.QuantizationExponent = DEFAULT_EXPONENT;
				InOther.ObjectPathToErrorStatus.Empty();
				InOther.ProcessedStaticMeshes.Empty();
			}

			return *this;
		}

		inline uint32 GetQuantizationExponent()const
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
		EStatus GetStatusInThisRegistry(const FString& InStaticMeshObjectPath)const;

		inline const TMap<FString, EStatus>& GetObjectPathToErrorStatus()const
		{
			return ObjectPathToErrorStatus;
		}

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
		uint32 QuantizationExponent = DEFAULT_EXPONENT;
		//FString is default case insensitive but this might be ok because there cant be two assets SM_Asset and SM_asset under same folder
		//And there cant be two sub-folsers Folder/folder under same folder
		//TMap<FString, EStatus, FDefaultSetAllocator, FLocKeyMapFuncs<EStatus>> will handle case sensitive of FString
		TMap<FString, EStatus> ObjectPathToErrorStatus;
		TArray<FQuantizedStaticMesh*> ProcessedStaticMeshes;

		//Allocate an instance of FQuantizedStaticMesh and set its QuantizationExponent
		[[nodiscard]] FQuantizedStaticMesh* Allocate()const;

		//Allocate an instance of FQuantizedStaticMesh and add it to ProcessedStaticMeshes
		FQuantizedStaticMesh* AllocateAndAddToThisRegistry(const FString& InStaticMeshObjectPath);
	};

	FPreprocessRegistry PreprocessStaticMeshes(const TSet<const UStaticMesh*>& InStaticMeshesToProcess, FSettings InSettings);

	//FString is default case insensitive but this might be ok because there cant be two assets SM_Asset and SM_asset under same folder
	//And there cant be two sub-folsers Folder/folder under same folder
	//TSet<FString, FLocKeySetFuncs, FDefaultSetAllocator> will handle case sensitive of FString
	FPreprocessRegistry PreprocessStaticMeshes(const TSet<FString>& InStaticMeshObjectPaths, FSettings InSettings);

	//Save to disk [D:\MyWorkspace\UnrealProjects\SanitizerTest\Saved\Test.bin]
	bool SaveTo(const FString& InSaveFileName, FPreprocessRegistry& InRegistry);

	//Load from disk [D:\MyWorkspace\UnrealProjects\SanitizerTest\Saved\Test.bin]
	bool LoadFrom(const FString& InLoadFileName, FPreprocessRegistry& InRegistry);
}

typedef StaticMeshPreprocessing::FSettings            FPreprocessSettings;
typedef StaticMeshPreprocessing::FQuantizedVector     FPreprocessedVector;
typedef StaticMeshPreprocessing::FQuantizedStaticMesh FPreprocessedStaticMesh;
typedef StaticMeshPreprocessing::EStatus              EPreprocessStatus;
typedef StaticMeshPreprocessing::FPreprocessRegistry  FPreprocessRegistry;
