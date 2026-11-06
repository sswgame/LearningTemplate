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

    REFLECT()
    struct AssetRefSample
    {
        REFLECT_BODY();

        PROPERTY()
        AssetRef<AssetRefSampleTarget> _reference;

        PROPERTY()
        vector<AssetRef<AssetRefSampleTarget>> _listReference;
    };
} // namespace sw
