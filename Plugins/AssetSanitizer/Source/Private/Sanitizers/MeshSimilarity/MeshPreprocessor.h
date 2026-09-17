#pragma once

#include "LogAssetSanitizer.h"

#include "CoreMinimal.h"
#include "Math/MathFwd.h"
#include "AssetRegistry/AssetRegistryModule.h"

constexpr int32 MIN_EXPONENT = -3, MAX_EXPONENT = 8, DEFAULT_EXPONENT = 0;

namespace StaticMeshPreprocessor
{
	enum class EStatus :uint8
	{
		Unknown,
		NoError,
		InvalidObjectPath,
		LOD0SourceModelNotFound,
		MeshDescriptionNotFound,
		PositionBufferContainsNaN
	};

	FString StatusToString(EStatus InStatus);

	struct FSettings
	{
	public:
		FSettings(int32 InQuantizationExponent = DEFAULT_EXPONENT)
		{
			QuantizationExponent = FMath::Clamp(InQuantizationExponent, MIN_EXPONENT, MAX_EXPONENT);
		}

		inline int32 GetQuantizationExponent()const
		{
			return QuantizationExponent;
		}

	private:
		int32 QuantizationExponent = DEFAULT_EXPONENT;
	};

	struct FStaticMesh
	{
		friend struct FRegistry;

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
		inline const TArray<FInt64Vector3>& GetQuantizedPositionBuffer()const
		{
			return QuantizedPositionBuffer;
		}

		friend FArchive& operator<<(FArchive& InAr, FStaticMesh& InQuantizedStaticMesh)
		{
			InAr << InQuantizedStaticMesh.QuantizationExponent;
			InAr << InQuantizedStaticMesh.BoundingBox;
			InAr << InQuantizedStaticMesh.StaticMeshObjectPath;
			InAr << InQuantizedStaticMesh.QuantizedPositionBuffer;

			return InAr;
		}

	private:
		//Private constructor
		FStaticMesh() = default;

		int32 QuantizationExponent = DEFAULT_EXPONENT;
		FBox BoundingBox = {};
		FString StaticMeshObjectPath = {};
		TArray<FInt64Vector3> QuantizedPositionBuffer = {};
	};

	//Registry which holds the preprocess results.
	//Elements in same registry are unique and their quantization exponents equal with the registry's exponent
	struct FRegistry
	{
	public:
		FRegistry(int32 InQuantizationExponent = DEFAULT_EXPONENT) :QuantizationExponent(InQuantizationExponent) {}
		
		virtual ~FRegistry()
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
		FRegistry(const FRegistry& InOther) = delete;
		//Copy assignment operator(Deleted)
		FRegistry& operator=(const FRegistry& InOther) = delete;

		//Move constructor
		FRegistry(FRegistry&& InOther)
		{
			QuantizationExponent = InOther.QuantizationExponent;
			ObjectPathToErrorStatus = MoveTemp(InOther.ObjectPathToErrorStatus);
			ProcessedStaticMeshes = MoveTemp(InOther.ProcessedStaticMeshes);

			InOther.QuantizationExponent = DEFAULT_EXPONENT;
			InOther.ObjectPathToErrorStatus.Empty();
			InOther.ProcessedStaticMeshes.Empty();
		}
		//Move assignment operator
		FRegistry& operator=(FRegistry&& InOther)
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
				ObjectPathToErrorStatus = MoveTemp(InOther.ObjectPathToErrorStatus);
				ProcessedStaticMeshes = MoveTemp(InOther.ProcessedStaticMeshes);

				InOther.QuantizationExponent = DEFAULT_EXPONENT;
				InOther.ObjectPathToErrorStatus.Empty();
				InOther.ProcessedStaticMeshes.Empty();
			}

			return *this;
		}

		inline int32 GetQuantizationExponent()const
		{
			return QuantizationExponent;
		}

		inline const TMap<FString, EStatus>& GetObjectPathToErrorStatus()const
		{
			return ObjectPathToErrorStatus;
		}

		inline const TArray<FStaticMesh*>& GetProcessedStaticMeshes()const
		{
			return ProcessedStaticMeshes;
		}

		//Save to disk eg. [D:\MyWorkspace\UnrealProjects\SanitizerTest\Saved\Test.bin]
		static bool SaveTo(TUniquePtr<FRegistry>& InRegistry, const FString& InSaveFileName);

		//Load from disk eg. [D:\MyWorkspace\UnrealProjects\SanitizerTest\Saved\Test.bin]
		static bool LoadFrom(TUniquePtr<FRegistry>& InRegistry, const FString& InLoadFileName);

		static TUniquePtr<FRegistry> PreprocessStaticMeshes(const TSet<const UStaticMesh*>& InStaticMeshesToProcess, const FSettings& InSettings);

		static TUniquePtr<FRegistry> PreprocessStaticMeshes(const TSet<FString>& InStaticMeshObjectPaths, const FSettings& InSettings);

		friend FArchive& operator<<(FArchive& InAr, FRegistry& InRegistry)
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
					FStaticMesh* QuantizedStaticMesh = InRegistry.Allocate();
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
		int32 QuantizationExponent = DEFAULT_EXPONENT;
		TMap<FString, EStatus> ObjectPathToErrorStatus = {};
		TArray<FStaticMesh*> ProcessedStaticMeshes = {};

		//Try to find the quantized static mesh pointer in this registry.
		//Returns nullptr if not found:
		//1.Static mesh is not in this registry
		//2.Static mesh is in this registry but error occurred when preprocessing it
		const FStaticMesh* TryFind(const FString& InStaticMeshObjectPath)const;

		//Get the status of a static mesh in this registry
		EStatus GetStatusInThisRegistry(const FString& InStaticMeshObjectPath)const;

		//Allocate an instance of FStaticMesh
		[[nodiscard]] FStaticMesh* Allocate()const;

		//Allocate an instance of FStaticMesh and add it to ProcessedStaticMeshes
		FStaticMesh* AllocateAndAddToThisRegistry(const FString& InStaticMeshObjectPath);
	};
}
