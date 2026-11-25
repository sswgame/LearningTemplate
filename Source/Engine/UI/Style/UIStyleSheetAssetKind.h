/**
 * @file Engine/UI/Style/UIStyleSheetAssetKind.h
 * @brief 스타일 시트 에셋 종류 정의만 담은 가벼운 헤더입니다 — 시트 칸(`AssetRef<UIStyleSheetAsset>`)을 드는 반사 헤더가 위젯 스타일 전체(`UIStyleSheet.h`)를 끌지 않게 합니다(`PrefabAssetKind.h` 와 같은 꼴).
 */
#pragma once
#include "Engine/Asset/AssetRef.h"
#include "Engine/Content/AssetTypeRegistry.h"

namespace sw
{
    struct UIStyleSheetAsset;

    /** @brief 스타일 시트 에셋 종류입니다(파일 머리말). */
    struct UIStyleSheetAssetKind
    {
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".uistyle.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `UIStyleSheet.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "UIStyleSheetAsset", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };
    };

    /** @brief `UIStyleSheetAsset` 이 완전하지 않은 곳에서도 칸의 종류를 안다. */
    template <>
    struct AssetKindOf<UIStyleSheetAsset>
    {
        static constexpr const AssetTypeDef& get() { return UIStyleSheetAssetKind::kAssetType; }
    };
} // namespace sw
