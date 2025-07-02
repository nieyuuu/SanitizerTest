#include "PreprocessStaticMeshes.h"

#include "StaticMeshCompiler.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"

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

	EStatus FPreprocessRegistry::GetStatusInThisRegistry(const FString& InStaticMeshObjectPath)const
	{
		if (ObjectPathToErrorStatus.Contains(InStaticMeshObjectPath))
		{
			check(ObjectPathToErrorStatus[InStaticMeshObjectPath] > EStatus::NoError);
			return ObjectPathToErrorStatus[InStaticMeshObjectPath];
		}
		else if (TryFind(InStaticMeshObjectPath) != nullptr)
		{
			return EStatus::NoError;
		}

		return EStatus::Unknown;
	}

	FQuantizedStaticMesh* FPreprocessRegistry::Allocate()const
	{
		FQuantizedStaticMesh* Result = new FQuantizedStaticMesh();
		Result->QuantizationExponent = QuantizationExponent;

		return Result;
	}

	FQuantizedStaticMesh* FPreprocessRegistry::AllocateAndAddToThisRegistry(const FString& InStaticMeshObjectPath)
	{
		checkf(TryFind(InStaticMeshObjectPath) == nullptr, TEXT("An entry of [%s] already exists in this registry."), *InStaticMeshObjectPath);

		FQuantizedStaticMesh* Result = Allocate();
		Result->StaticMeshObjectPath = InStaticMeshObjectPath;

		ProcessedStaticMeshes.Add(Result);

		return Result;
	}

	FPreprocessRegistry PreprocessStaticMeshes(const TSet<const UStaticMesh*>& InStaticMeshesToProcess, FSettings InSettings)
	{
		check(IsInGameThread());

		FPreprocessRegistry OutRegistry(InSettings.GetQuantizationExponent());

		if (!InStaticMeshesToProcess.Num())
		{
			return MoveTemp(OutRegistry);
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
			UE_LOG(LogAssetSanitiser, Display, TEXT("Waiting for static meshes to finish compiling."));
			FStaticMeshCompilingManager::Get().FinishCompilation(PendingStaticMeshes);
		}

		auto LoopBody = [&](FPreprocessRegistry& Context, int32 Index) {
			FTaskTagScope TaskTag(ETaskTag::EParallelGameThread);

			const UStaticMesh* StaticMesh = StaticMeshesToProcess[Index];
			const FString StaticMeshObjectPath = StaticMesh->GetPathName();

			if (!StaticMesh->IsSourceModelValid(0))
			{
				Context.ObjectPathToErrorStatus.Add(StaticMeshObjectPath, EStatus::LOD0SourceModelNotFound);
				return;
			}

			const FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(0);
			if (!MeshDescription)
			{
				Context.ObjectPathToErrorStatus.Add(StaticMeshObjectPath, EStatus::MeshDescriptionNotFound);
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
				//const bool OrphanedVertex = MeshDescription->IsVertexOrphaned(VertexID);

				const FVector3f& Position = VertexPositions[VertexID];
				if (Position.ContainsNaN())
				{
					Context.ProcessedStaticMeshes.RemoveSwap(QuantizedStaticMesh);
					delete QuantizedStaticMesh;

					Context.ObjectPathToErrorStatus.Add(StaticMeshObjectPath, EStatus::PositionBufferContainsNaN);
					return;
				}

				BoundingBox += FVector(VertexPositions[VertexID]);

				FQuantizedVector QuantizedVector;

				QuantizedVector.X = int64((double)Position.X * FMath::Pow(10.0, (double)Context.QuantizationExponent));
				QuantizedVector.Y = int64((double)Position.Y * FMath::Pow(10.0, (double)Context.QuantizationExponent));
				QuantizedVector.Z = int64((double)Position.Z * FMath::Pow(10.0, (double)Context.QuantizationExponent));

				QuantizedPositionBuffer.Add(MoveTemp(QuantizedVector));
			}

			check(BoundingBox.IsValid && !BoundingBox.ContainsNaN());

			QuantizedPositionBuffer.Sort();
		};

		auto ContextConstructor = [=](int32 ContextIndex, int32 NumContexts) {
			return FPreprocessRegistry(InSettings.GetQuantizationExponent());
		};

		TArray<FPreprocessRegistry> ParallelForContexts;

		UE_LOG(LogAssetSanitiser, Display, TEXT("Start preprocessing [%d] static meshes."), StaticMeshesToProcess.Num());

		const double StartTime = FPlatformTime::Seconds();

		ParallelForWithTaskContext(
			TEXT("ParallelPreprocessStaticMeshes"),
			ParallelForContexts,
			StaticMeshesToProcess.Num(),
			128,
			MoveTemp(ContextConstructor),
			MoveTemp(LoopBody),
			EParallelForFlags::Unbalanced);

		auto MergeRegistries = [](TArray<FPreprocessRegistry>& InRegistriesToMerge, FPreprocessRegistry& OutRegistry) {
			const uint32 QuantizeExponent = OutRegistry.QuantizationExponent;
			for (const auto& Registry : InRegistriesToMerge)
			{
				if (Registry.QuantizationExponent != QuantizeExponent)
				{
					return false;
				}
			}

			for (auto& Registry : InRegistriesToMerge)
			{
				TMap<FString, EStatus>& ObjectPathToErrorStatus = Registry.ObjectPathToErrorStatus;
				TArray<FQuantizedStaticMesh*>& ProcessedStaticMeshes = Registry.ProcessedStaticMeshes;

				for (const auto& Tuple : ObjectPathToErrorStatus)
				{
					if (OutRegistry.GetStatusInThisRegistry(Tuple.Key) != EStatus::Unknown)
						return false;
				}
				for (const auto& StaticMesh : ProcessedStaticMeshes)
				{
					if (OutRegistry.GetStatusInThisRegistry(StaticMesh->GetStaticMeshObjectPath()) != EStatus::Unknown)
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

		check(MergeRegistries(ParallelForContexts, OutRegistry));
		check((OutRegistry.ObjectPathToErrorStatus.Num() + OutRegistry.ProcessedStaticMeshes.Num()) == StaticMeshesToProcess.Num());

		const double EndTime = FPlatformTime::Seconds();

		UE_LOG(LogAssetSanitiser, Display, TEXT("Finish preprocessing static meshes in %f seconds."), EndTime - StartTime);

		return MoveTemp(OutRegistry);
	}

	FPreprocessRegistry PreprocessStaticMeshes(const TSet<FString>& InStaticMeshObjectPaths, FSettings InSettings)
	{
		check(IsInGameThread());

		FPreprocessRegistry OutRegistry(InSettings.GetQuantizationExponent());

		if (!InStaticMeshObjectPaths.Num())
		{
			return MoveTemp(OutRegistry);
		}

		TSet<const UStaticMesh*> StaticMeshesToProcess;
		TMap<FString, EStatus> InvalidObjectPathList;

		StaticMeshesToProcess.Reserve(InStaticMeshObjectPaths.Num());
		for (const FString& ObjectPath : InStaticMeshObjectPaths)
		{
			UStaticMesh* StaticMesh = FindObject<UStaticMesh>(nullptr, *ObjectPath);

			if (StaticMesh == nullptr)
			{
				StaticMesh = LoadObject<UStaticMesh>(nullptr, *ObjectPath);
			}

			if (StaticMesh == nullptr)
			{
				FSoftObjectPath SoftObjectPath(ObjectPath);
				StaticMesh = Cast<UStaticMesh>(SoftObjectPath.TryLoad());
			}

			if (StaticMesh == nullptr)
			{
				InvalidObjectPathList.Add(ObjectPath, EStatus::InvalidObjectPath);
			}
			else
			{
				check(StaticMesh->IsValidLowLevel());
				StaticMeshesToProcess.Add(StaticMesh);
			}
		}

		OutRegistry = PreprocessStaticMeshes(StaticMeshesToProcess, InSettings);

		if (InvalidObjectPathList.Num())
		{
			OutRegistry.ObjectPathToErrorStatus.Append(MoveTemp(InvalidObjectPathList));
		}

		return MoveTemp(OutRegistry);
	}

	bool SaveTo(const FString& InSaveFileName, FPreprocessRegistry& InRegistry)
	{
		TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*InSaveFileName));
		if (FileWriter.Get() == nullptr)
		{
			UE_LOG(LogAssetSanitiser, Warning, TEXT("Failed to create file writer when saving registry to %s"), *InSaveFileName);
			return false;
		}

		*FileWriter << InRegistry;
		if (!FileWriter->Close())
		{
			UE_LOG(LogAssetSanitiser, Warning, TEXT("Failed to save registry to %s"), *InSaveFileName);
			return false;
		}

		return true;
	}

	bool LoadFrom(const FString& InLoadFileName, FPreprocessRegistry& InRegistry)
	{
		if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*InLoadFileName))
		{
			UE_LOG(LogAssetSanitiser, Warning, TEXT("%s does not exist"), *InLoadFileName);
			return false;
		}

		TUniquePtr<FArchive> FileReader = TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*InLoadFileName));
		if (FileReader.Get() == nullptr)
		{
			UE_LOG(LogAssetSanitiser, Warning, TEXT("Failed to create file reader when loading registry from %s"), *InLoadFileName);
			return false;
		}

		*FileReader << InRegistry;
		if (!FileReader->Close())
		{
			UE_LOG(LogAssetSanitiser, Warning, TEXT("Failed to load registry from %s"), *InLoadFileName);
			return false;
		}

		return true;
	}
}
