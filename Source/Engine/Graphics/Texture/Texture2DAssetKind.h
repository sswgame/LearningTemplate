/**
 * @file Engine/Graphics/Texture/Texture2DAssetKind.h
 * @brief 텍스처 에셋 종류 정의만 담은 가벼운 헤더입니다 — `AssetRef<Texture2D>` 칸을 드는 GameFramework 코드가 `Texture2D.h`(RHI 헤더를 읽는다) 없이 칸을 만들게 합니다.
 * @details GameFramework 대상 문(Config/Rules/Layers/TargetLayers.toml)은 RHI 헤더를 막는다. 종류 정의는 한 곳이다 — `Texture2D::kAssetType` 이 이 값을 가리킨다
 *          (`PrefabAssetKind.h` 와 같은 꼴).
 */
#pragma once
#include "Engine/Asset/AssetRef.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Foundation/RenderTargetDeclaration.h"

namespace sw
{
    class Texture2D;

    /** @brief 텍스처 에셋 종류입니다(파일 머리말). */
    struct Texture2DAssetKind
    {
        /** @brief 텍스처 파일의 접미사입니다(쿠킹한 DDS). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".dds", AssetSuffixRole::Plain },
        };
        /**
         * @brief 텍스처 에셋 종류입니다(등록은 `Texture2D.cpp` 의 `SW_ASSET_TYPE`). 전용 서버 패키지가 빼는 묶음 `Texture` 다.
         * @details 텍스처 칸(`AssetRef<Texture2D>`)은 파일이나 렌더 텍스처를 가리킨다 — 유니티 `Texture` 칸의 `RenderTexture`, 언리얼 `UTexture` 칸의 `UTextureRenderTarget2D` 와 같다.
         */
        static constexpr AssetTypeDef kAssetType = AssetTypeDef{ "Texture", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, "Texture" }.withRuntimePath( AssetPathRule{ IRenderTargetDeclarationSink::kPathPrefix, {}, {} } );
    };

    /** @brief `Texture2D` 가 완전하지 않은 곳에서도 칸의 종류를 안다. */
    template <>
    struct AssetKindOf<Texture2D>
    {
        static constexpr const AssetTypeDef& get() { return Texture2DAssetKind::kAssetType; }
    };
} // namespace sw
