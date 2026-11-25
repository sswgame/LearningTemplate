/**
 * @file Engine/Automation/Scenario/ScenarioAssetKind.h
 * @brief 시나리오 파일(`*.scenario.xml`)을 가리키는 참조(`AssetRef<ScenarioAsset>`)의 대상 종류입니다.
 */
#pragma once
#include "Engine/Content/AssetTypeRegistry.h"

namespace sw
{
    /**
     * @struct ScenarioAsset
     * @brief 시나리오 파일의 종류입니다. 시나리오는 공유 캐시에 올리지 않으므로(재생기가 그때 읽는다) 표에 올리지 않고 타입 있는 참조의 검사에만 쓴다(`AudioPlayableAsset` 과 같다).
     */
    struct ScenarioAsset
    {
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".scenario.xml", AssetSuffixRole::Plain },
        };
        static constexpr AssetTypeDef kAssetType{ "Scenario", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };
    };
} // namespace sw
