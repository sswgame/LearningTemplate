/**
 * @file FoliageComponent.h
 * @brief 규칙 기반 식생입니다. 배치 규칙(`PlacementRule`)으로 지형 위에 풀과 나무를 시드 고정으로 놓고, GPU 인스턴스로 그리며(셀마다 배치,
 *        거리 컬링과 페이드), 바람과 상호작용 휘어짐은 정점 셰이더가 줍니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
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
    class TerrainComponent;

    /** @brief 식생 레이어가 고르는 메시 하나입니다. `_meshID` 는 `.mesh` 경로이거나 내장 도형 이름(`GrassClump` 포함)입니다. */
    REFLECT()
    struct SW_API FoliageMesh
    {
        REFLECT_BODY();

        PROPERTY( AssetPath, AssetType = "Mesh" )
        string _meshID{};
        PROPERTY( Min = 0.0 )
        float32 _weight{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 식생 레이어 하나입니다. 배치 규칙, 메시, 머티리얼, 그리기 값(페이드, 바람 반응, 색 흔들기)을 담습니다. */
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
        PROPERTY( AssetPath, AssetType = "Material", Tooltip = "foliage.material (casts shadows) or grass.material (no shadows). Empty uses the engine default foliage material" )
        string _materialPath{}; ///< 비면 엔진 기본 에셋 표의 `_foliageMaterial` 이다
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
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Radius each instance blocks on the navmesh (trunks, rocks); 0 does not block (grass)" )
        float32 _navBlockRadius{ 0.0f }; ///< 0 보다 크면 인스턴스마다 이 반지름의 걷지 않는 상자를 베이크 기하에 넣는다(`FoliageNavGeometry.cpp`)
        PROPERTY( Min = 0.01, Units = m, Tooltip = "Height of the navmesh block above the root (before instance scale)" )
        float32 _navBlockHeight{ 2.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class FoliageComponent
     * @brief 오너 위치 둘레 `_regionSize`(0 이면 아래 지형 전체)에 레이어마다 배치를 계산하고, (레이어, 메시, 셀)마다 `MeshInstanceBatch` 하나로 그립니다.
     * @details 배치는 결정적이라(같은 규칙, 시드, 지형이면 같은 인스턴스) 씬에는 규칙만 저장하고 로드할 때 다시 계산한다. 쿠킹이 인스턴스를 베이크하지
     *          않아도 같은 숲이 나온다(언리얼 PCG 의 런타임 생성에 해당한다). 표면은 오너 아래 지형(`TerrainComponent`)이고, 없으면 오너 높이의 평면이다.
     *          셀(`_cellSize`)은 컬링 단위다. 카메라에서 레이어의 페이드 끝보다 먼 셀은 배치를 숨기고(GPUScene 에 올리지 않는다), 가까운 셀은
     *          GPU 컬링이 인스턴스마다 절두체로 거른다. 페이드 구간에서는 정점 셰이더가 인스턴스를 뿌리 쪽으로 줄여 사라지게 한다.
     *          바람(`WindComponent`), 카메라 위치, 휘게 하는 구(`FoliageInfluencerComponent`), 시간은 틱마다 레이어 머티리얼 인스턴스에 싣는다.
     *          인스턴스마다 위상은 셰이더가 위치 해시로 정하고, 밝기 흔들기는 인스턴스 색 필드(`GPUSpriteInstanceData` 의 tint)로 받는다.
     *          지형을 고치면(`writeHeights` · `writeLayerWeights` · `reloadTerrain`) 높이장 · 스플랫 세대가 오른다. 틱이 그 변화를 보고, 세대가
     *          `kTerrainSettleSeconds` 동안 그대로이면(브러시 드래그가 끝나면) 틱 뒤에 배치를 다시 계산한다 — 칠하는 프레임마다 숲 전체를 다시 뿌리지 않는다.
     *          다시 계산은 **바뀐 곳만** 한다. 레이어마다 후보의 표면 판정(`PlacementCandidate`)을 들고 있다가 지형이 알려 준 편집 사각형(`TerrainComponent::findEditedArea`)
     *          안의 후보만 다시 판정하고, 최소 거리부터는 전부 다시 풀어(앞 번호가 이기는 순서 규칙이라 연쇄로 바뀔 수 있다) 처음부터 계산한 것과 비트까지 같은 배치를 얻는다.
     *          그다음 셀마다 내용 해시를 비교해 **바뀐 셀의 배치만** 다시 만든다 — 나머지 셀 배치는 레지스트리에 그대로 남는다. 영역 · 규칙이 바뀌었거나 지형을 다시 읽었으면 통째로 다시 계산한다.
     *          스레드: 배치 다시 만들기(`rebuildFoliage`)는 틱 밖에서만 호출한다. 틱은 자기 배치와 자기 머티리얼 인스턴스만 쓴다.
     */
    REFLECT( Category = "Environment", DisplayName = "Foliage", Tooltip = "Rule-placed GPU-instanced grass and trees with wind", Meta = "Icon=Foliage, IconColor=Environment" )
    class SW_API FoliageComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 지형 세대가 이만큼(s) 그대로여야 배치를 다시 계산합니다. */
        static constexpr float32 kTerrainSettleSeconds = 0.3f;

        FoliageComponent();
        virtual ~FoliageComponent() override;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onPostLoad() override;
        void onBeginPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 소유 오브젝트를 켜고 끄면 셀 배치를 더티로 표시합니다. 빌더가 다시 확인합니다. */
        void onOwnerActiveInHierarchyChanged() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 배치를 다시 계산하고 셀 배치를 다시 만듭니다. 등록 전이면 아무것도 하지 않습니다. */
        void rebuildFoliage();
        /**
         * @brief 레이어 @p layerIndex 의 배치를 계산합니다(그리지는 않으며 테스트와 도구용입니다). 표면은 오너 아래 지형입니다.
         * @return 레이어가 없으면 false 를 반환합니다.
         */
        bool computeLayerPlacements( uint32 layerIndex, vector<PlacementInstance>& outListInstance ) const;
        /** @brief 셀 거리 컬링과 머티리얼 값(바람, 시간, 카메라, 휘게 하는 구)을 @p viewPosition 기준으로 갱신합니다. 틱이 호출하고 테스트가 직접 호출합니다. */
        void updateView( const float3& viewPosition );

        /** @brief 만든 인스턴스 수(모든 레이어)를 반환합니다. */
        uint32 getInstanceCount() const { return _instanceCount; }
        /** @brief 셀 배치 수입니다. */
        uint32 getBatchCount() const { return static_cast<uint32>( _listCell.size() ); }
        /** @brief 지금 보이는 셀 배치 수입니다. */
        uint32 getVisibleBatchCount() const;
        /** @brief 마지막 배치 계산(통째 또는 지형 편집 뒤의 부분)이 새로 만든 셀 배치 수입니다. 바뀌지 않은 셀을 다시 만들지 않는지 테스트가 봅니다. */
        uint32 getLastRebuiltBatchCount() const { return _lastRebuiltBatchCount; }

        void setLayers( const vector<FoliageLayer>& listLayer ) { _listLayer = listLayer; }
        /** @brief 레이어 수입니다. */
        uint32 getLayerCount() const { return static_cast<uint32>( _listLayer.size() ); }
        /** @brief 레이어 @p layerIndex 입니다(`getLayerCount` 보다 작아야 한다). */
        const FoliageLayer& getLayer( uint32 layerIndex ) const { return _listLayer[layerIndex]; }
        void                setExclusions( const vector<PlacementExclusion>& listExclusion ) { _listExclusion = listExclusion; }
        void                setRegionSize( const float2& size ) { _regionSize = size; }
        void                setCellSize( float32 cellSize ) { _cellSize = cellSize; }

    private:
        /** @brief (레이어, 메시, 셀) 하나의 배치입니다. */
        struct Cell
        {
            unique_ptr<MeshInstanceBatch> _batch;
            float3                        _center{};
            float32                       _radius{ 0.0f };
            uint64                        _key{ 0 };         ///< (메시 번호, 셀 x, 셀 z) 키 — 레이어 안에서 하나
            uint64                        _contentHash{ 0 }; ///< 셀 인스턴스들의 내용 해시 — 같으면 다시 만들지 않는다
            uint32                        _layerIndex{ 0 };
        };

        /** @brief 배치를 계산할 평면 사각형을 반환합니다. 크기가 0 이면 아래 지형 전체이고, 지형도 없으면 빈 사각형입니다. */
        PlacementRegion computeRegion() const;
        /** @brief 셀 배치 모두를 더티로 표시합니다. 활성 상태가 바뀌었을 때 호출합니다. */
        void markCellsDirty();
        /** @brief 아래 지형의 세대 변화를 따라가다 `kTerrainSettleSeconds` 동안 그대로이면 틱 뒤에 배치를 다시 계산하게 합니다(틱이 부른다). */
        void followTerrainEdits( GameObjectManager& manager, float32 deltaTime );
        /** @brief 지형 편집 뒤의 다시 계산입니다. 들고 있는 후보 판정을 쓸 수 있으면 편집 사각형 안만 다시 판정하고, 아니면 `rebuildFoliage` 로 통째로 계산합니다. 틱 밖에서만 부른다. */
        void refreshFromTerrainEdits();
        /**
         * @brief 레이어 @p layerIndex 의 배치를 계산합니다. 지형 위면 후보 판정을 @p pCandidates(그 레이어의 `_listLayerCandidate` 칸)에 들고, @p pDirtyArea 가 있으면 그 안의 후보만 다시 판정합니다.
         * @param pTerrain 표면 지형입니다. nullptr 이면 오너 높이의 평면이고 후보 판정을 들지 않습니다.
         */
        void computeLayerInstances( uint32 layerIndex, const TerrainComponent* pTerrain, const PlacementRegion* pDirtyArea, vector<PlacementCandidate>* pCandidates,
                                    vector<PlacementInstance>& outListInstance ) const;
        /** @brief 레이어마다 `computeLayerInstances` 를 워커에 나눠 돌리고 모두 끝날 때까지 기다립니다(레이어끼리 독립이다). 결과는 레이어 순서다. */
        void computeAllLayerInstances( const TerrainComponent* pTerrain, const PlacementRegion* pDirtyArea, vector<vector<PlacementInstance>>& outListLayerInstance );
        /** @brief 레이어 @p layerIndex 의 배치를 셀로 나눠, 내용이 바뀐 셀만 배치를 새로 만들고 없어진 셀은 놓습니다. */
        void applyLayerInstances( uint32 layerIndex, const vector<PlacementInstance>& listInstance );
        /** @brief 셀 하나(@p listIndex 의 인스턴스들, 모두 메시 @p mesh)의 배치를 만들어 레지스트리에 더합니다. 바운드는 인스턴스 위치 상자에 메시 반지름 × 최대 크기를 더한 구입니다. */
        Cell makeCell( uint32 layerIndex, uint64 key, uint64 contentHash, const vector<PlacementInstance>& listInstance, const vector<uint32>& listIndex, const shared_ptr<Mesh>& mesh,
                       float32 meshRadius );
        /** @brief 레이어 @p layerIndex 의 셀 중 @p listKeep 이 참이 아닌 것을 놓습니다(배치의 소멸자가 레지스트리에서 뺀다). 다른 레이어의 셀은 그대로입니다. */
        void releaseLayerCells( uint32 layerIndex, const vector<uint8>& listKeep );
        /** @brief 셀 배치를 모두 레지스트리에서 빼고 놓습니다. */
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
        vector<unique_ptr<EnvironmentMaterial>> _listLayerMaterial;  ///< 레이어마다 하나다(바람 값을 레이어마다 싣는다).
        vector<vector<PlacementCandidate>>      _listLayerCandidate; ///< 레이어마다 후보의 표면 판정(지형 위일 때만, 지형 편집 뒤 부분 계산용)
        PlacementRegion                         _builtRegion;        ///< 배치를 계산한 영역 — 바뀌면 후보 판정을 버린다
        PrimitiveRegistry*                      _pPrimitiveRegistry; ///< 등록된 매니저의 레지스트리다(구조 링크라 생포인터를 쓴다).
        float32                                 _time;
        float32                                 _terrainSettleTime; ///< 지형 세대가 마지막으로 바뀐 뒤 흐른 시간(s) — 칠하는 동안은 다시 만들지 않는다
        uint32                                  _instanceCount;
        uint32                                  _lastRebuiltBatchCount;      ///< `getLastRebuiltBatchCount`
        uint32                                  _builtTerrainGeneration;     ///< 배치를 계산할 때 지형의 높이장 세대(`TerrainComponent::getHeightfieldGeneration`)
        uint32                                  _builtSplatRevision;         ///< 배치를 계산할 때 지형의 스플랫 세대(`TerrainComponent::getSplatRevision`)
        uint32                                  _seenTerrainGeneration;      ///< 지난 틱에 본 높이장 세대
        uint32                                  _seenSplatRevision;          ///< 지난 틱에 본 스플랫 세대
        uint8                                   _bBuiltOnTerrain        : 1; ///< 마지막 계산이 지형 위였다(아니면 시작 때 지형을 다시 찾는다)
        uint8                                   _bTerrainRefreshPending : 1; ///< 지형 편집 뒤 다시 계산을 틱 뒤로 걸어 두었다(같은 편집으로 두 번 걸지 않는다)
        [[maybe_unused]] uint8                  _reserved               : 6;
    };
} // namespace sw
