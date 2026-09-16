#include "SAnalyzeMeshSimilarity.h"

#include "SVisualizeMeshSimilarity.h"

#include "Widgets/SWindow.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Layout/WidgetPath.h"
#include "Styling/AppStyle.h"
#include "SlateOptMacros.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "StaticMeshCompiler.h"

#define LOCTEXT_NAMESPACE "SAnalyzeStaticMeshSimilarity"

struct FCompareFStaticMeshReportNodeByName
{
	inline bool operator()(TSharedPtr<FStaticMeshReportNode> A, TSharedPtr<FStaticMeshReportNode> B) const
	{
		return A->NodeName < B->NodeName;
	}
};

FStaticMeshReportNode::FStaticMeshReportNode() : CheckBoxState(ECheckBoxState::Undetermined), bShouldAnalyze(nullptr), bIsFolder(false), Parent(nullptr)
{}

FStaticMeshReportNode::FStaticMeshReportNode(const FString& InNodeName, bool InIsFolder) : NodeName(InNodeName), CheckBoxState(ECheckBoxState::Undetermined), bShouldAnalyze(nullptr), bIsFolder(InIsFolder), Parent(nullptr)
{}

void FStaticMeshReportNode::AddStaticMesh(const FString& InStaticMeshObjectPath, bool* InShouldAnalyze)
{
	TArray<FString> PathElements;
	InStaticMeshObjectPath.ParseIntoArray(PathElements, TEXT("/"), true);

	AddStaticMeshRecursively(PathElements, InShouldAnalyze);
}

void FStaticMeshReportNode::ExpandChildrenRecursively(const TSharedRef<FStaticMeshReportTree>& InTreeview)const
{
	for (auto ChildIt = Children.CreateConstIterator(); ChildIt; ++ChildIt)
	{
		InTreeview->SetItemExpansion(*ChildIt, (*ChildIt)->CheckBoxState != ECheckBoxState::Unchecked);
		(*ChildIt)->ExpandChildrenRecursively(InTreeview);
	}
}

