/**
 * @file SceneVoxelizer.h
 * @brief 씬의 정적 불투명 메시를 점유 복셀로 칠할 장면(`SceneVoxelScene`)을 모으고, GPU 가 칠할 일(`scene_voxelize.hlsl` 의 입력 행)을 짭니다.
 * @details 실시간 프로브 갱신(R6)이 GPU 에서 광선을 쏠 장면 표현을 만든다. 모으는 것은 프리미티브 레지스트리의 메시 컴포넌트 중 **불투명 · 보이는 · 스킨 없는** 것이다
 *          (스킨 메시와 반투명은 빛을 가리지 않는다 — 한계로 둔다). 칠하기는 GPU 다 — 렌더 프록시의 위치 흐름을 복사한 버퍼와 CPU 인덱스(LOD 0)를 컴퓨트가 읽어 격자의 점유 비트에
 *          원자 OR 로 쓴다. CPU 정점을 읽지 않는다(R-15). 면의 반사율은 메시가 아니라 볼륨 한 값이다(`LightProbeVolumeComponent::getBounceAlbedo` — 베이커와 같은 규칙).
 *
 *          래스터가 아니라 컴퓨트인 까닭: 픽셀 셰이더의 3D UAV 원자 쓰기는 다섯 백엔드에 없다 — WGSL 의 원자 형식은 workgroup 변수와 read_write 저장 **버퍼**뿐이다
 *          (https://www.w3.org/TR/WGSL/#atomic-types). 구조 버퍼의 원자 연산은 다섯 백엔드 모두의 컴퓨트에 있다(HLSL SM 5 —
 *          https://learn.microsoft.com/windows/win32/direct3dhlsl/interlockedor). 삼각형마다 스레드 하나가 노멀의 가장 큰 축으로 투영한 칸(3 축 투영 — 기하 셰이더 없음)을 돌며
 *          그 칸 기둥에서 평면이 지나는 층만 분리축으로 판정한다 — 보존 래스터 없이 보수적이고, 예전 CPU 복셀화와 같은 칸이 켜진다.
 *          능력은 컴퓨트(`RHICapabilities::_bCompute`) 하나다 — 없으면 PSO 가 없고 프로브 갱신이 오류로 알린다(`RealtimeProbeUpdater::prepare`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Lighting/SceneVoxelScene.h"

namespace sw
{
    class Material;
    class Mesh;
    class PrimitiveRegistry;

    /** @brief 이번에 칠할 격자 하나입니다 — 장면, 복셀 버퍼 안의 자리, 그 자리에 이미 칠해진 지난 장면. */
    struct SceneVoxelPaintTarget
    {
        const SceneVoxelScene* _pScene{ nullptr };
        const SceneVoxelScene* _pPainted{ nullptr }; ///< 지금 자리에 칠해져 있는 장면 — nullptr 이면 격자 전체를 새로 칠한다
        uint32                 _wordOffset{ 0 };     ///< 점유 단어의 첫 단어
        uint32                 _emissionOffset{ 0 }; ///< 발광 단어의 첫 단어. 발광 칸이 없으면 `SceneVoxelizer::kNoEmission`
    };

    /** @brief GPU 가 칠할 일 한 벌입니다 — 지우기 · 칠하기 디스패치의 입력 행과, 그 일이 읽는 메시의 정점 · 인덱스 자리입니다. */
    struct SceneVoxelPaintPlan
    {
        /** @brief 칠하기가 읽는 메시 하나입니다(같은 메시는 한 번). */
        struct MeshSpan
        {
            shared_ptr<Mesh> _mesh{};
            uint32           _firstVertex{ 0 }; ///< 위치 버퍼 안의 첫 정점
            uint32           _firstIndex{ 0 };  ///< 인덱스 버퍼 안의 첫 인덱스
        };

        vector<float4>   _listClearRow{}; ///< 지우기 디스패치의 입력 행(칸마다 스레드 하나)
        vector<float4>   _listPaintRow{}; ///< 칠하기 디스패치의 입력 행(삼각형마다 스레드 하나)
        uint32           _clearThreadCount{ 0 };
        uint32           _paintThreadCount{ 0 };
        vector<MeshSpan> _listMesh{};
        uint32           _vertexCount{ 0 }; ///< 위치 버퍼에 든 정점 수
        uint32           _indexCount{ 0 };  ///< 인덱스 버퍼에 든 인덱스 수

        /** @brief 지울 것이 없으면(다시 칠할 구간이 없다) true 입니다. */
        bool isEmpty() const { return _clearThreadCount == 0; }
    };

    /** @brief 씬의 메시를 복셀 장면으로 모으고 GPU 칠하기를 짜는 도구입니다(전부 static). */
    class SW_API SceneVoxelizer
    {
    public:
        static constexpr uint32 kJobRowCount      = 9;                                    ///< 일 하나의 행 수(scene_voxelize.hlsl kJobRowCount)
        static constexpr uint32 kThreadGroupSize  = 64;                                   ///< scene_voxelize.hlsl SW_NUMTHREADS 와 같다
        static constexpr uint32 kMaxGroupsPerAxis = 65535;                                ///< 한 축의 스레드 그룹 상한(D3D11 · D3D12 · WebGPU 기본 · Vulkan 최소 보장) — 넘으면 다음 축으로 접는다
        static constexpr uint32 kThreadsPerRow    = kThreadGroupSize * kMaxGroupsPerAxis; ///< 디스패치 한 줄의 스레드 수(scene_voxelize.hlsl kThreadsPerRow)
        static constexpr uint32 kNoEmission       = 0xFFFFFFFFu;                          ///< 발광 칸이 없는 격자의 발광 단어 번호(scene_voxelize.hlsl · probe_update.hlsl kNoEmission)

        /**
         * @brief 상자 @p bounds 를 덮는 격자와 레지스트리의 메시를 모은 장면을 만듭니다.
         * @param pSceneDefault 씬 기본 머티리얼 — 머티리얼이 비어 있는 메시는 렌더러처럼 이것으로 그려진다(`Material::resolveDrawMaterial`)
         * @param cellSize 목표 칸 크기(m) — 상자가 크면 `SceneVoxelGrid::make` 가 키운다
         * @return 모은 장면. 상자가 유효하지 않으면 격자가 빈 장면이다.
         */
        [[nodiscard]] static SceneVoxelScene makeScene( const PrimitiveRegistry& primitives, Material* pSceneDefault, const AABB& bounds, float32 cellSize );
        /**
         * @brief 등록 · 해제된 메시 칸 @p listChangedSlot(`PrimitiveRegistry::collectMeshSetChanges`)만 다시 모읍니다. 칠한 결과가 바뀌면 true 입니다.
         * @details 가리개가 아니거나 격자 밖인 칸의 변화(움직이는 투사체 · 반투명 · 먼 스폰)와 같은 것으로 다시 든 칸은 건너뛴다 — 장면이 그대로라 GPU 가 다시 칠하지 않는다.
         *          shortcut: 모은 뒤 움직인 가리개는 모은 때의 자리에 남는다(격자가 원래 움직임을 따라가지 않는다), 움직이는 가리개를 따라가야 하면 트랜스폼 더티 칸을 함께 넘긴다.
         */
        [[nodiscard]] static bool updateScene( SceneVoxelScene& inoutScene, const PrimitiveRegistry& primitives, Material* pSceneDefault, const vector<uint32>& listChangedSlot );
        /**
         * @brief 지난 장면 @p painted 를 칠한 격자를 @p scene 으로 바꾸려면 다시 칠할 칸 구간들을 @p outListRange 에 적습니다(바뀐 원천이 덮던 · 덮는 구간).
         * @details 격자 모양이나 발광 목록(순위)이 다르거나 구간들의 칸 합이 격자보다 많으면 격자 전체 하나다. 바뀐 것이 없으면 빈 목록이다.
         */
        static void collectDirtyRanges( const SceneVoxelScene& painted, const SceneVoxelScene& scene, vector<SceneVoxelGrid::CellRange>& outListRange );
        /** @brief 격자들을 칠할 일을 짭니다 — 다시 칠할 구간마다 칸을 지우고, 구간에 닿는 메시를 구간 안만 칠한다. 행 배치는 `scene_voxelize.hlsl` 머리 주석이다. */
        [[nodiscard]] static SceneVoxelPaintPlan planPaint( const vector<SceneVoxelPaintTarget>& listTarget );
    };
} // namespace sw
