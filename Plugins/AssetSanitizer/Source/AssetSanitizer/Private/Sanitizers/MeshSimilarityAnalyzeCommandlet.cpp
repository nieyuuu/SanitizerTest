#include "MeshSimilarityAnalyzeCommandlet.h"

#include "MeshSimilarityAnalyzer.h"

#include "AssetCompilingManager.h"

DEFINE_LOG_CATEGORY(LogMeshSimilarityAnalyzeCommandlet);

#define MODE TEXT("Mode")

#define BALANCER           TEXT("Balancer")
#define NUM_OF_BATCHES     TEXT("NumOfBatches")
#define DIRS_TO_PROCESS    TEXT("DirsToProcess")
#define OUTPUT_DIR         TEXT("OutputDir")
#define CONSIDER_DISK_SIZE TEXT("ConsiderDiskSize")

#define PREPROCESSOR          TEXT("Preprocessor")
#define QUANTIZATION_EXPONENT TEXT("QuantizationExponent")
#define INPUT_FILE            TEXT("InputFile")

#define ANALYZER         TEXT("Analyzer")
#define ANALYZER_TYPE    TEXT("AnalyzerType")
#define REGISTRY_STORAGE TEXT("RegistryStorage")

int32 UMeshSimilarityAnalyzeCommandlet::Main(const FString& InCmdLineParams)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> Arguments;
	ParseCommandLine(*InCmdLineParams, Tokens, Switches, Arguments);

	if (!Arguments.Contains(MODE) || Arguments[MODE].Len() == 0)
	{
		UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Execution mode required for this commandlet. Please specify -Mode=Balancer, -Mode=Preprocessor or -Mode=Analyzer in command line."));
		return -1;
	}

	if (Arguments[MODE] == BALANCER)
	{
		int32 NumOfBatches = MIN_NUM_OF_BATCHES;
		if (Arguments.Contains(NUM_OF_BATCHES) && Arguments[NUM_OF_BATCHES].IsNumeric())
		{
			NumOfBatches = FMath::Max(FCString::Atoi(*Arguments[NUM_OF_BATCHES]), NumOfBatches);
		}

		TArray<FString> DirsToProcess;
		if (Arguments.Contains(DIRS_TO_PROCESS) && Arguments[DIRS_TO_PROCESS].EndsWith(TEXT(".txt")))
		{
			if (!FFileHelper::LoadFileToStringArray(DirsToProcess, *Arguments[DIRS_TO_PROCESS]))
			{
				UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Failed to load text file [%s] to string array."), *Arguments[DIRS_TO_PROCESS]);
				return -1;
			}
			if (DirsToProcess.Num() == 0)
			{
				UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Text file [%s] is empty."), *Arguments[DIRS_TO_PROCESS]);
				return -1;
			}
			for (FString& Directory : DirsToProcess)
			{
				if (!Directory.EndsWith("/"))
				{
					Directory += "/";
				}
			}
		}
		else
		{
			//Default behavior is to process entire Content folder
			DirsToProcess.Add(TEXT("/Game/"));
		}

		if (!Arguments.Contains(OUTPUT_DIR) || Arguments[OUTPUT_DIR].Len() == 0)
		{
			UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Balancer mode needs an output directory. please specify output directory eg. -OutputDir=\"D:/BalancerOutput/\" in command line."));
			return -1;
		}

		FString OutputDir = Arguments[OUTPUT_DIR];
		if (!OutputDir.EndsWith("/"))
		{
			OutputDir += "/";
		}

		//Delete existing
		if (FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*OutputDir))
		{
			if (!FPlatformFileManager::Get().GetPlatformFile().DeleteDirectoryRecursively(*OutputDir))
			{
				UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Failed to delete existing output directory [%s]."), *OutputDir);
				return -1;
			}
		}
		//Create new
		if (!FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutputDir))
		{
			UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Failed to create output directory [%s]."), *OutputDir);
			return -1;
		}

		bool bConsiderDiskSize = false;
		if (Switches.Contains(CONSIDER_DISK_SIZE))
		{
			bConsiderDiskSize = true;
		}

		return RunBalancerMode(NumOfBatches, bConsiderDiskSize, DirsToProcess, OutputDir);
	}
	else if (Arguments[MODE] == PREPROCESSOR)
	{
		int32 QuantizationExponent = DEFAULT_EXPONENT;
		if (Arguments.Contains(QUANTIZATION_EXPONENT) && Arguments[QUANTIZATION_EXPONENT].IsNumeric())
		{
			QuantizationExponent = FMath::Clamp(FCString::Atoi(*Arguments[QUANTIZATION_EXPONENT]), MIN_EXPONENT, MAX_EXPONENT);
		}

		if (!Arguments.Contains(INPUT_FILE) || !Arguments[INPUT_FILE].EndsWith(TEXT(".txt")))
		{
			UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Please specify an input file eg. -InputFile=\"D:/BalancerOutput/Batch0.txt\" in command line."));
			return -1;
		}

		const FString InputFile = Arguments[INPUT_FILE];
		if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*InputFile))
		{
			UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Input file [%s] does not exists."), *InputFile);
			return -1;
		}

		FString PreprocessOutput = FPaths::SetExtension(Arguments[INPUT_FILE], TEXT(".bin"));

		return RunPreprocessorMode(QuantizationExponent, InputFile, PreprocessOutput);
	}
	else if (Arguments[MODE] == ANALYZER)
	{
		EAnalyzerType AnalyzerType = EAnalyzerType::PerVertex;
		if (Arguments.Contains(ANALYZER_TYPE))
		{
			if (Arguments[ANALYZER_TYPE] == TEXT("PerVertex"))
			{
				AnalyzerType = EAnalyzerType::PerVertex;
			}
			else if (Arguments[ANALYZER_TYPE] == TEXT("XxHash64"))
			{
				AnalyzerType = EAnalyzerType::XxHash64;
			}
			else if (Arguments[ANALYZER_TYPE] == TEXT("XxHash128"))
			{
				AnalyzerType = EAnalyzerType::XxHash128;
			}
			else
			{
				UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Unidentified analyzer type [%s]. Please specify -AnalyzerType=PerVertex, -AnalyzerType=XxHash64 or -AnalyzerType=XxHash128 in command line."), *Arguments[ANALYZER_TYPE]);
				return -1;
			}
		}

		if (!Arguments.Contains(REGISTRY_STORAGE) || Arguments[REGISTRY_STORAGE].Len() == 0)
		{
			UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Registry files required. Please specify the preprocess output directory eg. -RegistryStorage=\"D:/RegistryStorageFolder/\" in command line."));
			return -1;
		}

		FString RegistryStoragePath = Arguments[REGISTRY_STORAGE];
		if (!RegistryStoragePath.EndsWith("/"))
		{
			RegistryStoragePath += "/";
		}

		TArray<FString> RegistryPaths;
		IFileManager::Get().FindFiles(RegistryPaths, *RegistryStoragePath, TEXT("*.bin"));
		for (FString& FileName : RegistryPaths)
		{
			FileName = RegistryStoragePath + FileName;
		}

		if (RegistryPaths.IsEmpty())
		{
			UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Found no registries in directory [%s]"), *RegistryStoragePath);
			return -1;
		}

		const FString AnalyzeOutput = RegistryStoragePath + FString(TEXT("MeshSimilarity.json"));

		return RunAnalyzerMode(AnalyzerType, RegistryPaths, AnalyzeOutput);
	}
	else
	{
		UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Detected unidentified execution mode [%s]. Please specify -Mode=Balancer, -Mode=Preprocessor or -Mode=Analyzer in command line."), *Arguments[MODE]);
		return -1;
	}

	return 0;
}