FStaticMeshReportNode::FChildrenState FStaticMeshReportNode::AddStaticMeshRecursively(TArray<FString>& InPathElements, bool* InShouldAnalyze)
{
	FChildrenState ChildrenState{ false/*bAnyChildChecked*/, true/*bAllChildrenChecked*/ };

	if (InPathElements.Num() > 0)
	{
		FString ChildNodeName = InPathElements[0];
		InPathElements.RemoveAt(0);

		//Try find a child that uses this folder name
		TSharedPtr<FStaticMeshReportNode> Child;
		for (auto ChildIt = Children.CreateConstIterator(); ChildIt; ++ChildIt)
		{
			if ((*ChildIt)->NodeName == ChildNodeName)
			{
				Child = (*ChildIt);
				break;
			}
		}

		//Create a new one(Folder or Asset)
		if (!Child.IsValid())
		{
			const bool bIsAFolder = (InPathElements.Num() > 0);
			int32 ChildIdx = Children.Add(MakeShareable(new FStaticMeshReportNode(ChildNodeName, bIsAFolder)));
			Child = Children[ChildIdx];
			Child.Get()->Parent = this;
			Children.Sort(FCompareFStaticMeshReportNodeByName());
		}

		//Recursively add children
		if (ensure(Child.IsValid()))
		{
			//The Children State of the added Child
			FChildrenState ChildChildrenState = Child->AddStaticMeshRecursively(InPathElements, InShouldAnalyze);

			ChildrenState.bAnyChildChecked |= ChildChildrenState.bAnyChildChecked;
			ChildrenState.bAllChildrenChecked &= ChildChildrenState.bAllChildrenChecked;
		}

		//Determine CheckBoxState of this node based on ChildrenState
		CheckBoxState = ChildrenState.bAllChildrenChecked ? ECheckBoxState::Checked : (ChildrenState.bAnyChildChecked ? ECheckBoxState::Undetermined : ECheckBoxState::Unchecked);
	}
	else
	{
		//The last recursive(the leaf node)
		CheckBoxState = *InShouldAnalyze ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		ChildrenState.bAnyChildChecked = ChildrenState.bAllChildrenChecked = CheckBoxState == ECheckBoxState::Checked;
		bShouldAnalyze = InShouldAnalyze;
	}

	return ChildrenState;
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SAnalyzeMeshSimilarity::Construct(const FArguments& InArgs, TSharedPtr<TArray<FStaticMeshReportData>> InStaticMeshReportDatas)
{
	if (InStaticMeshReportDatas.Get() == nullptr)
	{
		InStaticMeshReportDatas = MakeShared<TArray<FStaticMeshReportData>>();
	}

	StaticMeshesToAnalyze = InStaticMeshReportDatas;

	FolderOpenBrush = FAppStyle::GetBrush("ContentBrowser.AssetTreeFolderOpen");
	FolderClosedBrush = FAppStyle::GetBrush("ContentBrowser.AssetTreeFolderClosed");
	AssetBrush = FAppStyle::GetBrush("ContentBrowser.ColumnViewAssetIcon");

	ConstructNodeTree(InStaticMeshReportDatas);

	AnalyzerTypeOptions.Add(MakeShared<StaticMeshAnalyzer::EStaticMeshAnalyzerType>(StaticMeshAnalyzer::EStaticMeshAnalyzerType::PerVertex));
	AnalyzerTypeOptions.Add(MakeShared<StaticMeshAnalyzer::EStaticMeshAnalyzerType>(StaticMeshAnalyzer::EStaticMeshAnalyzerType::XxHash64));
	AnalyzerTypeOptions.Add(MakeShared<StaticMeshAnalyzer::EStaticMeshAnalyzerType>(StaticMeshAnalyzer::EStaticMeshAnalyzerType::XxHash128));

	SelectedAnalyzerTypeOption = AnalyzerTypeOptions[0];

	ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Docking.Tab.ContentAreaBrush"))
				.Padding(FMargin(4, 8, 4, 4))
				[
					//Quantization exponent
					SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 4)
						[
							SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.f)
								.VAlign(VAlign_Center)
								[
									SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.VAlign(VAlign_Center)
										[
											SNew(STextBlock).Text(FText::FromString(TEXT("QuantizationExponent: ")))
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.VAlign(VAlign_Center)
										[
											SNew(SSpinBox<int32>)
												.MinValue(MIN_EXPONENT)
												.MaxValue(MAX_EXPONENT)
												.MinSliderValue(MIN_EXPONENT)
												.MaxSliderValue(MAX_EXPONENT)
												.Delta(1)
												.MinDesiredWidth(27)
												.Value_Lambda([this]() {
												return QuantizationExponent;
													})
												.OnValueChanged_Lambda([this](int32 InNewValue) {
												QuantizationExponent = InNewValue;
													})
										]
								]
							//StaticMeshAnalyzer type
							+ SHorizontalBox::Slot()
								.FillWidth(1.f)
								.HAlign(HAlign_Right)
								.VAlign(VAlign_Center)
								[
									SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.VAlign(VAlign_Center)
										[
											SNew(STextBlock).Text(FText::FromString(TEXT("AnalyzerType: ")))
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.VAlign(VAlign_Center)
										[
											SNew(SComboBox<TSharedPtr<StaticMeshAnalyzer::EStaticMeshAnalyzerType>>)
												.OptionsSource(&AnalyzerTypeOptions)
												.OnSelectionChanged_Lambda([this](TSharedPtr<StaticMeshAnalyzer::EStaticMeshAnalyzerType> InNewSelection, ESelectInfo::Type) {
												SelectedAnalyzerTypeOption = InNewSelection;
													})
												.OnGenerateWidget_Lambda([](TSharedPtr<StaticMeshAnalyzer::EStaticMeshAnalyzerType> InOption) {
												return SNew(STextBlock).Text(UEnum::GetDisplayValueAsText(*InOption));
													})
												.InitiallySelectedItem(SelectedAnalyzerTypeOption)
												[
													SNew(STextBlock).Text_Lambda([this]() {
														return UEnum::GetDisplayValueAsText(*SelectedAnalyzerTypeOption);
														})
												]
										]
								]
						]
					//Tool tip message
					+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 4)
						[
							SNew(STextBlock)
								.Text(LOCTEXT("AnalyzeReportTitle", "The following static meshes will be analyzed with the given parameters."))
								.TextStyle(FAppStyle::Get(), "PackageMigration.DialogTitle")
						]
						//Tree view
						+ SVerticalBox::Slot()
						.FillHeight(1.f)
						[
							SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
								[
									SAssignNew(TreeView, FStaticMeshReportTree)
										.TreeItemsSource(&RootNode.Children)
										.SelectionMode(ESelectionMode::Single)
										.OnGenerateRow(this, &SAnalyzeMeshSimilarity::GenerateTreeRow)
										.OnGetChildren(this, &SAnalyzeMeshSimilarity::GetChildrenForNode)
								]
						]
					//OK/Cancel button
					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						.Padding(0, 4, 0, 0)
						[
							SNew(SUniformGridPanel)
								.SlotPadding(FAppStyle::GetMargin("StandardDialog.SlotPadding"))
								.MinDesiredSlotWidth(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
								.MinDesiredSlotHeight(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
								+ SUniformGridPanel::Slot(0, 0)
								[
									SNew(SButton)
										.HAlign(HAlign_Center)
										.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
										.OnClicked(this, &SAnalyzeMeshSimilarity::OnOkClicked)
										.Text(LOCTEXT("OkButton", "OK"))
								]
							+ SUniformGridPanel::Slot(1, 0)
								[
									SNew(SButton)
										.HAlign(HAlign_Center)
										.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
										.OnClicked(this, &SAnalyzeMeshSimilarity::OnCancelClicked)
										.Text(LOCTEXT("CancelButton", "Cancel"))
								]
						]
				]
		];

	//Expand after construct
	if (ensure(TreeView.IsValid()))
	{
		RootNode.ExpandChildrenRecursively(TreeView.ToSharedRef());
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SAnalyzeMeshSimilarity::OpenAnalyzeMeshSimilarityDialog(TSharedPtr<TArray<FStaticMeshReportData>> InStaticMeshReportDatas)
{
	TSharedRef<SWindow> AnalyzeMeshSimilarityWindow = SNew(SWindow)
		.Title(LOCTEXT("AnalyzeMeshSimilarityTitle", "Analyze Mesh Similarity"))
		.ClientSize(FVector2D(800, 600))
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		[
			SNew(SAnalyzeMeshSimilarity, InStaticMeshReportDatas)
		];

	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	if (MainFrameModule.GetParentWindow().IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(AnalyzeMeshSimilarityWindow, MainFrameModule.GetParentWindow().ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(AnalyzeMeshSimilarityWindow);
	}
}

void SAnalyzeMeshSimilarity::CloseDialog()
{
	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());

	if (Window.IsValid())
	{
		Window->RequestDestroyWindow();
	}
}

void SAnalyzeMeshSimilarity::ConstructNodeTree(TSharedPtr<TArray<FStaticMeshReportData>> InStaticMeshReportDatas)
{
	for (FStaticMeshReportData& ReportData : *InStaticMeshReportDatas.Get())
	{
		RootNode.AddStaticMesh(ReportData.StaticMeshObjectPath, &ReportData.bShouldAnalyze);
	}
}

TSharedRef<ITableRow> SAnalyzeMeshSimilarity::GenerateTreeRow(TSharedPtr<FStaticMeshReportNode> InTreeItem, const TSharedRef<STableViewBase>& InOwnerTable)
{
	check(InTreeItem.IsValid());

	const FSlateBrush* IconBrush = GetNodeIcon(InTreeItem);

	return SNew(STableRow<TSharedPtr<FStaticMeshReportNode>>, InOwnerTable)
		[
			//Icon
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
						.OnCheckStateChanged(this, &SAnalyzeMeshSimilarity::OnCheckBoxStateChanged, InTreeItem, InOwnerTable)
						.IsChecked(this, &SAnalyzeMeshSimilarity::GetCheckBoxStateForNode, InTreeItem)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SImage).Image(IconBrush)
				]
				//Node Name
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(STextBlock).Text(FText::FromString(InTreeItem->NodeName))
						.ColorAndOpacity(FSlateColor::UseForeground())
				]
		];
}

