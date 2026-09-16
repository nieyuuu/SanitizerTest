#pragma once

#include "Modules/ModuleManager.h"

class UContentBrowserAssetContextMenuContext;
class UContentBrowserFolderContext;

class FAssetSanitizerModule :public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterContentBrowserMenu();

	void OnGenerateSubMenuForAssetContext(UToolMenu* InMenu);
	void OnGenerateSubMenuForFolderContext(UToolMenu* InMenu);

	void OnAnalyzeStaticMeshSimilarity(const UContentBrowserAssetContextMenuContext* InAssetContextMenuContext, const UContentBrowserFolderContext* InFolderContext);
	void OnVisualizeAnalyzeResults();
};