int32 UMeshSimilarityAnalyzeCommandlet::RunBalancerMode(int32 InNumOfBatches, bool InConsiderDiskSize, const TArray<FString>& InDirectoriesToProcess, const FString& InOutputDir)
{
	TArray<TArray<FString>> BalancedBatches;
	if (InConsiderDiskSize)
	{
		BalancedBatches = FPreprocessBalancer::BalanceStaticMeshesBasedOnDiskSize(InDirectoriesToProcess, InNumOfBatches);
	}
	else
	{
		BalancedBatches = FPreprocessBalancer::BalanceStaticMeshes(InDirectoriesToProcess, InNumOfBatches);
	}

	for (int i = 0; i < BalancedBatches.Num(); ++i)
	{
		const FString& File = InOutputDir + FString::Printf(TEXT("Batch%d.txt"), i);
		if (!FFileHelper::SaveStringArrayToFile(BalancedBatches[i], *File))
		{
			UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Failed to save preprocess batch results to file [%s]."), *File);
			return -1;
		}
	}

	return 0;
}

int32 UMeshSimilarityAnalyzeCommandlet::RunPreprocessorMode(int32 InQuantizationExponent, const FString& InInputFile, const FString& InOutputFile)
{
	TArray<FString> StaticMeshesToProcess;
	if (!FFileHelper::LoadFileToStringArray(StaticMeshesToProcess, *InInputFile))
	{
		UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Failed to load preprocess batch file [%s] to string array."), *InInputFile);
		return -1;
	}

	TSet<FString> UniqueStaticMeshes;
	UniqueStaticMeshes.Reserve(StaticMeshesToProcess.Num());
	for (int i = 0; i < StaticMeshesToProcess.Num(); ++i)
	{
		UniqueStaticMeshes.Add(StaticMeshesToProcess[i]);
	}

	FPreprocessSettings Settings(InQuantizationExponent);
	TUniquePtr<FPreprocessRegistry> Registry = FPreprocessRegistry::PreprocessStaticMeshes(UniqueStaticMeshes, Settings);

	if (!FPreprocessRegistry::SaveTo(Registry, InOutputFile))
	{
		UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Failed to save preprocess registry to [%s]."), *InOutputFile);
		return -1;
	}

	//Wait for remaining asset compiling jobs finish to avoid potential crash(Mesh Distance Field)?
	//Disable some of these like shader compiling, texture compiling, mesh distance field compling because we dont need them?
	FAssetCompilingManager::Get().FinishAllCompilation();

	return 0;
}