void SAnalyzeMeshSimilarity::SetStateRecursively(TSharedPtr<FStaticMeshReportNode> InTreeItem, bool InIsChecked)
{
	if (InTreeItem.Get() == nullptr)
	{
		return;
	}

	InTreeItem.Get()->CheckBoxState = InIsChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;

	if (InTreeItem.Get()->bShouldAnalyze)
	{
		*(InTreeItem.Get()->bShouldAnalyze) = InIsChecked;
	}

	TArray<TSharedPtr<FStaticMeshReportNode>> Children;
	GetChildrenForNode(InTreeItem, Children);
	for (int i = 0; i < Children.Num(); i++)
	{
		if (Children[i].Get() == nullptr)
		{
			continue;
		}

		SetStateRecursively(Children[i], InIsChecked);
	}
}

void SAnalyzeMeshSimilarity::OnCheckBoxStateChanged(ECheckBoxState InNewCheckBoxState, TSharedPtr<FStaticMeshReportNode> InTreeItem, TSharedRef<STableViewBase> InOwnerTable)
{
	SetStateRecursively(InTreeItem, InNewCheckBoxState == ECheckBoxState::Checked);

	FStaticMeshReportNode* CurrentParent = InTreeItem->Parent;
	while (CurrentParent != nullptr)
	{
		bool bAnyChildChecked = false;
		bool bAllChildrenChecked = true;
		for (int i = 0; i < CurrentParent->Children.Num(); i++)
		{
			bAnyChildChecked |= CurrentParent->Children[i]->CheckBoxState != ECheckBoxState::Unchecked;
			bAllChildrenChecked &= CurrentParent->Children[i]->CheckBoxState != ECheckBoxState::Unchecked;
		}

		CurrentParent->CheckBoxState = bAllChildrenChecked ? ECheckBoxState::Checked : (bAnyChildChecked ? ECheckBoxState::Undetermined : ECheckBoxState::Unchecked);
		CurrentParent = CurrentParent->Parent;
	}

	InOwnerTable.Get().RebuildList();
}

