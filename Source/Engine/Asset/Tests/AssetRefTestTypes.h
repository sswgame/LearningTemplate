/**
 * @file AssetRefTestTypes.h
 * @brief 타입 있는 참조 칸의 샘플입니다(`TestAssetRef.cpp`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief `AssetRef<T>` 의 대상 종류입니다. 등록하지 않는다 — 값은 정의의 접미사만 본다. */
    struct AssetRefSampleTarget
    {
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".reftarget.xml", AssetSuffixRole::Plain }
        };
        static constexpr AssetTypeDef kAssetType{ "AssetRefSampleTarget", kArrAssetSuffix, 1, {}, 0, {} };
    };

    /** @brief 파일 말고 내장 이름도 받는 대상 종류입니다(메시의 `Cube` 와 같은 모양). */
    struct AssetRefSampleBuiltinTarget
    {
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".refshape", AssetSuffixRole::Plain }
        };
        static constexpr string_view  kArrBuiltinName[] = { "Box", "Ball" };
        static constexpr AssetTypeDef kAssetType        = AssetTypeDef{ "AssetRefSampleBuiltinTarget", kArrAssetSuffix, 1, {}, 0, {} }.withBuiltinNames( kArrBuiltinName );
    };

    /** @brief 접미사 없이 경로 규칙으로 가르는 카탈로그 종류입니다(`/data/crops.xml` 과 같은 모양). */
    struct AssetRefSampleCatalog
    {
        static constexpr AssetTypeDef kAssetType = AssetTypeDef::makeDataFile( "AssetRefSampleCatalog", AssetPathRule{ "game/", {}, "/data/refsample.xml" }, {} );
    };

    REFLECT()
    struct AssetRefSample
    {
        REFLECT_BODY();

        PROPERTY()
        AssetRef<AssetRefSampleTarget> _reference;

        PROPERTY()
        vector<AssetRef<AssetRefSampleTarget>> _listReference;

        PROPERTY()
        AssetRef<AssetRefSampleBuiltinTarget> _shape;

        PROPERTY()
        AssetRef<AssetRefSampleCatalog> _catalog;
    };
} // namespace sw
