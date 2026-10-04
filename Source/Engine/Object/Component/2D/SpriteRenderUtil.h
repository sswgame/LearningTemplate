/**
 * @file SpriteRenderUtil.h
 * @brief 스프라이트를 그리는 두 길(`SpriteComponent` · `SpriteInstanceBatch`)이 함께 쓰는 머티리얼 · 텍스처 인스턴스 규칙입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class IAssetCache;
    class Material;
    class MaterialInstance;

    /**
     * @struct SpriteRenderUtil
     * @brief 스프라이트 머티리얼 경로와 (머티리얼, 텍스처) 인스턴스 공유 표입니다.
     * @details 두 길이 **같은 표**를 써야 같은 텍스처의 스프라이트 컴포넌트와 UI 스프라이트(데미지 숫자 · HP 바)가 한 배치로 묶입니다 — 배치 키가
     *          머티리얼 인스턴스입니다.
     */
    struct SW_API SpriteRenderUtil
    {
        /** @brief 스프라이트 머티리얼(투명 · `sprite2d.hlsl`) 경로입니다. */
        static hashed_string getSpriteMaterialPath();

        /**
         * @brief (머티리얼, 텍스처)의 인스턴스를 나눠 줍니다. 없으면 만듭니다. 여러 스레드에서 불러도 됩니다(잠급니다).
         * @details 표는 **약한 참조**다(`MeshUtil::acquirePrimitive` 와 같은 모양) — 소유는 스프라이트에 있고 마지막 스프라이트가 놓으면
         *          인스턴스도 사라진다. 사라진 칸은 새로 만들 때 걷는다. 인스턴스는 텍스처(`albedoMap`) 하나만 덮어쓴다 — 프레임 · 색은 인스턴스에
         *          싣지 않는다(`GpuSpriteInstanceData`). 그것까지 덮으면 스프라이트마다 인스턴스가 갈려 배치가 하나씩 생긴다.
         *          @p normalMap 이 있으면(빛 받는 스프라이트) `normalMap` 도 덮어쓰고 키에 든다.
         */
        static shared_ptr<MaterialInstance> acquireTextureInstance( Material* pParent, hashed_string texture, hashed_string normalMap = {} );

        /** @brief `acquireTextureInstance` 의 표를 에셋 캐시 등록부에 보이는 창구입니다("SpriteTextureInstance" — 진단 · 비우기). `AssetManager` 가 올립니다. */
        static IAssetCache& getTextureInstanceCache();

        /**
         * @brief 정렬 레이어 이름 · 레이어 안 순서를 활성 표(`Render2DSettings::getActive`)로 정렬 키로 풉니다.
         * @details 모르는 레이어 이름은 데이터 오류라 @p ownerLabel 과 함께 오류를 남기고 `Default` 레이어로 그립니다.
         */
        static uint32 resolveSortKey( const hashed_string& layerName, int32 orderInLayer, string_view ownerLabel );
    };
} // namespace sw
