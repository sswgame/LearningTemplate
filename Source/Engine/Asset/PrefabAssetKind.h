/**
 * @file Engine/Asset/PrefabAssetKind.h
 * @brief 프리팹 에셋 종류 정의만 담은 가벼운 헤더입니다 — `AssetRef<PrefabAsset>` 칸을 드는 헤더가 `PrefabAsset.h`(모델 헤더) 없이 칸을 만들게 합니다.
 * @details 외형 · 꾸미기 데이터(`ItemVisual.h` · `CustomizationSchema.h`)가 프리팹을 칸으로 들고, 에디터 패널 뷰가 그 헤더를 뷰 모델을 거쳐 읽는다 — 패널 뷰는 모델 헤더를
 *          포함할 수 없다(층 규칙). 종류 정의는 한 곳이다 — `PrefabAsset::kAssetType` 이 이 값을 가리킨다(`ZoneProfileAssetKind.h` 와 같은 꼴, 언리얼 `TSoftObjectPtr` 가
 *          형식 헤더 없이 경로를 드는 자리).
 */
#pragma once
#include "Engine/Asset/AssetRef.h"
#include "Engine/Content/AssetTypeRegistry.h"

namespace sw
{
    class PrefabAsset;

    /** @brief 프리팹 에셋 종류입니다(파일 머리말). */
    struct PrefabAssetKind
    {
        /** @brief 프리팹 파일의 접미사입니다 — 저작 소스 둘(XML · JSON), 쿠킹본, 확장자 없는 이름(쿠킹본으로 읽는다). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".prefab.xml",  AssetSuffixRole::CookSource},
            {".prefab.json",  AssetSuffixRole::CookSource},
            { ".prefab.bin",      AssetSuffixRole::Cooked},
            {     ".prefab", AssetSuffixRole::CookedAlias},
        };
        /** @brief 프리팹 에셋 종류입니다(등록은 `PrefabAsset.cpp` 의 `SW_ASSET_TYPE`, 캐시 등록은 `PrefabCache.cpp`). 판 0. */
        static constexpr AssetTypeDef kAssetType{ "Prefab", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), ".prefab.bin", 1, {} };
    };

    /** @brief `PrefabAsset` 이 완전하지 않은 곳에서도 칸의 종류를 안다. */
    template <>
    struct AssetKindOf<PrefabAsset>
    {
        static constexpr const AssetTypeDef& get() { return PrefabAssetKind::kAssetType; }
    };
} // namespace sw
