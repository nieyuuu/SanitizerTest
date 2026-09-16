#pragma once

#include "LogAssetSanitizer.h"

#include "CoreMinimal.h"
#include "Math/MathFwd.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include "MeshPreprocessor.generated.h"

constexpr int32 MIN_EXPONENT	 = -2;
constexpr int32 MAX_EXPONENT	 = 7;
constexpr int32 DEFAULT_EXPONENT = 0;

namespace StaticMeshPreprocessor
{
	UENUM()
	enum class EStaticMeshStatus :uint8
	{
		Unknown,
		NoError,
		InvalidObjectPath,
		LOD0SourceModelNotFound,
		MeshDescriptionNotFound,
		PositionBufferContainsNaN
	};

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
			InAr << InQuantizedStaticMesh.StaticMeshObjectPath;
			InAr << InQuantizedStaticMesh.BoundingBox;
			InAr << InQuantizedStaticMesh.QuantizedPositionBuffer;

			return InAr;
		}

	private:
		//Private constructor
		FStaticMesh() = default;

		int32 QuantizationExponent = DEFAULT_EXPONENT;
		FString StaticMeshObjectPath;
		FBox BoundingBox;
		TArray<FInt64Vector3> QuantizedPositionBuffer;
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
		const FStaticMesh* TryFind(const FString& InStaticMeshObjectPath)const;

		inline const TArray<FStaticMesh*>& GetProcessedStaticMeshes()const
		{
			return ProcessedStaticMeshes;
		}

		//Get the status of a static mesh in this registry
		EStaticMeshStatus GetStatusInThisRegistry(const FString& InStaticMeshObjectPath)const;

		inline const TMap<FString, EStaticMeshStatus>& GetObjectPathToErrorStatus()const
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
		static bool SaveTo(TUniquePtr<FRegistry>& InRegistry, const FString& InSaveFileName);

		//Load from disk eg. [D:\MyWorkspace\UnrealProjects\SanitizerTest\Saved\Test.bin]
		static bool LoadFrom(TUniquePtr<FRegistry>& InRegistry, const FString& InLoadFileName);

		static TUniquePtr<FRegistry> PreprocessStaticMeshes(const TSet<const UStaticMesh*>& InStaticMeshesToProcess, FSettings InSettings);

		//FString is default case insensitive but this might be ok because there cant be two assets SM_Asset/SM_asset under same folder
		//And there cant be two sub-folsers Folder/folder under same folder
		//TSet<FString, FLocKeySetFuncs, FDefaultSetAllocator> will handle case sensitive of FString
		static TUniquePtr<FRegistry> PreprocessStaticMeshes(const TSet<FString>& InStaticMeshObjectPaths, FSettings InSettings);

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

					//RTTI is disabled
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
		//TMap<FString, EStaticMeshStatus, FDefaultSetAllocator, FLocKeyMapFuncs<EStaticMeshStatus>> will handle case sensitive of FString
		TMap<FString, EStaticMeshStatus> ObjectPathToErrorStatus;
		TArray<FStaticMesh*> ProcessedStaticMeshes;

		//Allocate an instance of FStaticMesh and set its QuantizationExponent
		[[nodiscard]] FStaticMesh* Allocate()const;

		//Allocate an instance of FStaticMesh and add it to ProcessedStaticMeshes
		FStaticMesh* AllocateAndAddToThisRegistry(const FString& InStaticMeshObjectPath);
	};
}
