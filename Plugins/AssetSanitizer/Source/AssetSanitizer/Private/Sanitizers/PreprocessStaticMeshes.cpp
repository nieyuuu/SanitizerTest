#include "PreprocessStaticMeshes.h"

#include "StaticMeshCompiler.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"

struct FDefaultWeightCalculator
{
	inline double operator()(const FAssetData& InAssetData)const
	{
		return 1.0;
	}
};

struct FDiskSizeWeightCalculator
{
	//TODO: Consider the size of referenced textures
	inline double operator()(const FAssetData& InAssetData)const
	{
		const FString PackagePath = InAssetData.PackageName.ToString();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackagePath);

		/*FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName);
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		TArray<FName> Dependencies;
		AssetRegistry.GetDependencies(*PackagePath, Dependencies);*/

		const FString UAssetPath = FPaths::SetExtension(PackageFileName, TEXT(".uasset"));
		const FString UExpPath = FPaths::SetExtension(PackageFileName, TEXT(".uexp"));
		const FString UBulkPath = FPaths::SetExtension(PackageFileName, TEXT(".ubulk"));

		int64 TotalSize = 0;
		if (IFileManager::Get().FileExists(*UAssetPath))
		{
			TotalSize += IFileManager::Get().FileSize(*UAssetPath);
		}
		//Cooked editor?
		if (IFileManager::Get().FileExists(*UExpPath))
		{
			TotalSize += IFileManager::Get().FileSize(*UExpPath);
		}
		//Cooked editor?
		if (IFileManager::Get().FileExists(*UBulkPath))
		{
			TotalSize += IFileManager::Get().FileSize(*UBulkPath);
		}

		return TotalSize / (1024.0 * 1024.0);
	}
};

TArray<TArray<FString>> FPreprocessBalancer::BalanceStaticMeshes(const TArray<FString>& InDirsToProcess, int32 InNumOfBatches)
{
	return BalanceStaticMeshesImp(FDefaultWeightCalculator(), InDirsToProcess, InNumOfBatches);
}

TArray<TArray<FString>> FPreprocessBalancer::BalanceStaticMeshesBasedOnDiskSize(const TArray<FString>& InDirsToProcess, int32 InNumOfBatches)
{
	return BalanceStaticMeshesImp(FDiskSizeWeightCalculator(), InDirsToProcess, InNumOfBatches);
}

FString PreprocessStatusToString(EPreprocessStatus InStatus)
{
	check(InStatus >= EPreprocessStatus::Unknown && InStatus <= EPreprocessStatus::PositionBufferContainsNaN);

	static FString StatusTable[] = {
		FString("UnKnown"),
		FString("NoError"),
		FString("InvalidObjectPath"),
		FString("LOD0SourceModelNotFound"),
		FString("MeshDescriptionNotFound"),
		FString("PositionBufferContainsNaN")
	};

	return StatusTable[int32(InStatus)];
}

namespace StaticMeshPreprocessing
{
	const FQuantizedStaticMesh* FPreprocessRegistry::TryFind(const FString& InStaticMeshObjectPath)const
	{
		const FQuantizedStaticMesh *const *pResult = ProcessedStaticMeshes.FindByPredicate([&](const FQuantizedStaticMesh* InQuantizedStaticMesh) {
			return InQuantizedStaticMesh->StaticMeshObjectPath == InStaticMeshObjectPath;
		});

		if (pResult != nullptr)
		{
			check((*pResult)->QuantizationExponent == QuantizationExponent);
			return *pResult;
		}
		else
		{
			return nullptr;
		}
	}

	EPreprocessStatus FPreprocessRegistry::GetStatusInThisRegistry(const FString& InStaticMeshObjectPath)const
	{
		if (ObjectPathToErrorStatus.Contains(InStaticMeshObjectPath))
		{
			check(ObjectPathToErrorStatus[InStaticMeshObjectPath] > EPreprocessStatus::NoError);
			return ObjectPathToErrorStatus[InStaticMeshObjectPath];
		}
		else if (TryFind(InStaticMeshObjectPath) != nullptr)
		{
			return EPreprocessStatus::NoError;
		}

		return EPreprocessStatus::Unknown;
	}