void SAnalyzeMeshSimilarity::GetChildrenForNode(TSharedPtr<FStaticMeshReportNode> InTreeItem, TArray<TSharedPtr<FStaticMeshReportNode>>& OutChildren)
{
	OutChildren = InTreeItem->Children;
}

ECheckBoxState SAnalyzeMeshSimilarity::GetCheckBoxStateForNode(TSharedPtr<FStaticMeshReportNode> InTreeItem) const
{
	return InTreeItem.Get()->CheckBoxState;
}

const FSlateBrush* SAnalyzeMeshSimilarity::GetNodeIcon(const TSharedPtr<FStaticMeshReportNode>& InStaticMeshReportNode) const
{
	if (!InStaticMeshReportNode->bIsFolder)
	{
		return AssetBrush;
	}
	else if (TreeView->IsItemExpanded(InStaticMeshReportNode))
	{
		return FolderOpenBrush;
	}
	else
	{
		return FolderClosedBrush;
	}
}

FReply SAnalyzeMeshSimilarity::OnOkClicked()
{
	TSet<FString> UniqueStaticMeshObjectPaths;
	for (const FStaticMeshReportData& Data : *StaticMeshesToAnalyze.Get())
	{
		if (Data.bShouldAnalyze)
		{
			UniqueStaticMeshObjectPaths.Add(Data.StaticMeshObjectPath);
		}
	}

	FScopedSlowTask SlowTask(5, LOCTEXT("AnalyzeMeshSimilarity_LoadingStaticMeshes", "Loading Static Meshes..."));
	SlowTask.MakeDialog(false, false);

	//Load static meshes
	SlowTask.EnterProgressFrame(1, LOCTEXT("AnalyzeMeshSimilarity_LoadingStaticMeshes", "Loading Static Meshes..."));

	TSet<const UStaticMesh*> LoadedStaticMeshes;
	TArray<UStaticMesh*> PendingCompilingStaticMeshes;
	TArray<FString> FailedToLoadObjectPaths;

	LoadedStaticMeshes.Reserve(UniqueStaticMeshObjectPaths.Num());
	for (const FString& ObjectPaths : UniqueStaticMeshObjectPaths)
	{
		UStaticMesh* StaticMesh = FindObject<UStaticMesh>(nullptr, *ObjectPaths);

		if (StaticMesh == nullptr)
		{
			StaticMesh = LoadObject<UStaticMesh>(nullptr, *ObjectPaths);
		}
		if (StaticMesh == nullptr)
		{
			FSoftObjectPath SoftObjectPath(ObjectPaths);
			StaticMesh = Cast<UStaticMesh>(SoftObjectPath.TryLoad());
		}
		if (StaticMesh == nullptr)
		{
			FailedToLoadObjectPaths.Add(ObjectPaths);
		}
		else
		{
			check(StaticMesh->IsValidLowLevel());
			LoadedStaticMeshes.Add(StaticMesh);

			if (StaticMesh->IsCompiling())
			{
				PendingCompilingStaticMeshes.Add(StaticMesh);
			}
		}
	}

	//Wait for static meshes finishing compiling
	SlowTask.EnterProgressFrame(1, LOCTEXT("AnalyzeMeshSimilarity_WaitingForCompiling", "Waiting for Static Meshes Finishing Compiling..."));
	FStaticMeshCompilingManager::Get().FinishCompilation(PendingCompilingStaticMeshes);

	//Preprocess
	SlowTask.EnterProgressFrame(1, LOCTEXT("AnalyzeMeshSimilarity_PreprocessingStaticMeshes", "Preprocessing Static Meshes..."));
	TUniquePtr<StaticMeshPreprocessor::FRegistry> Registry = StaticMeshPreprocessor::FRegistry::PreprocessStaticMeshes(LoadedStaticMeshes, StaticMeshPreprocessor::FSettings(QuantizationExponent));

	//Perform a garbage collection
	SlowTask.EnterProgressFrame(1, LOCTEXT("AnalyzeMeshSimilarity_CollectGarbage", "Collecting Garbage..."));
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	//Analyze
	SlowTask.EnterProgressFrame(1, LOCTEXT("AnalyzeMeshSimilarity_AnalyzeMeshSimilarity", "Analyzing Static Mesh Similarity..."));
	TSharedPtr<StaticMeshAnalyzer::FStaticMeshAnalyzeResults> Results = MakeShared<StaticMeshAnalyzer::FStaticMeshAnalyzeResults>();
	if (!StaticMeshAnalyzer::AnalyzeMeshSimilarity(*SelectedAnalyzerTypeOption, { Registry.Get() }, *Results))
	{
		FNotificationInfo Info(LOCTEXT("FailedToAnalyze", "Failed to analyze mesh similarity."));
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	else
	{
		for (const FString& ObjectPath : FailedToLoadObjectPaths)
		{
			Results.Get()->ObjectPathToErrorStatus.Add(ObjectPath, StaticMeshPreprocessor::EStaticMeshStatus::InvalidObjectPath);
		}
		SVisualizeMeshSimilarity::OpenVisualizeMeshSimilarityDialog(Results);
	}

	return FReply::Handled();
}

FReply SAnalyzeMeshSimilarity::OnCancelClicked()
{
	CloseDialog();

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE