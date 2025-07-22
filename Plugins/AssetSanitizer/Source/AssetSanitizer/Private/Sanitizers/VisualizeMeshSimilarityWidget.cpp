#include "VisualizeMeshSimilarityWidget.h"

#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "SlateOptMacros.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DesktopPlatformModule.h"
#include "AssetThumbnail.h"
#include "Interfaces/IMainFrameModule.h"

#define LOCTEXT_NAMESPACE "SVisualizeMeshSimilarity"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SVisualizeMeshSimilarity::Construct(const FArguments& InArgs, TSharedPtr<FAnalyzeResults> InResults)
{
	if (InResults.Get() == nullptr)
	{
		InResults = MakeShared<FAnalyzeResults>();
	}

	AnalyzeResults = InResults;
	ThumbnailPool = MakeShareable(new FAssetThumbnailPool(256));

	ReCacheListViewSources();
	SimilarGroupSource = CachedSimilarGroupSource;
	ErrorStatusSource = CachedErrorStatusSource;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 6, 8, 4)
		[
			SNew(SHorizontalBox)
			//Search box
			+ SHorizontalBox::Slot()
			.FillWidth(0.6f)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchBoxHint", "Search by mesh path..."))
				.OnTextChanged(this, &SVisualizeMeshSimilarity::OnSearchTextChanged)
				.MinDesiredWidth(170)
			]
			//Result counts
			+ SHorizontalBox::Slot()
			.FillWidth(0.4f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
				return FText::Format(LOCTEXT("ResultCounts", "Groups: {0}/{1}  Errors: {2}/{3}"),
					FText::AsNumber(SimilarGroupSource.Num()),
					FText::AsNumber(CachedSimilarGroupSource.Num()),
					FText::AsNumber(ErrorStatusSource.Num()),
					FText::AsNumber(CachedErrorStatusSource.Num()));
				})
				.Justification(ETextJustify::Right)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 0, 8, 4)
		[
			SNew(SHorizontalBox)
			//Quantization exponent
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("QuantizationExponentLabel", "Quantization Exponent:"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
			//Quantization exponent
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return FText::AsNumber(AnalyzeResults->QuantizationExponent); })
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
			//Save button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(12, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("SaveButton", "Save"))
				.OnClicked(this, &SVisualizeMeshSimilarity::OnSaveClicked)
				.ToolTipText(LOCTEXT("SaveTooltip", "Save analyze results to file"))
				.ContentPadding(FMargin(6, 2))
			]
			//Load button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("LoadButton", "Load"))
				.OnClicked(this, &SVisualizeMeshSimilarity::OnLoadClicked)
				.ToolTipText(LOCTEXT("LoadTooltip", "Load analyze results from file"))
				.ContentPadding(FMargin(6, 2))
			]
		]
		//Similar groups
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 8, 8, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SimilarGroupsHeader", "Similar Groups"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
		]
		//Similar groups
		+ SVerticalBox::Slot()
		.FillHeight(0.6f)
		.Padding(8, 0, 8, 2)
		[
			SNew(SBox)
			.MinDesiredHeight(300)
			[
				SAssignNew(SimilarGroupsListView, SListView<TSharedPtr<FSimilarGroupEntry>>)
				.ListItemsSource(&SimilarGroupSource)
				.ConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible)
				.OnGenerateRow(this, &SVisualizeMeshSimilarity::GenerateSimilarGroupRow)
			]
		]
		//Error status map
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 12, 8, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ErrorStatusHeader", "Error Status Map"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
		]
		//Error status map
		+ SVerticalBox::Slot()
		.FillHeight(0.15f)
		.Padding(8, 0, 8, 8)
		[
			SNew(SBox)
			.MinDesiredHeight(60)
			[
				SAssignNew(ErrorStatusListView, SListView<TSharedPtr<FErrorStatusEntry>>)
				.ListItemsSource(&ErrorStatusSource)
				.ConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible)
				.OnGenerateRow(this, &SVisualizeMeshSimilarity::GenerateErrorStatusRow)
			]
		]
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SVisualizeMeshSimilarity::OpenVisualizeMeshSimilarityDialog(TSharedPtr<FAnalyzeResults> InResults)
{
	if (InResults.Get() == nullptr)
	{
		InResults = MakeShared<FAnalyzeResults>();
	}

	TSharedRef<SWindow> VisualizeMeshSimilarityWindow = SNew(SWindow)
		.Title(LOCTEXT("VisualizeMeshSimilarityTitle", "Mesh Similarity Analyze Results"))
		.ClientSize(FVector2D(1200, 800))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(SVisualizeMeshSimilarity, InResults)
		];

	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	if (MainFrameModule.GetParentWindow().IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(VisualizeMeshSimilarityWindow, MainFrameModule.GetParentWindow().ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(VisualizeMeshSimilarityWindow);
	}
}

void SVisualizeMeshSimilarity::CloseDialog()
{
	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());

	if (Window.IsValid())
	{
		Window->RequestDestroyWindow();
	}
}

