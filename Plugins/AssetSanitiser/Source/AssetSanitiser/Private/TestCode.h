#pragma once

#include "LogAssetSanitiser.h"
#include "Sanitisers/MeshSimilarityAnalyzer.h"

#include "AssetRegistry/AssetRegistryModule.h"

void GetAllStaticMeshes()
{
    auto OuterTask = []() {
		FAssetRegistryModule& Registry = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
		while (Registry.Get().IsLoadingAssets()) {}

        auto InnerTask = []() {
            FAssetRegistryModule& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");

            FARFilter Filter;
            Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
            Filter.PackagePaths.Add("/Game");
            Filter.bRecursivePaths = true;
            Filter.bIncludeOnlyOnDiskAssets = true;

            TArray<FAssetData> StaticMeshAssetDatas;
            AssetRegistry.Get().GetAssets(Filter, StaticMeshAssetDatas);

            TSet<const UStaticMesh*> StaticMeshesToProcess;
            TSet<FString> StaticMeshObjectPaths;

            for (const FAssetData& AssetData : StaticMeshAssetDatas)
            {
                UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *AssetData.ObjectPath.ToString());
                StaticMeshesToProcess.Add(StaticMesh);
                StaticMeshObjectPaths.Add((AssetData.GetObjectPathString()));
            }

            {
                FPreprocessSettings PreprocessSettings(6);
                FPreprocessRegistry PreprocessRegistry = StaticMeshPreprocessing::PreprocessStaticMeshes(StaticMeshObjectPaths, PreprocessSettings);

                {
                    const FString FileName = FString::Printf(TEXT("MyArchiveTest.bin"));
                    const FString SavePath = FPaths::ProjectSavedDir() + FileName;

                    StaticMeshPreprocessing::SaveTo(SavePath, PreprocessRegistry);

                    FPreprocessRegistry DeserializedRegistry;
                    StaticMeshPreprocessing::LoadFrom(SavePath, DeserializedRegistry);

                    TArray<TArray<FString>> Results;
                    FPerVertexAnalyzer PerVertexAnalyzer({ &DeserializedRegistry });
                    PerVertexAnalyzer.Analyzes(Results);

                    FXxHash64Analyzer MemoryHashAnalyzer64({ &DeserializedRegistry });
                    MemoryHashAnalyzer64.Analyzes(Results);

                    FXxHash128Analyzer MemoryHashAnalyzer128({ &DeserializedRegistry });
                    MemoryHashAnalyzer128.Analyzes(Results);
                }
            }
            {
                FPreprocessSettings PreprocessSettings(5);
                FPreprocessRegistry PreprocessRegistry = StaticMeshPreprocessing::PreprocessStaticMeshes(StaticMeshesToProcess, PreprocessSettings);

                TArray<TArray<const FPreprocessedStaticMesh*>> Results;
                FPerVertexAnalyzer PerVertexAnalyzer({ &PreprocessRegistry });
                PerVertexAnalyzer.Analyzes(Results);

                FXxHash64Analyzer MemoryHashAnalyzer64({ &PreprocessRegistry });
                MemoryHashAnalyzer64.Analyzes(Results);

                FXxHash128Analyzer MemoryHashAnalyzer128({ &PreprocessRegistry });
                MemoryHashAnalyzer128.Analyzes(Results);

				for (auto It = Results.CreateConstIterator(); It; ++It)
				{
					UE_LOG(LogAssetSanitiser, Display, TEXT("=== Begin Duplicated Static Mesh Group ==="));
					for (auto Iterator = It->CreateConstIterator(); Iterator; ++Iterator)
					{
						UE_LOG(LogAssetSanitiser, Display, TEXT("SM: %s"), *((*Iterator)->GetStaticMeshObjectPath()));
					}
					UE_LOG(LogAssetSanitiser, Display, TEXT("=== End Duplicated Static Mesh Group ==="));
				}
            }

            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        };

        Async(EAsyncExecution::TaskGraphMainThread, MoveTemp(InnerTask));
    };

    Async(EAsyncExecution::Thread, MoveTemp(OuterTask));
}