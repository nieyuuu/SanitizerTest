#include "TestCode.h"

void TestStaticMeshSimilarityAnalyzer()
{
    auto OuterTask = []() {
        FAssetRegistryModule& Registry = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
        while (Registry.Get().IsLoadingAssets()) {}

        auto InnerTask = []() {
            FAssetRegistryModule& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
            AssetRegistry.Get().SearchAllAssets(true);

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
                TUniquePtr<FPreprocessRegistry> PreprocessRegistry = FPreprocessRegistry::PreprocessStaticMeshes(StaticMeshObjectPaths, PreprocessSettings);

                {
                    const FString FileName = FString::Printf(TEXT("MyArchiveTest.bin"));
                    const FString SavePath = FPaths::ProjectSavedDir() + FileName;

                    FPreprocessRegistry::SaveTo(PreprocessRegistry, SavePath);

                    TUniquePtr<FPreprocessRegistry> DeserializedRegistry;
                    FPreprocessRegistry::LoadFrom(DeserializedRegistry, SavePath);

                    FAnalyzeResults Results1;
                    FPerVertexAnalyzer PerVertexAnalyzer;
                    PerVertexAnalyzer.Analyzes({ DeserializedRegistry.Get() }, Results1);

                    FAnalyzeResults Results2;
                    FXxHash64Analyzer MemoryHashAnalyzer64;
                    MemoryHashAnalyzer64.Analyzes({ DeserializedRegistry.Get() }, Results2);

                    FAnalyzeResults Results3;
                    FXxHash128Analyzer MemoryHashAnalyzer128;
                    MemoryHashAnalyzer128.Analyzes({ DeserializedRegistry.Get() }, Results3);

                    FString JsonResultSavePath = FPaths::ProjectSavedDir() + "AnalyzeResults.json";
                    FAnalyzeResults::SaveTo(Results3, JsonResultSavePath);

                    FAnalyzeResults LoadedAnalyzeResults;
                    FAnalyzeResults::LoadFrom(LoadedAnalyzeResults, JsonResultSavePath);

                    auto& SimilarGroups = Results3.SimilarGroups;
                    for (auto It = SimilarGroups.CreateConstIterator(); It; ++It)
                    {
                        UE_LOG(LogAssetSanitizer, Display, TEXT("=== Begin Duplicated Static Mesh Group ==="));
                        auto& Groups = It->SimilarStaticMeshes;
                        for (auto Iterator = Groups.CreateConstIterator(); Iterator; ++Iterator)
                        {
                            UE_LOG(LogAssetSanitizer, Display, TEXT("Static Mesh: %s"), *Iterator->ToString());
                        }
                        UE_LOG(LogAssetSanitizer, Display, TEXT("=== End Duplicated Static Mesh Group ==="));
                    }
                }
            }

            {
                FPreprocessSettings PreprocessSettings(5);
                TUniquePtr<FPreprocessRegistry> PreprocessRegistry = FPreprocessRegistry::PreprocessStaticMeshes(StaticMeshesToProcess, PreprocessSettings);

                FAnalyzeResults Results1;
                FPerVertexAnalyzer PerVertexAnalyzer;
                PerVertexAnalyzer.Analyzes({ PreprocessRegistry.Get() }, Results1);

                FAnalyzeResults Results2;
                FXxHash64Analyzer MemoryHashAnalyzer64;
                MemoryHashAnalyzer64.Analyzes({ PreprocessRegistry.Get() }, Results2);

                FAnalyzeResults Results3;
                FXxHash128Analyzer MemoryHashAnalyzer128;
                MemoryHashAnalyzer128.Analyzes({ PreprocessRegistry.Get() }, Results3);

                auto& SimilarGroups = Results3.SimilarGroups;
                for (auto It = SimilarGroups.CreateConstIterator(); It; ++It)
                {
                    UE_LOG(LogAssetSanitizer, Display, TEXT("=== Begin Duplicated Static Mesh Group ==="));
                    auto& Groups = It->SimilarStaticMeshes;
                    for (auto Iterator = Groups.CreateConstIterator(); Iterator; ++Iterator)
                    {
                        UE_LOG(LogAssetSanitizer, Display, TEXT("Static Mesh: %s"), *Iterator->ToString());
                    }
                    UE_LOG(LogAssetSanitizer, Display, TEXT("=== End Duplicated Static Mesh Group ==="));
                }
            }

            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        };

        Async(EAsyncExecution::TaskGraphMainThread, MoveTemp(InnerTask));
    };

    Async(EAsyncExecution::Thread, MoveTemp(OuterTask));
}