	FQuantizedStaticMesh* FPreprocessRegistry::Allocate()const
	{
		return new FQuantizedStaticMesh();
	}

	FQuantizedStaticMesh* FPreprocessRegistry::AllocateAndAddToThisRegistry(const FString& InStaticMeshObjectPath)
	{
		checkf(TryFind(InStaticMeshObjectPath) == nullptr, TEXT("An entry of [%s] already exists in this registry."), *InStaticMeshObjectPath);

		FQuantizedStaticMesh* Result = Allocate();
		Result->QuantizationExponent = QuantizationExponent;
		Result->StaticMeshObjectPath = InStaticMeshObjectPath;

		ProcessedStaticMeshes.Add(Result);

		return Result;
	}

	bool FPreprocessRegistry::SaveTo(TUniquePtr<FPreprocessRegistry>& InRegistry, const FString& InSaveFileName)
	{
		if (InRegistry.Get() == nullptr)
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Attempt to save a null registry pointer to file %s."), *InSaveFileName);
			return false;
		}

		if (!InSaveFileName.EndsWith(TEXT(".bin")))
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Archive file should have [.bin] extension. Input file is %s."), *InSaveFileName);
			return false;
		}

		TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*InSaveFileName));
		if (FileWriter.Get() == nullptr)
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to create file writer when saving registry to %s"), *InSaveFileName);
			return false;
		}

		*FileWriter << *InRegistry.Get();
		if (!FileWriter->Close())
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to save registry to %s"), *InSaveFileName);
			return false;
		}

		return true;
	}

	bool FPreprocessRegistry::LoadFrom(TUniquePtr<FPreprocessRegistry>& InRegistry, const FString& InLoadFileName)
	{
		if (InRegistry.Get() == nullptr)
		{
			InRegistry = MakeUnique<FPreprocessRegistry>();
		}

		if (!InLoadFileName.EndsWith(TEXT(".bin")))
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Archive file should have [.bin] extension. Input file is %s."), *InLoadFileName);
			return false;
		}

		if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*InLoadFileName))
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Archive file %s does not exist."), *InLoadFileName);
			return false;
		}

		TUniquePtr<FArchive> FileReader = TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*InLoadFileName));
		if (FileReader.Get() == nullptr)
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to create file reader when loading registry from %s"), *InLoadFileName);
			return false;
		}

		*FileReader << *InRegistry.Get();
		if (!FileReader->Close())
		{
			UE_LOG(LogAssetSanitizer, Error, TEXT("Failed to load registry from %s"), *InLoadFileName);
			return false;
		}

		InRegistry.Get()->bFromDiskFile = true;
		InRegistry.Get()->FileName = InLoadFileName;

		return true;
	}

	TUniquePtr<FPreprocessRegistry> FPreprocessRegistry::PreprocessStaticMeshes(const TSet<const UStaticMesh*>& InStaticMeshesToProcess, FPreprocessSettings InSettings)
	{
		check(IsInGameThread());

		TUniquePtr<FPreprocessRegistry> OutRegistry = MakeUnique<FPreprocessRegistry>(InSettings.GetQuantizationExponent());

		if (!InStaticMeshesToProcess.Num())
		{
			UE_LOG(LogAssetSanitizer, Warning, TEXT("Empty static mesh set to preprocess."));
			return OutRegistry;
		}

		const TArray<const UStaticMesh*> StaticMeshesToProcess = InStaticMeshesToProcess.Array();

		TArray<UStaticMesh*> PendingStaticMeshes;
		for (const UStaticMesh* StaticMesh : StaticMeshesToProcess)
		{
			checkf(StaticMesh != nullptr && StaticMesh->IsValidLowLevel(), TEXT("Invalid UStaticMesh pointer."));
			if (StaticMesh->IsCompiling())
			{
				PendingStaticMeshes.Add(const_cast<UStaticMesh*>(StaticMesh));
			}
		}
		if (!PendingStaticMeshes.IsEmpty())
		{
			UE_LOG(LogAssetSanitizer, Display, TEXT("Waiting for static meshes to finish compiling."));
			FStaticMeshCompilingManager::Get().FinishCompilation(PendingStaticMeshes);
		}

		auto LoopBody = [&](FPreprocessRegistry& Context, int32 Index) {
			FTaskTagScope TaskTag(ETaskTag::EParallelGameThread);

			const UStaticMesh* StaticMesh = StaticMeshesToProcess[Index];
			const FString StaticMeshObjectPath = StaticMesh->GetPathName();

			//Preprocess LOD0 source model
			if (!StaticMesh->IsSourceModelValid(0))
			{
				Context.ObjectPathToErrorStatus.Add(StaticMeshObjectPath, EPreprocessStatus::LOD0SourceModelNotFound);
				return;
			}

			const FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(0);
			if (!MeshDescription)
			{
				Context.ObjectPathToErrorStatus.Add(StaticMeshObjectPath, EPreprocessStatus::MeshDescriptionNotFound);
				return;
			}

			const TMeshElementContainer<FVertexID>& Vertices = MeshDescription->Vertices();
			const TVertexAttributesRef<const FVector3f>& VertexPositions = MeshDescription->GetVertexPositions();

			FQuantizedStaticMesh* QuantizedStaticMesh = Context.AllocateAndAddToThisRegistry(StaticMeshObjectPath);
			FBox& BoundingBox = QuantizedStaticMesh->FetchBoundingBox();
			TArray<FQuantizedVector>& QuantizedPositionBuffer = QuantizedStaticMesh->FetchQuantizedPositionBuffer();

			QuantizedPositionBuffer.Reserve(VertexPositions.GetRawArray().Num());

			for (const FVertexID& VertexID : Vertices.GetElementIDs())
			{
				check(MeshDescription->IsVertexValid(VertexID));

				//We dont care if a vertex is orphaned or not
				//const bool bOrphanedVertex = MeshDescription->IsVertexOrphaned(VertexID);

				const FVector3f& Position = VertexPositions[VertexID];
				if (Position.ContainsNaN())
				{
					Context.ProcessedStaticMeshes.RemoveSwap(QuantizedStaticMesh);
					delete QuantizedStaticMesh;

					Context.ObjectPathToErrorStatus.Add(StaticMeshObjectPath, EPreprocessStatus::PositionBufferContainsNaN);
					return;
				}

				BoundingBox += FVector(VertexPositions[VertexID]);

				FQuantizedVector QuantizedVector;

				//int64 overflow/underflow?
				QuantizedVector.X = int64((double)Position.X * FMath::Pow(10.0, (double)Context.QuantizationExponent));
				QuantizedVector.Y = int64((double)Position.Y * FMath::Pow(10.0, (double)Context.QuantizationExponent));
				QuantizedVector.Z = int64((double)Position.Z * FMath::Pow(10.0, (double)Context.QuantizationExponent));

				QuantizedPositionBuffer.Add(MoveTemp(QuantizedVector));
			}

			check(BoundingBox.IsValid && !BoundingBox.ContainsNaN());

			//Sort position buffer here
			QuantizedPositionBuffer.Sort();
		};

		auto ContextConstructor = [=](int32 ContextIndex, int32 NumContexts) {
			return FPreprocessRegistry(InSettings.GetQuantizationExponent());
		};

		TArray<FPreprocessRegistry> ParallelForContexts;

		UE_LOG(LogAssetSanitizer, Display, TEXT("Start preprocessing [%d] static meshes."), StaticMeshesToProcess.Num());

		const double StartTime = FPlatformTime::Seconds();

		ParallelForWithTaskContext(
			TEXT("ParallelPreprocessStaticMeshes"),
			ParallelForContexts,
			StaticMeshesToProcess.Num(),
			32,
			MoveTemp(ContextConstructor),
			MoveTemp(LoopBody),
			EParallelForFlags::Unbalanced);

		auto MergeRegistries = [](TArray<FPreprocessRegistry>& InRegistriesToMerge, FPreprocessRegistry& OutRegistry) {
			const int32 QuantizeExponent = OutRegistry.QuantizationExponent;
			for (const auto& Registry : InRegistriesToMerge)
			{
				if (Registry.QuantizationExponent != QuantizeExponent)
				{
					return false;
				}
			}

			for (auto& Registry : InRegistriesToMerge)
			{
				TMap<FString, EPreprocessStatus>& ObjectPathToErrorStatus = Registry.ObjectPathToErrorStatus;
				TArray<FQuantizedStaticMesh*>& ProcessedStaticMeshes = Registry.ProcessedStaticMeshes;

				for (const auto& Tuple : ObjectPathToErrorStatus)
				{
					if (OutRegistry.GetStatusInThisRegistry(Tuple.Key) != EPreprocessStatus::Unknown)
						return false;
				}
				for (const auto& StaticMesh : ProcessedStaticMeshes)
				{
					if (OutRegistry.GetStatusInThisRegistry(StaticMesh->GetStaticMeshObjectPath()) != EPreprocessStatus::Unknown)
						return false;
				}

				OutRegistry.ObjectPathToErrorStatus.Append(ObjectPathToErrorStatus);
				OutRegistry.ProcessedStaticMeshes.Append(ProcessedStaticMeshes);

				Registry.QuantizationExponent = DEFAULT_EXPONENT;
				Registry.ObjectPathToErrorStatus.Empty();
				Registry.ProcessedStaticMeshes.Empty();
			}

			return true;
		};

		const bool MergeSucceed = MergeRegistries(ParallelForContexts, *OutRegistry.Get());
		
		check(MergeSucceed);
		check((OutRegistry.Get()->ObjectPathToErrorStatus.Num() + OutRegistry.Get()->ProcessedStaticMeshes.Num()) == StaticMeshesToProcess.Num());

		const double EndTime = FPlatformTime::Seconds();

		UE_LOG(LogAssetSanitizer, Display, TEXT("Finish preprocessing static meshes in %f seconds."), EndTime - StartTime);

		return MoveTemp(OutRegistry);
	}

	TUniquePtr<FPreprocessRegistry> FPreprocessRegistry::PreprocessStaticMeshes(const TSet<FString>& InStaticMeshObjectPaths, FPreprocessSettings InSettings)
	{
		check(IsInGameThread());

		TSet<const UStaticMesh*> StaticMeshesToProcess;
		TMap<FString, EPreprocessStatus> InvalidObjectPathList;

		StaticMeshesToProcess.Reserve(InStaticMeshObjectPaths.Num());
		for (const FString& ObjectPath : InStaticMeshObjectPaths)
		{
			UStaticMesh* StaticMesh = FindObject<UStaticMesh>(nullptr, *ObjectPath);

			if (StaticMesh == nullptr)
			{
				uint32 LoadFlags = LOAD_None;
				/*if (IsRunningCommandlet())
				{
					LoadFlags |= LOAD_SkipLoadImportedPackages;
				}*/
				StaticMesh = LoadObject<UStaticMesh>(nullptr, *ObjectPath, nullptr, LoadFlags);
			}

			if (StaticMesh == nullptr)
			{
				FSoftObjectPath SoftObjectPath(ObjectPath);
				StaticMesh = Cast<UStaticMesh>(SoftObjectPath.TryLoad());
			}

			if (StaticMesh == nullptr)
			{
				InvalidObjectPathList.Add(ObjectPath, EPreprocessStatus::InvalidObjectPath);
			}
			else
			{
				check(StaticMesh->IsValidLowLevel());
				StaticMeshesToProcess.Add(StaticMesh);
			}
		}

		TUniquePtr<FPreprocessRegistry> OutRegistry = PreprocessStaticMeshes(StaticMeshesToProcess, InSettings);

		if (InvalidObjectPathList.Num())
		{
			OutRegistry.Get()->ObjectPathToErrorStatus.Append(MoveTemp(InvalidObjectPathList));
		}

		return MoveTemp(OutRegistry);
	}
}
