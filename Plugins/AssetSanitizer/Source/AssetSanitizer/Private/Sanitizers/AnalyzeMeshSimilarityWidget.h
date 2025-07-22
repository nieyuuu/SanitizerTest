#pragma once

#include "MeshSimilarityAnalyzer.h"

struct FStaticMeshReportNode;

//Tree view type alias
typedef STreeView<TSharedPtr<struct FStaticMeshReportNode>> FStaticMeshReportTree;

//User side input
struct FStaticMeshReportData
{
	FString StaticMeshObjectPath;
	bool bShouldAnalyze;
};

//Tree view node
struct FStaticMeshReportNode
{
public:
	//Node name(Can be Folder_Name or SM_Name.SM_Name)
	FString NodeName;
	//Check box state of this node
	ECheckBoxState CheckBoxState;
	//A pointer to external FStaticMeshReportData::bShouldAnalyze and is only non-null in leaf nodes
	bool* bShouldAnalyze;
	//Whether this node is a folder or a static mesh
	bool  bIsFolder;

	//Parent node of this node
	FStaticMeshReportNode* Parent;
	//Children nodes of this node
	TArray<TSharedPtr<FStaticMeshReportNode>> Children;

	FStaticMeshReportNode();
	FStaticMeshReportNode(const FString& InNodeName, bool InIsFolder);

	void AddStaticMesh(const FString& InStaticMeshObjectPath, bool* InShouldAnalyze);

	void ExpandChildrenRecursively(const TSharedRef<FStaticMeshReportTree>& InTreeview)const;

private:
	struct FChildrenState
	{
		bool bAnyChildChecked;
		bool bAllChildrenChecked;
	};

	FChildrenState AddStaticMeshRecursively(TArray<FString>& InPathElements, bool* InShouldAnalyze);
};

class SAnalyzeMeshSimilarity :public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAnalyzeMeshSimilarity) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<TArray<FStaticMeshReportData>> InStaticMeshReportDatas);

	static void OpenAnalyzeMeshSimilarityDialog(TSharedPtr<TArray<FStaticMeshReportData>> InStaticMeshReportDatas);

	void CloseDialog();

private:
	void ConstructNodeTree(TSharedPtr<TArray<FStaticMeshReportData>> InStaticMeshReportDatas);

	TSharedRef<ITableRow> GenerateTreeRow(TSharedPtr<FStaticMeshReportNode> InTreeItem, const TSharedRef<STableViewBase>& InOwnerTable);

	void SetStateRecursively(TSharedPtr<FStaticMeshReportNode> InTreeItem, bool InIsChecked);

	void OnCheckBoxStateChanged(ECheckBoxState InNewCheckBoxState, TSharedPtr<FStaticMeshReportNode> InTreeItem, TSharedRef<STableViewBase> InOwnerTable);

	void GetChildrenForNode(TSharedPtr<FStaticMeshReportNode> InTreeItem, TArray<TSharedPtr<FStaticMeshReportNode>>& OutChildren);

	ECheckBoxState GetEnaCheckBoxStateForNode(TSharedPtr<FStaticMeshReportNode> InTreeItem) const;
	const FSlateBrush* GetNodeIcon(const TSharedPtr<FStaticMeshReportNode>& InStaticMeshReportNode) const;

	FReply OnOkClicked();
	FReply OnCancelClicked();

private:
	TSharedPtr<TArray<FStaticMeshReportData>> StaticMeshesToAnalyze;

	FStaticMeshReportNode RootNode;
	TSharedPtr<FStaticMeshReportTree> TreeView;

	const FSlateBrush* FolderOpenBrush;
	const FSlateBrush* FolderClosedBrush;
	const FSlateBrush* AssetBrush;

	int32 QuantizationExponent = DEFAULT_EXPONENT;

	TArray<TSharedPtr<EAnalyzerType>> AnalyzerTypeOptions;
	TSharedPtr<EAnalyzerType> SelectedAnalyzerTypeOption;
};