void SVisualizeMeshSimilarity::ReCacheListViewSources()
{
	CachedSimilarGroupSource.Empty(AnalyzeResults->SimilarGroups.Num());
	CachedErrorStatusSource.Empty(AnalyzeResults->ObjectPathToErrorStatus.Num());

	for (const FSimilarGroup& SimilarGroup : AnalyzeResults->SimilarGroups)
	{
		TSharedPtr<FSimilarGroupEntry> Entry = MakeShared<FSimilarGroupEntry>();
		Entry->SimilarGroup = &SimilarGroup;
		CachedSimilarGroupSource.Add(Entry);
	}
	for (const TPair<FString, EPreprocessStatus>& Pair : AnalyzeResults->ObjectPathToErrorStatus)
	{
		TSharedPtr<FErrorStatusEntry> Entry = MakeShared<FErrorStatusEntry>();
		Entry->ObjectPath = &Pair.Key;
		Entry->Status = Pair.Value;
		CachedErrorStatusSource.Add(Entry);
	}
}

void SVisualizeMeshSimilarity::OnSearchTextChanged(const FText& InFilterText)
{
	SearchFilter = InFilterText.ToString();
	SimilarGroupSource.Empty(CachedSimilarGroupSource.Num());
	ErrorStatusSource.Empty(CachedErrorStatusSource.Num());

	if (SearchFilter.IsEmpty())
	{
		SimilarGroupSource = CachedSimilarGroupSource;
		ErrorStatusSource = CachedErrorStatusSource;
	}
	else
	{
		for (TSharedPtr<FSimilarGroupEntry>& Group : CachedSimilarGroupSource)
		{
			for (const FSoftObjectPath& MeshPath : Group->SimilarGroup->SimilarStaticMeshes)
			{
				if (MeshPath.ToString().Contains(SearchFilter))
				{
					SimilarGroupSource.Add(Group);
					break;
				}
			}
		}

		for (TSharedPtr<FErrorStatusEntry>& ErrorStatus : CachedErrorStatusSource)
		{
			if (ErrorStatus->ObjectPath->Contains(SearchFilter))
			{
				ErrorStatusSource.Add(ErrorStatus);
			}
		}
	}

	SimilarGroupsListView->RequestListRefresh();
	ErrorStatusListView->RequestListRefresh();
}

TSharedRef<ITableRow> SVisualizeMeshSimilarity::GenerateSimilarGroupRow(TSharedPtr<FSimilarGroupEntry> InSimilarGroupEntry, const TSharedRef<STableViewBase>& InOwnerTable)
{
	const int32 GroupIndex = CachedSimilarGroupSource.IndexOfByKey(InSimilarGroupEntry);

	return SNew(STableRow<TSharedPtr<FSimilarGroup>>, InOwnerTable)
		.Padding(3)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(5)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(0.85f)
				[
					//Group statistics
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("SimilarGroupHeader", "Group {0}: {1} vertices"), FText::AsNumber(GroupIndex), FText::AsNumber(InSimilarGroupEntry->SimilarGroup->NumOfVertices)))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					]
					//Group thumbnail
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(6, 2, 0, 0)
					[
						ConstructMeshThumbnailPreview(InSimilarGroupEntry->SimilarGroup->SimilarStaticMeshes)
					]
				]
				//Browse group assets
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				.Padding(4, 2, 0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("BrowseAssetsButton", "Browse"))
					.ToolTipText(LOCTEXT("BrowseTooltip", "Browse all assets in this group"))
					.OnClicked_Lambda([this, GroupIndex]() {
						BrowseAssetsInGroup(GroupIndex);
						return FReply::Handled();
					})
					.ContentPadding(FMargin(4, 1))
				]
			]
		];
}

TSharedRef<SWidget> SVisualizeMeshSimilarity::ConstructMeshThumbnailPreview(const TArray<FSoftObjectPath>& InMeshObjectPaths)
{
	TSharedPtr<SWrapBox> ThumbnailContainer = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5, 5));
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	for (const FSoftObjectPath& ObjectPath : InMeshObjectPaths)
	{
		FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(ObjectPath);
		TSharedPtr<FAssetThumbnail> AssetThumbnail = MakeShareable(new FAssetThumbnail(AssetData, 128, 128, ThumbnailPool));

		ThumbnailContainer->AddSlot()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			.Padding(0.0f)
			[
				//Asset thumbnail
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					AssetThumbnail->MakeThumbnailWidget()
				]
				//Asset name
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2)
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.WidthOverride(64)
					[
						SNew(SHyperlink)
						.Text(FText::FromString(FPaths::GetBaseFilename(ObjectPath.ToString())))
						.OnNavigate_Lambda([ObjectPath]() {
							FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
							FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(ObjectPath);
							if (AssetData.IsValid())
							{
								FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().SyncBrowserToAssets({ AssetData });
							}
							else
							{
								FNotificationInfo Info(LOCTEXT("NoAssetFound", "No valid assets found for this static mesh"));
								Info.ExpireDuration = 5.0f;
								FSlateNotificationManager::Get().AddNotification(Info);
							}
						})
						.ToolTipText(FText::FromString(ObjectPath.ToString()))
					]
				]
			]
		];
	}

	return ThumbnailContainer.ToSharedRef();
}