int32 UMeshSimilarityAnalyzeCommandlet::RunAnalyzerMode(EAnalyzerType InAnalyzerType, const TArray<FString>& InRegistryPaths, const FString& InOutputFile)
{
	TArray<TUniquePtr<FPreprocessRegistry>> RegistryStorage;
	for (const FString& RegistryPath : InRegistryPaths)
	{
		TUniquePtr<FPreprocessRegistry> Registry;
		if (!FPreprocessRegistry::LoadFrom(Registry, RegistryPath))
		{
			UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Failed to load registry from file [%s]"), *RegistryPath);
			return -1;
		}
		RegistryStorage.Add(MoveTemp(Registry));
	}

	TArray<FPreprocessRegistry*> Registries;
	Algo::Transform(RegistryStorage, Registries, [](const TUniquePtr<FPreprocessRegistry>& InRegistry) {
		return InRegistry.Get();
	});

	FAnalyzeResults Results;
	if (!AnalyzeMeshSimilarity(InAnalyzerType, Registries, Results))
	{
		UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Failed to analyze mesh similarity"));
		return -1;
	}

	if (!FAnalyzeResults::SaveTo(Results, InOutputFile))
	{
		UE_LOG(LogMeshSimilarityAnalyzeCommandlet, Error, TEXT("Failed to save final analyze results to [%s]"), *InOutputFile);
		return -1;
	}

	return 0;
}
