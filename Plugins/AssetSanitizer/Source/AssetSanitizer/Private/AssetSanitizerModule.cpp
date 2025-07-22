#include "AssetSanitizerModule.h"

#include "Sanitizers/VisualizeMeshSimilarityWidget.h"
#include "Sanitizers/AnalyzeMeshSimilarityWidget.h"

#include "AssetViewUtils.h"
#include "ContentBrowserMenuContexts.h"

#define LOCTEXT_NAMESPACE "FAssetSanitizerModule"

void FAssetSanitizerModule::StartupModule()
{
    if (IsRunningCommandlet())
        return;

    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAssetSanitizerModule::RegisterContentBrowserMenu));
}

void FAssetSanitizerModule::ShutdownModule()
{
    if (IsRunningCommandlet())
        return;

    UToolMenus::UnRegisterStartupCallback(this);
}

void FAssetSanitizerModule::RegisterContentBrowserMenu()
{
    UToolMenu* StaticMeshActionsMenu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.StaticMesh"));
    if (StaticMeshActionsMenu)
    {
        FToolMenuSection& StaticMeshActionsSection = StaticMeshActionsMenu->FindOrAddSection(TEXT("GetAssetActions"));
        StaticMeshActionsSection.AddSubMenu(
            TEXT("MeshSimilarityAnalyze"),
            LOCTEXT("MeshSimilarityAnalyzeMenu", "Similarity Analyze"),
            LOCTEXT("MeshSimilarityAnalyzeMenuTooltip", "Analyze static mesh similarity or visualize analyze results."),
            FNewToolMenuDelegate::CreateRaw(this, &FAssetSanitizerModule::OnGenerateSubMenuForAssetContext),
            false,
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Search")));
    }

    UToolMenu* FolderOptionsMenu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.FolderContextMenu"));
    if (FolderOptionsMenu)
    {
        FToolMenuSection& FolderOptionsSection = FolderOptionsMenu->FindOrAddSection(TEXT("PathViewFolderOptions"));
        FolderOptionsSection.AddSubMenu(
            TEXT("MeshSimilarityAnalyze"),
            LOCTEXT("MeshSimilarityAnalyzeMenu", "Static Mesh Similarity Analyze"),
            LOCTEXT("MeshSimilarityAnalyzeMenuTooltip", "Analyze static mesh similarity or visualize analyze results."),
            FNewToolMenuDelegate::CreateRaw(this, &FAssetSanitizerModule::OnGenerateSubMenuForFolderContext),
            false,
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Search")));
    }
}

void FAssetSanitizerModule::OnGenerateSubMenuForAssetContext(UToolMenu* InMenu)
{
    FToolMenuSection& SimilarityAnalyzeSection = InMenu->AddSection(TEXT("SimilarityAnalyzeSection"), LOCTEXT("SimilarityAnalyzeSection", "Similarity Analyze"));
    SimilarityAnalyzeSection.AddMenuEntry(
        TEXT("VisualizeAnalyzeResults"),
        LOCTEXT("VisualizeAnalyzeResultsMenu", "Visualize Analyze Results"),
        LOCTEXT("VisualizeAnalyzeResultsMenuTooltip", "Show the similarity analyze results loaded from disk."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateRaw(this, &FAssetSanitizerModule::OnVisualizeAnalyzeResults)));

    const UContentBrowserAssetContextMenuContext* AssetContext = InMenu->FindContext<UContentBrowserAssetContextMenuContext>();
    if (AssetContext && AssetContext->SelectedAssets.Num() >= 1)
    {
        SimilarityAnalyzeSection.AddMenuEntry(
            TEXT("AnalyzeStaticMeshSimilarity"),
            LOCTEXT("AnalyzeStaticMeshSimilarityMenu", "Analyze Static Mesh Similarity"),
            LOCTEXT("AnalyzeStaticMeshSimilarityMenuTooltip", "Analyze similarity of the selected static meshes."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([AssetContext, this]() {
                OnAnalyzeStaticMeshSimilarity(AssetContext, nullptr);
            })));
    }
}

void FAssetSanitizerModule::OnGenerateSubMenuForFolderContext(UToolMenu* InMenu)
{
    FToolMenuSection& SimilarityAnalyzeSection = InMenu->AddSection(TEXT("SimilarityAnalyzeSection"), LOCTEXT("SimilarityAnalyzeSection", "Similarity Analyze"));
    SimilarityAnalyzeSection.AddMenuEntry(
        TEXT("VisualizeAnalyzeResults"),
        LOCTEXT("VisualizeAnalyzeResultsMenu", "Visualize Analyze Results"),
        LOCTEXT("VisualizeAnalyzeResultsMenuTooltip", "Show the similarity analyze results loaded from disk."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateRaw(this, &FAssetSanitizerModule::OnVisualizeAnalyzeResults)));

    const UContentBrowserFolderContext* FolderContext = InMenu->FindContext<UContentBrowserFolderContext>();
    if (FolderContext && FolderContext->GetSelectedPackagePaths().Num() >= 1)
    {
        SimilarityAnalyzeSection.AddMenuEntry(
            TEXT("AnalyzeStaticMeshSimilarity"),
            LOCTEXT("AnalyzeStaticMeshSimilarityMenu", "Analyze Static Mesh Similarity"),
            LOCTEXT("AnalyzeStaticMeshSimilarityMenuTooltip", "Analyze similarity of static meshes under the selected folders."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([FolderContext, this]() {
                OnAnalyzeStaticMeshSimilarity(nullptr, FolderContext);
            })));
    }
}

void FAssetSanitizerModule::OnAnalyzeStaticMeshSimilarity(const UContentBrowserAssetContextMenuContext* InAssetContextMenuContext, const UContentBrowserFolderContext* InFolderContext)
{
    TArray<FAssetData> StaticMeshAssetDatas;
    if (InFolderContext != nullptr)
    {
        AssetViewUtils::GetAssetsInPaths(InFolderContext->SelectedPackagePaths, StaticMeshAssetDatas);
    }
    else if (InAssetContextMenuContext != nullptr)
    {
        StaticMeshAssetDatas.Append(InAssetContextMenuContext->SelectedAssets);
    }

    TSharedPtr<TArray<FStaticMeshReportData>> StaticMeshesToAnalyze = MakeShared<TArray<FStaticMeshReportData>>();
    StaticMeshesToAnalyze->Reserve(StaticMeshAssetDatas.Num());
    for (const FAssetData& AssetData : StaticMeshAssetDatas)
    {
        UClass* AssetClass = AssetData.GetClass(EResolveClass::Yes);
        if (AssetClass != nullptr)
        {
            if (AssetClass == UStaticMesh::StaticClass())
            {
                StaticMeshesToAnalyze->Add(FStaticMeshReportData(AssetData.GetObjectPathString(), true));
            }
        }
    }

    if (StaticMeshesToAnalyze->Num() < 2)
    {
        return;
    }

    SAnalyzeMeshSimilarity::OpenAnalyzeMeshSimilarityDialog(StaticMeshesToAnalyze);
}

void FAssetSanitizerModule::OnVisualizeAnalyzeResults()
{
    SVisualizeMeshSimilarity::OpenVisualizeMeshSimilarityDialog();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssetSanitizerModule, AssetSanitizer)