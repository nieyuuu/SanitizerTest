#include "MeshPreprocessor.h"

#include "StaticMeshCompiler.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"

namespace StaticMeshPreprocessor
{
	FString StatusToString(EStatus InStatus)
	{
		switch (InStatus)
		{
		case EStatus::Unknown:
			return TEXT("Unknown");
		case EStatus::NoError:
			return TEXT("NoError");
		case EStatus::InvalidObjectPath:
			return TEXT("InvalidObjectPath");
		case EStatus::LOD0SourceModelNotFound:
			return TEXT("LOD0SourceModelNotFound");
		case EStatus::MeshDescriptionNotFound:
			return TEXT("MeshDescriptionNotFound");
		case EStatus::PositionBufferContainsNaN:
			return TEXT("PositionBufferContainsNaN");
		default:
			check(false);
			break;
		}

		return TEXT("");
	}

	const FStaticMesh* FRegistry::TryFind(const FString& InStaticMeshObjectPath)const
	{
		const FStaticMesh* const* pResult = ProcessedStaticMeshes.FindByPredicate([&](const FStaticMesh* InQuantizedStaticMesh) {
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

	EStatus FRegistry::GetStatusInThisRegistry(const FString& InStaticMeshObjectPath)const
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

	FStaticMesh* FRegistry::Allocate()const
	{
		return new FStaticMesh();
	}

	FStaticMesh* FRegistry::AllocateAndAddToThisRegistry(const FString& InStaticMeshObjectPath)
	{
		checkf(TryFind(InStaticMeshObjectPath) == nullptr, TEXT("An entry of [%s] already exists in this registry."), *InStaticMeshObjectPath);

		FStaticMesh* Result = Allocate();
		Result->QuantizationExponent = QuantizationExponent;
		Result->StaticMeshObjectPath = InStaticMeshObjectPath;

		ProcessedStaticMeshes.Add(Result);

		return Result;
	}

	bool FRegistry::SaveTo(TUniquePtr<FRegistry>& InRegistry, const FString& InSaveFileName)
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

	bool FRegistry::LoadFrom(TUniquePtr<FRegistry>& InRegistry, const FString& InLoadFileName)
	{
		if (InRegistry.Get() == nullptr)
		{
			InRegistry = MakeUnique<FRegistry>();
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

		return true;
	}

	TUniquePtr<FRegistry> FRegistry::PreprocessStaticMeshes(const TSet<const UStaticMesh*>& InStaticMeshesToProcess, const FSettings& InSettings)
	{
		check(IsInGameThread());

		TUniquePtr<FRegistry> OutRegistry = MakeUnique<FRegistry>(InSettings.GetQuantizationExponent());

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

		auto LoopBody = [&](FRegistry& Context, int32 Index) {
			FTaskTagScope TaskTag(ETaskTag::EParallelGameThread);

			const UStaticMesh* StaticMesh = StaticMeshesToProcess[Index];
			const FString StaticMeshObjectPath = StaticMesh->GetPathName();

			//Preprocess LOD0 source model
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

			FStaticMesh* QuantizedStaticMesh = Context.AllocateAndAddToThisRegistry(StaticMeshObjectPath);
			FBox& BoundingBox = QuantizedStaticMesh->BoundingBox;
			TArray<FInt64Vector3>& QuantizedPositionBuffer = QuantizedStaticMesh->QuantizedPositionBuffer;

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

					Context.ObjectPathToErrorStatus.Add(StaticMeshObjectPath, EStatus::PositionBufferContainsNaN);
					return;
				}

				BoundingBox += FVector(VertexPositions[VertexID]);

				FInt64Vector3 QuantizedVector = {};

				//int64 overflow/underflow?
				QuantizedVector.X = int64((double)Position.X * FMath::Pow(10.0, (double)Context.QuantizationExponent));
				QuantizedVector.Y = int64((double)Position.Y * FMath::Pow(10.0, (double)Context.QuantizationExponent));
				QuantizedVector.Z = int64((double)Position.Z * FMath::Pow(10.0, (double)Context.QuantizationExponent));

				QuantizedPositionBuffer.Add(MoveTemp(QuantizedVector));
			}

			check(BoundingBox.IsValid && !BoundingBox.ContainsNaN());

			//Sort position buffer here
			QuantizedPositionBuffer.Sort([](const FInt64Vector3& A, const FInt64Vector3& B) {
				return A.X != B.X ? A.X < B.X :
					A.Y != B.Y ? A.Y < B.Y :
					A.Z < B.Z;
				});
			};

		auto ContextConstructor = [=](int32 ContextIndex, int32 NumContexts) {
			return FRegistry(InSettings.GetQuantizationExponent());
			};

		TArray<FRegistry> ParallelForContexts;

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

		auto MergeRegistries = [](TArray<FRegistry>& InRegistriesToMerge, FRegistry& OutRegistry) {
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
				TMap<FString, EStatus>& ObjectPathToErrorStatus = Registry.ObjectPathToErrorStatus;
				TArray<FStaticMesh*>& ProcessedStaticMeshes = Registry.ProcessedStaticMeshes;

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

		const bool MergeSucceed = MergeRegistries(ParallelForContexts, *OutRegistry.Get());

		check(MergeSucceed);
		check((OutRegistry.Get()->ObjectPathToErrorStatus.Num() + OutRegistry.Get()->ProcessedStaticMeshes.Num()) == StaticMeshesToProcess.Num());

		const double EndTime = FPlatformTime::Seconds();

		UE_LOG(LogAssetSanitizer, Display, TEXT("Finish preprocessing static meshes in %f seconds."), EndTime - StartTime);

		return MoveTemp(OutRegistry);
	}

	TUniquePtr<FRegistry> FRegistry::PreprocessStaticMeshes(const TSet<FString>& InStaticMeshObjectPaths, const FSettings& InSettings)
	{
		check(IsInGameThread());

		TSet<const UStaticMesh*> StaticMeshesToProcess;
		TMap<FString, EStatus> InvalidObjectPathList;

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
				InvalidObjectPathList.Add(ObjectPath, EStatus::InvalidObjectPath);
			}
			else
			{
				check(StaticMesh->IsValidLowLevel());
				StaticMeshesToProcess.Add(StaticMesh);
			}
		}

		TUniquePtr<FRegistry> OutRegistry = PreprocessStaticMeshes(StaticMeshesToProcess, InSettings);

		if (InvalidObjectPathList.Num())
		{
			OutRegistry.Get()->ObjectPathToErrorStatus.Append(MoveTemp(InvalidObjectPathList));
		}

		return MoveTemp(OutRegistry);
	}
}
