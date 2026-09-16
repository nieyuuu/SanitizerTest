#pragma once

#include <type_traits>

#include "AssetRegistry/AssetRegistryModule.h"

constexpr int32 MIN_NUM_OF_BATCHES = 1;

template <bool bConsiderDiskSize = true, bool bConsiderDependencies = true>
struct FDefaultPayloadCalculator
{
	inline double operator()(const FAssetData& InAssetData) const
	{
		if (!InAssetData.IsValid())
		{
			return 0.0;
		}

		TSet<FName> AssetsToCheck;
		if constexpr (bConsiderDependencies)
		{
			RecursiveGetDependencies(InAssetData.PackageName, AssetsToCheck);
		}

		AssetsToCheck.Add(InAssetData.PackageName);

		double Payload = 0.0;
		for (const FName& Asset : AssetsToCheck)
		{
			if constexpr (bConsiderDiskSize)
			{
				const FString PackageFileName = FPackageName::LongPackageNameToFilename(Asset.ToString());

				const FString UAssetPath = FPaths::SetExtension(PackageFileName, TEXT(".uasset"));
				const FString UExpPath = FPaths::SetExtension(PackageFileName, TEXT(".uexp"));
				const FString UBulkPath = FPaths::SetExtension(PackageFileName, TEXT(".ubulk"));

				if (IFileManager::Get().FileExists(*UAssetPath))
				{
					Payload += IFileManager::Get().FileSize(*UAssetPath);
				}
				if (IFileManager::Get().FileExists(*UExpPath))
				{
					Payload += IFileManager::Get().FileSize(*UExpPath);
				}
				if (IFileManager::Get().FileExists(*UBulkPath))
				{
					Payload += IFileManager::Get().FileSize(*UBulkPath);
				}
			}
			else
			{
				// Default to 1 KB per asset if bConsiderDiskSize is false
				Payload += (1.0 * 1024.0);
			}
		}

		return Payload / 1024.0;
	}

private:
	static inline void RecursiveGetDependencies(const FName& InPackageName, TSet<FName>& OutAllDependencies)
	{
		const IAssetRegistry& AssetRegistry = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();

		TArray<FName> DependenciesOfThisPackage;
		AssetRegistry.GetDependencies(InPackageName, DependenciesOfThisPackage);

		for (const FName& Dependency : DependenciesOfThisPackage)
		{
			if (!Dependency.ToString().StartsWith(TEXT("/Script")) && AssetRegistry.GetAssetPackageDataCopy(Dependency).IsSet())
			{
				const uint32 DependencyHash = GetTypeHash(Dependency);
				if (!OutAllDependencies.ContainsByHash(DependencyHash, Dependency))
				{
					OutAllDependencies.AddByHash(DependencyHash, Dependency);

					RecursiveGetDependencies(Dependency, OutAllDependencies);
				}
			}
		}
	}
};

struct FLoadBalancer
{
public:
	template <typename FPayloadCalculator = FDefaultPayloadCalculator>
	static inline TArray<TArray<FString>> BalanceAssets(const TArray<FAssetData>& InAssetDatas, int32 InNumOfBatches, FPayloadCalculator&& InPayloadCalculator = std::decay_t<FPayloadCalculator>{})
	{
		return BalanceAssetsImpl(InAssetDatas, InNumOfBatches, Forward<FPayloadCalculator>(InPayloadCalculator));
	}

private:
	template <typename FPayloadCalculator>
	static inline TArray<TArray<FString>> BalanceAssetsImpl(const TArray<FAssetData>& InAssetDatas, int32 InNumOfBatches, FPayloadCalculator&& InPayloadCalculator)
	{
		if (InAssetDatas.IsEmpty())
		{
			return {};
		}

		struct FPayloadAsset
		{
			double Payload;
			FString ObjectPath;
		};

		TArray<FPayloadAsset> PayloadAssets;
		PayloadAssets.Reserve(InAssetDatas.Num());
		for (int32 Index = 0; Index < InAssetDatas.Num(); ++Index)
		{
			PayloadAssets.Add({ InPayloadCalculator(InAssetDatas[Index]),InAssetDatas[Index].GetObjectPathString() });
		}

		PayloadAssets.Sort([](const FPayloadAsset& A, const FPayloadAsset& B) {
			return A.Payload > B.Payload;
			});

		const int32 ActualNumOfBatches = FMath::Clamp(InNumOfBatches, MIN_NUM_OF_BATCHES, InAssetDatas.Num());

		TArray<TArray<FString>> BalancedBatches;
		TArray<double> PayloadSums;
		TArray<int32> AssetCounts;
		BalancedBatches.AddDefaulted(ActualNumOfBatches);
		PayloadSums.AddZeroed(ActualNumOfBatches);
		AssetCounts.AddZeroed(ActualNumOfBatches);

		for (const FPayloadAsset& Asset : PayloadAssets)
		{
			int32 BestBatchIndex = 0;
			double BestBatchPayloadSum = PayloadSums[0];
			int32 BestBatchAssetCount = AssetCounts[0];

			for (int32 i = 1; i < ActualNumOfBatches; ++i)
			{
				if (PayloadSums[i] < BestBatchPayloadSum || (PayloadSums[i] == BestBatchPayloadSum && AssetCounts[i] < BestBatchAssetCount))
				{
					BestBatchIndex = i;
					BestBatchPayloadSum = PayloadSums[i];
					BestBatchAssetCount = AssetCounts[i];
				}
			}

			BalancedBatches[BestBatchIndex].Add(Asset.ObjectPath);
			PayloadSums[BestBatchIndex] += Asset.Payload;
			++AssetCounts[BestBatchIndex];
		}

		return MoveTemp(BalancedBatches);
	}
};
