/**
 * @file FoliageComponent.h
 * @brief 규칙 기반 식생 — 배치 규칙(`PlacementRule`)으로 지형 위에 풀 · 나무를 씨앗 고정으로 놓고, GPU 인스턴스로 그리며(셀마다 배치,
 *        거리 컬링 · 페이드), 바람 · 상호작용 휘어짐을 정점 셰이더가 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Environment/Placement/PlacementRule.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class EnvironmentMaterial;
    class GameObjectManager;
    class Mesh;
    class MeshInstanceBatch;
    class PrimitiveRegistry;

    /** @brief 식생 레이어가 고르는 메시 하나입니다. `_meshId` 는 `.mesh` 경로 또는 내장 도형 이름(`GrassClump` 포함)입니다. */
    REFLECT()
    struct SW_API FoliageMesh
    {
        REFLECT_BODY();

        PROPERTY( AssetPath, AssetType = "Mesh" )
        string _meshId{};
        PROPERTY( Min = 0.0 )
        float32 _weight{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 식생 레이어 하나 — 배치 규칙 + 메시 + 머티리얼 + 그리기 값(페이드 · 바람 반응 · 색 흔들기)입니다. */
    REFLECT()
    struct SW_API FoliageLayer
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
        PROPERTY()
        PlacementRule _rule{};
        PROPERTY()
        vector<FoliageMesh> _listMesh{};
        PROPERTY( AssetPath, AssetType = "Material", Tooltip = "foliage.material (casts shadows) or grass.material (no shadows)" )
        string _materialPath{ "engine/materials/foliage.material" };
        PROPERTY( Tooltip = "Linear RGB tint" )
        float4 _tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Per-instance brightness variation" )
        float32 _tintVariation{ 0.15f };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Instances start shrinking into the ground here" )
        float32 _fadeStart{ 60.0f };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Instances are gone (and their cells culled) beyond this" )
        float32 _fadeEnd{ 80.0f };
        PROPERTY( Min = 0.0, Tooltip = "Multiplies the wind sway" )
        float32 _windResponse{ 1.0f };
        PROPERTY( Min = 0.01, Units = m, Tooltip = "Height at which the sway reaches full strength" )
        float32 _swayHeight{ 1.0f };
        PROPERTY( Min = 0.0, Tooltip = "How far influencer spheres push the foliage" )
        float32 _bendStrength{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class FoliageComponent
     * @brief 오너 위치 둘레 `_regionSize`(0 이면 아래 지형 전체)에 레이어마다 배치를 계산하고, (레이어, 메시, 셀)마다 `MeshInstanceBatch` 하나로 그립니다.
     * @details 배치는 결정적이라(같은 규칙 · 씨앗 · 지형 → 같은 인스턴스) 씬에는 규칙만 저장되고 로드 때 다시 계산합니다 — 쿠킹이 인스턴스를 굽지
     *          않아도 같은 숲입니다(언리얼 PCG 의 런타임 생성 자리). 표면은 오너 아래 지형(`TerrainComponent`)이고 없으면 오너 높이의 평면입니다.
     *          셀(`_cellSize`)은 컬링 단위입니다 — 카메라에서 레이어의 페이드 끝보다 먼 셀은 배치를 숨기고(GpuScene 에 실리지 않는다), 가까운 셀은
     *          GPU 컬링이 인스턴스마다 절두체로 거릅니다. 페이드 구간에서는 정점 셰이더가 인스턴스를 뿌리 쪽으로 줄여 사라지게 합니다.
     *          바람(`WindComponent`) · 카메라 위치 · 휘게 하는 구(`FoliageInfluencerComponent`) · 시간은 틱마다 레이어 머티리얼 인스턴스에 실립니다.
     *          인스턴스마다 위상은 셰이더가 위치 해시로, 밝기 흔들기는 인스턴스 색 칸(`GpuSpriteInstanceData` 의 tint)으로 받습니다.
     *          스레드: 배치 다시 만들기(`rebuildFoliage`)는 틱 밖에서만 부릅니다. 틱은 자기 배치 · 자기 머티리얼 인스턴스만 씁니다.
     */
    REFLECT( Category = "Environment", DisplayName = "Foliage", Tooltip = "Rule-placed GPU-instanced grass and trees with wind" )
    class SW_API FoliageComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        FoliageComponent();
        virtual ~FoliageComponent() override;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onPostLoad() override;
        void onBeginPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;
        void onTick( float32 deltaTime ) override;

        /** @brief 배치를 다시 계산하고 셀 배치를 다시 만듭니다. 등록 전이면 아무것도 하지 않습니다. */
        void rebuildFoliage();
        /**
         * @brief 레이어 @p layerIndex 의 배치를 계산합니다(그리지 않습니다 — 시험 · 도구). 표면은 오너 아래 지형입니다.
         * @return 레이어가 없으면 false 입니다.
         */
        bool computeLayerPlacements( uint32 layerIndex, vector<PlacementInstance>& outListInstance ) const;
        /** @brief 셀 거리 컬링과 머티리얼 값(바람 · 시간 · 카메라 · 휘게 하는 구)을 @p viewPosition 으로 갱신합니다. 틱이 부르고 시험이 직접 부릅니다. */
        void updateView( const float3& viewPosition );

        /** @brief 만든 인스턴스 수(모든 레이어)입니다. */
        uint32 getInstanceCount() const { return _instanceCount; }
        /** @brief 셀 배치 수입니다. */
        uint32 getBatchCount() const { return static_cast<uint32>( _listCell.size() ); }
        /** @brief 지금 보이는 셀 배치 수입니다. */
        uint32 getVisibleBatchCount() const;

        void setLayers( const vector<FoliageLayer>& listLayer ) { _listLayer = listLayer; }
        void setExclusions( const vector<PlacementExclusion>& listExclusion ) { _listExclusion = listExclusion; }
        void setRegionSize( const float2& size ) { _regionSize = size; }
        void setCellSize( float32 cellSize ) { _cellSize = cellSize; }

    private:
        /** @brief (레이어, 메시, 셀) 하나의 배치입니다. */
        struct Cell
        {
            unique_ptr<MeshInstanceBatch> _batch;
            float3                        _center{};
            float32                       _radius{ 0.0f };
            uint32                        _layerIndex{ 0 };
        };

        /** @brief 배치를 계산할 평면 사각형입니다 — 크기가 0 이면 아래 지형 전체, 지형도 없으면 빈 사각형입니다. */
        PlacementRegion computeRegion() const;
        /** @brief 셀 배치를 모두 등록부에서 빼고 놓습니다. */
        void releaseCells();

    private:
        PROPERTY( Category = "Foliage", DisplayName = "Layers" )
        vector<FoliageLayer> _listLayer;
        PROPERTY( Category = "Foliage", DisplayName = "Exclusions", Tooltip = "Areas left empty (paths, buildings, water)" )
        vector<PlacementExclusion> _listExclusion;
        PROPERTY( Category = "Foliage", DisplayName = "Region Size", Units = m, Tooltip = "Extent around the owner; zero covers the terrain below" )
        float2 _regionSize;
        PROPERTY( Category = "Foliage", DisplayName = "Cell Size", Min = 4.0, Units = m, Tooltip = "Culling cell (one instance batch per mesh and cell)" )
        float32 _cellSize;

        vector<Cell>                            _listCell;
        vector<unique_ptr<EnvironmentMaterial>> _listLayerMaterial;  ///< 레이어마다 하나(바람 값을 레이어마다 싣는다)
        PrimitiveRegistry*                      _pPrimitiveRegistry; ///< 등록된 매니저의 등록부(구조 링크라 생포인터)
        float32                                 _time;
        uint32                                  _instanceCount;
        uint8                                   _bBuiltOnTerrain : 1; ///< 마지막 계산이 지형 위였다(아니면 시작 때 지형을 다시 찾는다)
        [[maybe_unused]] uint8                  _reserved        : 7;
    };
} // namespace sw
