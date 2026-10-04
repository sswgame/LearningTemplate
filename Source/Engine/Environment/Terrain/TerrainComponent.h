/**
 * @file TerrainComponent.h
 * @brief 높이장 지형 — 청크 LOD 메시 · 스플랫 레이어 머티리얼 · 구멍 · CPU 높이/노멀 질의입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Environment/EnvironmentUtil.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Environment/Terrain/TerrainMeshBuilder.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;
    class MeshInstanceBatch;
    class PrimitiveRegistry;

    /** @brief 지형 레이어 하나(풀 · 흙 · 바위 · 모래 …) — 스플랫 맵의 한 채널입니다. 최대 넷입니다. */
    REFLECT()
    struct SW_API TerrainLayer
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
        PROPERTY( Tooltip = "Linear RGB tint; alpha is unused" )
        float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "How strongly the detail texture channel modulates the tint" )
        float32 _detailStrength{ 0.5f };
        PROPERTY( Tooltip = "Project the detail texture from three axes (cliffs do not stretch)" )
        bool _bTriplanar{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class TerrainComponent
     * @brief 높이장(`.heightfield`)을 청크로 나눠 거리 LOD 메시로 그리고, 스플랫 맵(RGBA = 레이어 0..3 가중치)으로 레이어를 칠합니다.
     * @details 지형은 오너의 월드 위치에서 x · z 로 `_size` 만큼 펼쳐집니다(회전 · 크기는 보지 않습니다). 청크마다 `MeshInstanceBatch`
     *          하나(항목 하나)를 프리미티브 등록부에 넣어 메시 컴포넌트와 같은 길(GpuScene · GPU 컬링)로 그려집니다 — 청크를 오브젝트로
     *          만들지 않으므로 씬에 저장되지 않고 계층 창에도 없습니다. 틱마다 카메라 거리로 청크 LOD 를 고르고, LOD 나 이웃 LOD 가 바뀐
     *          청크만 메시를 다시 만들어 배치에 갈아 끼웁니다(`MeshInstanceBatch::setMesh`). 이웃이 더 거친 변은 접어 틈이 없습니다(`TerrainMeshBuilder`).
     *
     *          레이어 가중치 맵(`_splatTexturePath`, 비압축 RGBA8 DDS)은 GPU 텍스처로도 쓰고 CPU 로도 읽어 `computeLayerWeightsAt` 이 셰이더와
     *          같은 값을 줍니다(배치 규칙의 레이어 필터 · 밀도 맵). 레이어 색 · 디테일 · 3 축 투영은 `_listLayer` 가 머티리얼 인스턴스에 싣습니다.
     *
     *          스레드: 질의(`findHeightAt` …)는 읽기만이라 병렬 틱 · 배치 계산이 동시에 불러도 됩니다. 다시 읽기(`reloadTerrain`)는 틱 밖에서만 부릅니다.
     */
    REFLECT( Category = "Environment", DisplayName = "Terrain", Tooltip = "Heightfield terrain with LOD chunks and splat-painted layers" )
    class SW_API TerrainComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 레이어 수의 상한(스플랫 RGBA)입니다. */
        static constexpr uint32 kMaxLayerCount = 4;

        TerrainComponent();
        virtual ~TerrainComponent() override;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onPostLoad() override;
        void onPropertyChanged( hashed_string propertyName ) override;
        void onTick( float32 deltaTime ) override;
        /** @brief 오너가 움직였다고 표시만 합니다(여러 스레드에서 불린다). 다음 틱이 틱 뒤로 다시 읽기를 미룹니다. */
        void onWorldTransformUpdated() override;

        /**
         * @brief 높이장 · 스플랫을 다시 읽고 청크를 다시 만듭니다. 등록 전이면 데이터만 읽습니다.
         * @return 높이장을 읽었으면 true 입니다(경로가 비었거나 읽지 못하면 false — 그리지 않습니다).
         */
        [[nodiscard]] bool reloadTerrain();
        /**
         * @brief 카메라 위치 @p viewPosition 으로 청크 LOD 를 고르고, 바뀐 청크의 메시를 다시 만듭니다. 틱이 부르고 시험이 직접 부릅니다.
         * @return 메시를 다시 만든 청크 수입니다.
         */
        uint32 updateLods( const float3& viewPosition );

        /** @brief 월드 (x, z) 의 지형 높이입니다. 지형 밖 · 구멍이면 false 입니다. */
        [[nodiscard]] bool findHeightAt( float32 worldX, float32 worldZ, float32& outHeight ) const { return _heightfield.findHeightAt( worldX, worldZ, outHeight ); }
        /** @brief 월드 (x, z) 의 지형 노멀입니다. 지형 밖이면 false 입니다. */
        [[nodiscard]] bool findNormalAt( float32 worldX, float32 worldZ, float3& outNormal ) const { return _heightfield.findNormalAt( worldX, worldZ, outNormal ); }
        /** @brief 월드 (x, z) 가 구멍이면 true 입니다. */
        bool isHoleAt( float32 worldX, float32 worldZ ) const { return _heightfield.isHoleAt( worldX, worldZ ); }
        /** @brief 월드 (x, z) 의 레이어 가중치(합 1)입니다. */
        float4 computeLayerWeightsAt( float32 worldX, float32 worldZ ) const { return _heightfield.computeLayerWeightsAt( worldX, worldZ ); }
        /** @brief CPU 높이장입니다(물리 높이장 · 배치 표면이 읽습니다). */
        const TerrainHeightfield& getHeightfield() const { return _heightfield; }
        /** @brief 청크 배치입니다. */
        const TerrainChunkLayout& getChunkLayout() const { return _layout; }
        /** @brief 청크 (x, z) 의 지금 LOD 입니다. 범위 밖이면 0 입니다. */
        uint32 getChunkLod( uint32 chunkX, uint32 chunkZ ) const;
        /** @brief 청크 (x, z) 의 지금 메시 정점 수입니다(시험). */
        uint32 getChunkVertexCount( uint32 chunkX, uint32 chunkZ ) const;

        /**
         * @brief @p manager 의 지형 가운데 월드 (x, z) 를 덮는 첫 지형입니다. 없으면 nullptr 입니다.
         * @details 포인터는 이 호출 안에서만 씁니다(빌린 것). 병렬 틱 안에서 불러도 됩니다(읽기만 합니다).
         */
        static TerrainComponent* findTerrainAt( const GameObjectManager& manager, float32 worldX, float32 worldZ );

        void setHeightfieldPath( string_view path ) { _heightfieldPath = path; }
        void setSplatTexturePath( string_view path ) { _splatTexturePath = path; }
        void setDetailTexturePath( string_view path ) { _detailTexturePath = path; }
        void setMaterialPath( string_view path ) { _materialPath = path; }
        void setSize( const float2& size ) { _size = size; }
        void setHeightRange( float32 heightMin, float32 heightMax );
        void setChunkCells( uint32 chunkCells ) { _chunkCells = chunkCells; }
        void setLodDistance( float32 lodDistance ) { _lodDistance = lodDistance; }
        void setLayers( const vector<TerrainLayer>& listLayer ) { _listLayer = listLayer; }

    private:
        /** @brief 청크 하나 — 배치 하나와 지금 메시의 LOD · 이웃 LOD 입니다. */
        struct Chunk
        {
            unique_ptr<MeshInstanceBatch> _batch;
            uint32                        _lod{ 0 };
            uint32                        _arrNeighborLod[static_cast<uint32>( TerrainChunkSide::Count )]{};
            uint32                        _vertexCount{ 0 };
        };

        /** @brief 청크 배치를 등록부에서 빼고 놓습니다. */
        void releaseChunks();
        /** @brief 지금 LOD 로 청크 배치를 모두 만들어 등록합니다. */
        void createChunks();
        /** @brief 청크 하나의 메시를 지금 LOD · 이웃 LOD 로 다시 만들어 배치에 겁니다. */
        void rebuildChunkMesh( uint32 chunkIndex );
        /** @brief 청크 (x, z) 의 이웃 LOD 를 `TerrainChunkSide` 순서로 채웁니다(가장자리는 자기 LOD). */
        void fillNeighborLods( uint32 chunkX, uint32 chunkZ, const vector<uint32>& listLod, uint32 ( &outArrLod )[4] ) const;
        /** @brief 머티리얼을 잡고 인스턴스에 레이어 · 텍스처 값을 싣습니다. 엔진 서비스가 없으면(CPU 시험) 건너뜁니다. */
        void acquireMaterial();
        /** @brief 스플랫 DDS 를 CPU 로 읽어 높이장에 줍니다. 비압축 RGBA8/BGRA8 만 읽습니다. */
        void loadSplatWeights();

    private:
        PROPERTY( Category = "Terrain", DisplayName = "Heightfield", AssetPath, Tooltip = "Imported .heightfield asset" )
        string _heightfieldPath;
        PROPERTY( Category = "Terrain", DisplayName = "Material", AssetPath, AssetType = "Material", Tooltip = "Terrain material (terrain.hlsl)" )
        string _materialPath;
        PROPERTY( Category = "Terrain", DisplayName = "Splat Map", AssetPath, AssetType = "Texture", Tooltip = "Layer weights RGBA = layers 0..3 (uncompressed RGBA8 DDS)" )
        string _splatTexturePath;
        PROPERTY( Category = "Terrain", DisplayName = "Detail Map", AssetPath, AssetType = "Texture", Tooltip = "Tileable detail, one channel per layer" )
        string _detailTexturePath;
        PROPERTY( Category = "Terrain", DisplayName = "Layers", Tooltip = "Up to four layers, one per splat channel" )
        vector<TerrainLayer> _listLayer;
        PROPERTY( Category = "Terrain", DisplayName = "Size", Meta = "Units=m", Tooltip = "World extent along x and z" )
        float2 _size;
        PROPERTY( Category = "Terrain", DisplayName = "Height Min", Meta = "Units=m" )
        float32 _heightMin;
        PROPERTY( Category = "Terrain", DisplayName = "Height Max", Meta = "Units=m" )
        float32 _heightMax;
        PROPERTY( Category = "Terrain", DisplayName = "Detail Tiling", Min = 0.0, Tooltip = "Detail texture repeats per meter" )
        float32 _detailTiling;
        PROPERTY( Category = "Terrain", DisplayName = "Cliff Start", Min = 0.0, Max = 1.0, Tooltip = "Normal Y below which the cliff layer takes over" )
        float32 _cliffStart;
        PROPERTY( Category = "Terrain", DisplayName = "Cliff Layer", Min = -1, Max = 3, Tooltip = "Layer forced on steep slopes (-1 = off)" )
        int32 _cliffLayer;
        PROPERTY( Category = "LOD", DisplayName = "Chunk Cells", Min = 2, Tooltip = "Cells per chunk side (power of two dividing resolution - 1)" )
        uint32 _chunkCells;
        PROPERTY( Category = "LOD", DisplayName = "LOD Distance", Min = 0.0, Meta = "Units=m", Tooltip = "Distance of the first LOD step; each next step doubles it" )
        float32 _lodDistance;

        TerrainHeightfield  _heightfield;
        TerrainChunkLayout  _layout;
        vector<Chunk>       _listChunk;
        vector<uint32>      _listWantedLod; ///< updateLods 의 스크래치(청크마다 고른 LOD)
        EnvironmentMaterial _material;
        PrimitiveRegistry*  _pPrimitiveRegistry; ///< 등록된 매니저의 등록부(구조 링크라 생포인터)
        atomic<bool>        _bOriginDirty;       ///< 오너가 움직여 지형 원점이 낡았다
    };
} // namespace sw
