#pragma once

#include "MeshSimilarityAnalyzer.h"

class SVisualizeMeshSimilarity :public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVisualizeMeshSimilarity) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<StaticMeshAnalyzer::FStaticMeshAnalyzeResults> InResults);

	static void OpenVisualizeMeshSimilarityDialog(TSharedPtr<StaticMeshAnalyzer::FStaticMeshAnalyzeResults> InResults = MakeShared<StaticMeshAnalyzer::FStaticMeshAnalyzeResults>());

	void CloseDialog();

private:
	void ReCacheListViewSources();

	void OnSearchTextChanged(const FText& InFilterText);

	struct FSimilarGroupEntry
	{
		const StaticMeshAnalyzer::FStaticMeshSimilarGroup* SimilarGroup = nullptr;
	};

	TSharedRef<ITableRow> GenerateSimilarGroupRow(TSharedPtr<FSimilarGroupEntry> InSimilarGroupEntry, const TSharedRef<STableViewBase>& InOwnerTable);
	TSharedRef<SWidget> ConstructMeshThumbnailPreview(const TArray<FSoftObjectPath>& InMeshObjectPaths);

	struct FErrorStatusEntry
	{
		const FString* ObjectPath = nullptr;
		StaticMeshPreprocessor::EStaticMeshStatus Status = StaticMeshPreprocessor::EStaticMeshStatus::Unknown;
	};

	TSharedRef<ITableRow> GenerateErrorStatusRow(TSharedPtr<FErrorStatusEntry> InErrorStatusEntry, const TSharedRef<STableViewBase>& InOwnerTable);

	FReply OnSaveClicked();
	FReply OnLoadClicked();

	void BrowseAssetsInGroup(int32 InGroupIndex);

	TSharedPtr<StaticMeshAnalyzer::FStaticMeshAnalyzeResults> AnalyzeResults;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;

	FString SearchFilter;
	TSharedPtr<SSearchBox> SearchBox;

	TArray<TSharedPtr<FSimilarGroupEntry>> CachedSimilarGroupSource;
	TArray<TSharedPtr<FErrorStatusEntry>> CachedErrorStatusSource;

	TArray<TSharedPtr<FSimilarGroupEntry>> SimilarGroupSource;
	TArray<TSharedPtr<FErrorStatusEntry>> ErrorStatusSource;

	TSharedPtr<SListView<TSharedPtr<FSimilarGroupEntry>>> SimilarGroupsListView;
	TSharedPtr<SListView<TSharedPtr<FErrorStatusEntry>>> ErrorStatusListView;
};