TSharedRef<ITableRow> SVisualizeMeshSimilarity::GenerateErrorStatusRow(TSharedPtr<FErrorStatusEntry> InErrorStatusEntry, const TSharedRef<STableViewBase>& InOwnerTable)
{
	return SNew(STableRow<TSharedPtr<FErrorStatusEntry>>, InOwnerTable)
		.Padding(2)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(2)
			[
				//Object path
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(2, 0)
				[
					SNew(SEditableText)
					.Text(FText::FromString(*InErrorStatusEntry->ObjectPath))
					.IsReadOnly(true)
				]
				//Error status
				+ SHorizontalBox::Slot()
				.Padding(2, 0)
				.AutoWidth()
				[
					SNew(SEditableText)
					.Text(GetStatusText(InErrorStatusEntry->Status))
					.IsReadOnly(true)
				]
			]
		];
}

FReply SVisualizeMeshSimilarity::OnSaveClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> SaveFilenames;
		bool bSaved = DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			LOCTEXT("SaveDialogTitle", "Save Analyze Results").ToString(),
			FPaths::ProjectSavedDir(),
			TEXT(""),
			TEXT("JSON files (*.json)|*.json"),
			EFileDialogFlags::None,
			SaveFilenames);

		if (bSaved && SaveFilenames.Num() > 0)
		{
			if (FAnalyzeResults::SaveTo(*AnalyzeResults, SaveFilenames[0]))
			{
				FNotificationInfo Info(FText::Format(LOCTEXT("SaveSuccess", "Results saved to: {0}"), FText::FromString(SaveFilenames[0])));
				Info.ExpireDuration = 5.0f;
				FSlateNotificationManager::Get().AddNotification(Info);
			}
			else
			{
				FNotificationInfo Info(LOCTEXT("SaveFailed", "Failed to save results!"));
				Info.ExpireDuration = 5.0f;
				FSlateNotificationManager::Get().AddNotification(Info);
			}
		}
	}
	return FReply::Handled();
}

FReply SVisualizeMeshSimilarity::OnLoadClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OpenFilenames;
		bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			LOCTEXT("LoadDialogTitle", "Load Analyze Results").ToString(),
			FPaths::ProjectSavedDir(),
			TEXT(""),
			TEXT("JSON files (*.json)|*.json"),
			EFileDialogFlags::None,
			OpenFilenames);

		if (bOpened && OpenFilenames.Num() > 0)
		{
			TSharedPtr<FAnalyzeResults> NewResults = MakeShared<FAnalyzeResults>();
			if (FAnalyzeResults::LoadFrom(*NewResults.Get(), OpenFilenames[0]))
			{
				AnalyzeResults = NewResults;

				SearchFilter.Empty();
				SearchBox->SetText(FText::FromString(SearchFilter));

				ReCacheListViewSources();

				OnSearchTextChanged(FText::FromString(SearchFilter));

				FNotificationInfo Info(FText::Format(LOCTEXT("LoadSuccess", "Results loaded from: {0}"), FText::FromString(OpenFilenames[0])));
				Info.ExpireDuration = 5.0f;
				FSlateNotificationManager::Get().AddNotification(Info);
			}
			else
			{
				FNotificationInfo Info(LOCTEXT("LoadFailed", "Failed to load results!"));
				Info.ExpireDuration = 5.0f;
				FSlateNotificationManager::Get().AddNotification(Info);
			}
		}
	}
	return FReply::Handled();
}

void SVisualizeMeshSimilarity::BrowseAssetsInGroup(int32 InGroupIndex)
{
	if (!CachedSimilarGroupSource.IsValidIndex(InGroupIndex))
		return;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	const TArray<FSoftObjectPath>& MeshesObjectPaths = CachedSimilarGroupSource[InGroupIndex]->SimilarGroup->SimilarStaticMeshes;
	TArray<FAssetData> AssetsToBrowse;
	for (const FSoftObjectPath& ObjectPath : MeshesObjectPaths)
	{
		FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(ObjectPath);
		if (AssetData.IsValid())
		{
			AssetsToBrowse.Add(AssetData);
		}
		else
		{

			FNotificationInfo Info(FText::Format(LOCTEXT("AssetNotFoundForGroup", "No valid assets found for {0}"), FText::FromString(ObjectPath.ToString())));
			Info.ExpireDuration = 5.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}

	if (AssetsToBrowse.Num() > 0)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToBrowse);
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("NoValidAssetsFoundForGroup", "No valid assets found in this group"));
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

FText SVisualizeMeshSimilarity::GetStatusText(EPreprocessStatus InStatus)
{
	check(InStatus > EPreprocessStatus::NoError);

	return FText::FromString(PreprocessStatusToString(InStatus));
}

#undef LOCTEXT_NAMESPACE
