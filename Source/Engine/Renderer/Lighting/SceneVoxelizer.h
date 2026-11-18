/**
 * @file SceneVoxelizer.h
 * @brief 씬의 정적 불투명 메시를 점유 복셀 격자(`SceneVoxelGrid`)로 옮깁니다.
 * @details 실시간 프로브 갱신(R6)이 GPU 에서 광선을 쏠 장면 표현을 만든다. 모으는 것은 프리미티브 레지스트리의 메시 컴포넌트 중 **불투명 · 보이는 · 스킨 없는** 것이다
 *          (스킨 메시와 반투명은 빛을 가리지 않는다 — 한계로 둔다). 메시의 CPU 정점(`Mesh::getVertices`, LOD 0 비인덱스 삼각형 목록)을 컴포넌트의 월드 행렬로 옮겨 칠하므로 메시 에셋이
 *          아직 읽는 중이면 그 컴포넌트는 건너뛴다. 면의 반사율은 메시가 아니라 볼륨 한 값이다(`LightProbeVolumeComponent::getBounceAlbedo` — 베이커와 같은 규칙).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"

#include "Engine/Graphics/Lighting/SceneVoxelGrid.h"

namespace sw
{
    class Material;
    class PrimitiveRegistry;

    /** @brief 씬의 메시를 복셀 격자로 만드는 도구입니다(전부 static). */
    class SW_API SceneVoxelizer
    {
    public:
        /**
         * @brief 상자 @p bounds 를 덮는 격자를 만들고 레지스트리의 메시를 칠합니다.
         * @param pSceneDefault 씬 기본 머티리얼 — 머티리얼이 비어 있는 메시는 렌더러처럼 이것으로 그려진다(`Material::resolveDrawMaterial`)
         * @param cellSize 목표 칸 크기(m) — 상자가 크면 `SceneVoxelGrid::make` 가 키운다
         * @return 칠한 격자. 상자가 유효하지 않으면 빈 격자다.
         */
        [[nodiscard]] static SceneVoxelGrid makeGrid( const PrimitiveRegistry& primitives, Material* pSceneDefault, const AABB& bounds, float32 cellSize );
        /**
         * @brief 등록 · 해제된 메시 칸 @p listChangedSlot(`PrimitiveRegistry::collectMeshSetChanges`)이 덮던 곳과 덮는 곳만 다시 칠합니다. 칠한 칸이 바뀌었으면 true 입니다.
         * @details 칸마다 칠할 때의 상자(`SceneVoxelGrid::_listSourceBox`)와 지금 상자를 보고, 가리개가 아니거나 격자 밖인 칸의 변화(움직이는 투사체 · 반투명 · 먼 스폰)는 건너뛴다.
         *          덮인 구간마다 칸을 끄고, 그 구간에 닿는 가리개를 구간 안만 다시 칠한다 — 구간 밖은 지난 결과 그대로다.
         *          shortcut: 칠한 뒤 움직인 가리개는 구간 안만 새 위치로 칠하고 옛 자리는 남는다(격자가 원래 움직임을 따라가지 않는다), 움직이는 가리개를 따라가야 하면 트랜스폼 더티로 구간을 넓힌다.
         */
        [[nodiscard]] static bool updateGrid( SceneVoxelGrid& inoutGrid, const PrimitiveRegistry& primitives, Material* pSceneDefault, const vector<uint32>& listChangedSlot );
    };
} // namespace sw
