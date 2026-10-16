/**
 * @file TerrainComponent.h
 * @brief 높이장 지형입니다. 청크 LOD 메시, 스플랫 레이어 머티리얼, 구멍, CPU 높이와 노멀 질의를 제공합니다.
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
    class Archive;
    class GameObjectManager;
    class MeshInstanceBatch;
    class PrimitiveRegistry;

    /** @brief 지형 레이어 하나(풀, 흙, 바위, 모래 등)입니다. 스플랫 맵의 한 채널이며 최대 넷입니다. */
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
     * @details 지형은 오너의 월드 위치에서 x 와 z 방향으로 `_size` 만큼 펼쳐진다(회전과 크기는 보지 않는다). 청크마다 `MeshInstanceBatch`
     *          하나(항목 하나)를 프리미티브 레지스트리에 넣어, 메시 컴포넌트와 같은 경로(GPUScene, GPU 컬링)로 그린다. 청크를 오브젝트로
     *          만들지 않으므로 씬에 저장되지 않고 계층 창에도 보이지 않는다. 틱마다 카메라 거리로 청크 LOD 를 고르고, LOD 나 이웃 LOD 가 바뀐
     *          청크만 메시를 다시 만들어 배치에 교체한다(`MeshInstanceBatch::setMesh`). 이웃이 더 거친 변은 정점을 접어 틈이 없다(`TerrainMeshBuilder`).
     *
     *          레이어 가중치 맵(`_splatTexturePath`, `.layerweights` 데이터 에셋 — `LayerWeightsData`)은 클라이언트와 전용 서버가 같은 파일을 CPU 로 읽는다.
     *          텍스처가 아니므로 서버 패키지에 남아 서버의 레이어 질의(발소리 · 표면 재질 · 이동 속도)도 정확하다. 그리는 쪽의 GPU 텍스처는 읽은 바이트로
     *          지형마다 하나 만든다(`getCPUSplatPath`). `computeLayerWeightsAt` 이 셰이더와 같은 값을 주므로 배치 규칙의 레이어 필터와 밀도 맵에 쓸 수 있다.
     *          레이어 색, 디테일, 3 축 투영은 `_listLayer` 가 머티리얼 인스턴스에 싣는다.
     *
     *          스레드: 질의(`findHeightAt` 등)는 읽기만 하므로 병렬 틱과 배치 계산이 동시에 호출해도 된다. 다시 읽기(`reloadTerrain`)는 틱 밖에서만 호출한다.
     */
    REFLECT( Category = "Environment", DisplayName = "Terrain", Tooltip = "Heightfield terrain with LOD chunks and splat-painted layers", Meta = "Icon=Heightfield, IconColor=Environment" )
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
        /** @brief 소유 오브젝트를 켜고 끄면 청크 배치를 더티로 표시합니다. 빌더가 다시 확인합니다. */
        void onOwnerActiveInHierarchyChanged() override;
        void onTick( float32 deltaTime ) override;
        /** @brief 오너가 움직였다고 표시만 합니다(여러 스레드에서 불린다). 다음 틱이 틱 뒤로 다시 읽기를 미룹니다. */
        void onWorldTransformUpdated() override;

        /**
         * @brief 높이장과 스플랫을 다시 읽고 청크를 다시 만듭니다. 등록 전이면 데이터만 읽습니다.
         * @details 디스크 값으로 돌아간다 — 저장하지 않은 높이 · 스플랫 편집은 버린다. 트랜스폼 · 프로퍼티가 바뀌어 다시 읽을 때는 편집을 넘기는
         *          `reloadTerrainKeepingEdits` 를 쓴다.
         * @return 높이장과(정했으면) 레이어 가중치를 읽었으면 true 를 반환합니다(경로가 비었거나 읽지 못하면 false 이며, 그리지 않습니다).
         */
        [[nodiscard]] bool reloadTerrain();
        /**
         * @brief 카메라 위치 @p viewPosition 으로 청크 LOD 를 고르고, 바뀐 청크의 메시를 다시 만듭니다. 틱이 부르고 테스트가 직접 부릅니다.
         * @return 메시를 다시 만든 청크 수입니다.
         */
        uint32 updateLODs( const float3& viewPosition );
        /**
         * @brief 높이 쓰기로 낡은 청크 메시를 앞에서부터 최대 @p maxCount 개 다시 만듭니다. 틱이 프레임마다 부르고(쓰기도 한 번 부른다) 테스트가 직접 부릅니다.
         * @return 다시 만든 청크 수입니다. 남은 것은 `getStaleChunkCount` 입니다.
         */
        uint32 rebuildStaleChunkMeshes( uint32 maxCount );
        /** @brief 높이를 썼지만 메시를 아직 다시 만들지 않은 청크 수입니다. */
        uint32 getStaleChunkCount() const { return static_cast<uint32>( _listStaleChunk.size() ); }

        /** @brief 월드 (x, z) 의 지형 높이를 구합니다. 지형 밖이거나 구멍이면 false 를 반환합니다. */
        [[nodiscard]] bool findHeightAt( float32 worldX, float32 worldZ, float32& outHeight ) const { return _heightfield.findHeightAt( worldX, worldZ, outHeight ); }
        /** @brief 월드 (x, z) 의 지형 노멀입니다. 지형 밖이면 false 입니다. */
        [[nodiscard]] bool findNormalAt( float32 worldX, float32 worldZ, float3& outNormal ) const { return _heightfield.findNormalAt( worldX, worldZ, outNormal ); }
        /** @brief 월드 (x, z) 가 구멍이면 true 입니다. */
        bool isHoleAt( float32 worldX, float32 worldZ ) const { return _heightfield.isHoleAt( worldX, worldZ ); }
        /** @brief 월드 (x, z) 의 레이어 가중치(합 1)를 반환합니다. */
        float4 computeLayerWeightsAt( float32 worldX, float32 worldZ ) const { return _heightfield.computeLayerWeightsAt( worldX, worldZ ); }
        /** @brief CPU 높이장을 반환합니다(물리 충돌 `TerrainColliderComponent` 와 배치 표면이 읽습니다). */
        const TerrainHeightfield& getHeightfield() const { return _heightfield; }
        /** @brief 높이장을 다시 읽거나 높이를 쓸 때마다(`reloadTerrain`, `writeHeights`) 오르는 세대입니다. 높이장에서 만든 것(물리 바디)이 낡았는지 견줍니다. */
        uint32 getHeightfieldGeneration() const { return _heightfieldGeneration; }
        /**
         * @brief 청크마다(`getChunkLayout` 순서, z 가 바깥) 그 청크의 높이를 마지막으로 바꾼 세대(`getHeightfieldGeneration` 의 그때 값)입니다.
         * @details `writeHeights` 는 바뀐 샘플을 쓰는 청크만 올리고 `reloadTerrain` 은 모두 올린다. 물리 바디처럼 청크 단위로 만든 것이 낡은 청크만 다시 만든다.
         */
        const vector<uint32>& getChunkGenerations() const { return _listChunkGeneration; }
        /**
         * @brief 높이 샘플 전부(`getHeightfield().getHeightSamples()` 를 복사해 고친 사본)에서 @p rect 의 샘플을 반영합니다(월드 높이, m).
         * @details 바뀐 샘플이 닿는 청크(노멀이 이웃 샘플을 읽으므로 한 샘플 바깥까지)의 바운드를 고치고 메시는 앞의 넷까지 지금, 나머지는 다음 틱들에 나눠 다시 만들고, `getHeightfieldGeneration` 과 바뀐 샘플을 쓰는
         *          청크의 `getChunkGenerations` 를 올려 그 청크의 물리 바디만 다시 만들어지게 한다. 되돌리기는 편집 전에 복사해 둔 사본과 같은 @p rect 로 이 함수를 다시 부르면 된다.
         *          틱 밖에서만 부른다(`reloadTerrain` 과 같다 — 락이 없다). 메시를 다시 만드는 것이라 스트로크마다가 아니라 프레임에 한 번 부른다.
         *          이 지형을 읽어 만든 배치 결과(식생 등)는 다시 계산하지 않는다. 속성 변경 · 오너 이동의 다시 읽기는 쓴 값을 넘기고(`reloadTerrainKeepingEdits`),
         *          `reloadTerrain` 을 직접 부르면 에셋 값으로 돌아간다.
         * @return 반영했으면 true 입니다(지형이 없거나 크기 · 범위가 틀리면 false 이고 아무것도 바꾸지 않습니다).
         */
        [[nodiscard]] bool writeHeights( const vector<float32>& listHeight, const TerrainSampleRect& rect );
        /**
         * @brief 레이어 가중치 전부(`getHeightfield().readSplatWeights` 로 읽어 고친 사본)에서 @p rect(스플랫 텍셀 좌표)를 CPU 스플랫에 반영하고 `getSplatRevision` 을 올립니다.
         * @details 텍스처는 읽을 때 지형마다 하나 만든 CPU 텍스처(`getCPUSplatPath`, `TextureCache::createCPUTexture`)라 같은 에셋을 쓰는 다른 지형은 칠해지지 않고,
         *          디바이스를 다시 만들어도 칠한 값이 남는다(언리얼 랜드스케이프 가중치 맵처럼 지형마다 자기 텍스처를 둔다). @p rect 만 한 번에 맡긴다
         *          (`TextureCache::requestRegionUpdate` — 렌더 스레드가 다음 프레임 기록 전에 올린다). 디스크 에셋은 `saveSplat` 전까지 그대로이고
         *          `reloadTerrain` 은 디스크 값으로 돌아간다. 틱 밖에서만 부른다.
         */
        [[nodiscard]] bool writeLayerWeights( const vector<float4>& listWeight, const TerrainSampleRect& rect );
        /** @brief `writeLayerWeights` 마다 오르는 세대입니다(`reloadTerrain` 은 0 으로 돌립니다). 가중치로 만든 것(레이어 필터 배치)이 낡았는지 견줍니다. */
        uint32 getSplatRevision() const { return _splatRevision; }
        /** @brief 이 지형이 그리는 자기 스플랫 텍스처의 캐시 경로(`cpu/terrain/...`)입니다. 가중치가 없거나 엔진 서비스가 없으면(CPU 테스트) 비어 있습니다. */
        const string& getCPUSplatPath() const { return _cpuSplatPath; }
        /** @brief 지금 높이를 `.heightfield` 형식으로 절대 경로에 씁니다(높이는 `_heightMin`..`_heightMax` 로 양자화). 높이장이 없으면 false 입니다. */
        [[nodiscard]] bool saveHeightfield( string_view filePath ) const;
        /** @brief 지금 CPU 가중치를 `.layerweights` 형식(`LayerWeightsData`)으로 절대 경로에 씁니다. 가중치가 없으면 false 입니다. */
        [[nodiscard]] bool saveSplat( string_view filePath ) const;

        /**
         * @brief 상태 스냅샷 계약(`ComponentStateStore`) — 마지막 `reloadTerrain` 뒤에 쓴 높이 · 스플랫을 씁니다. 쓴 것이 없으면 버전과 빈 표시만 씁니다.
         * @details 모듈 리로드와 백엔드 교체는 씬을 속성으로 되살리고 `onPostLoad` 가 디스크 에셋을 다시 읽으므로, 저장하지 않은 편집은 여기로만 넘어간다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief 마지막 `reloadTerrain` 뒤에 높이나 스플랫을 썼으면 true 입니다(`writeState` 가 싣는 것이 있다). 저장해도 다시 읽기 전까지는 true 로 남는다. */
        bool hasWrittenEdits() const { return _bHeightsWritten || _splatRevision > 0; }
        /** @brief 상태 스냅샷 계약 — 다시 읽은 지형에 `writeState` 의 편집을 되돌려 씁니다. 크기가 맞지 않거나 읽지 못하면 알리고 디스크 값으로 둡니다. */
        void restoreState( vector<uint8>&& bytes );

        /** @brief 청크 배치입니다. */
        const TerrainChunkLayout& getChunkLayout() const { return _layout; }
        /** @brief 청크 (x, z) 의 지금 LOD 입니다. 범위 밖이면 0 입니다. */
        uint32 getChunkLOD( uint32 chunkX, uint32 chunkZ ) const;
        /** @brief 청크 (x, z) 의 지금 메시 정점 수입니다(테스트). */
        uint32 getChunkVertexCount( uint32 chunkX, uint32 chunkZ ) const;

        /**
         * @brief @p manager 의 지형 가운데 월드 (x, z) 를 덮는 첫 지형입니다. 없으면 nullptr 입니다.
         * @details 포인터는 이 호출 안에서만 씁니다(빌린 것). 병렬 틱 안에서 불러도 됩니다(읽기만 합니다).
         */
        static TerrainComponent* findTerrainAt( const GameObjectManager& manager, float32 worldX, float32 worldZ );

        const string& getHeightfieldPath() const { return _heightfieldPath; }
        const string& getSplatTexturePath() const { return _splatTexturePath; }
        void          setHeightfieldPath( string_view path ) { _heightfieldPath = path; }
        void          setSplatTexturePath( string_view path ) { _splatTexturePath = path; }
        void          setDetailTexturePath( string_view path ) { _detailTexturePath = path; }
        void          setMaterialPath( string_view path ) { _materialPath = path; }
        void          setSize( const float2& size ) { _size = size; }
        void          setHeightRange( float32 heightMin, float32 heightMax );
        void          setChunkCells( uint32 chunkCells ) { _chunkCells = chunkCells; }
        void          setLODDistance( float32 lodDistance ) { _lodDistance = lodDistance; }
        void          setLayers( const vector<TerrainLayer>& listLayer ) { _listLayer = listLayer; }

    private:
        /** @brief 청크 하나입니다. 배치 하나와 현재 메시의 LOD, 이웃 LOD 를 담습니다. */
        struct Chunk
        {
            unique_ptr<MeshInstanceBatch> _batch;
            uint32                        _lod{ 0 };
            uint32                        _arrNeighborLOD[static_cast<uint32>( TerrainChunkSide::Count )]{};
            uint32                        _vertexCount{ 0 };
            bool                          _bMeshStale{ false }; ///< 높이를 썼고 메시는 아직이다(`_listStaleChunk` 에 있다)
        };

        /** @brief 청크 배치 모두를 더티로 표시합니다. 활성 상태가 바뀌었을 때 호출합니다. */
        void markChunksDirty();
        /** @brief 청크 배치를 레지스트리에서 빼고 놓습니다. */
        void releaseChunks();
        /**
         * @brief 다시 읽되, 같은 높이장 경로면 저장하지 않은 높이 · 스플랫 편집을 `writeState` 로 들고 건너 되돌려 씁니다. 경로가 바뀌었으면 버립니다.
         * @details 트랜스폼 · 프로퍼티 변경이 부른다 — 지형을 옮기거나 플레이를 시작하는 것이 칠한 것을 버리지 않는다(언리얼 랜드스케이프와 같다).
         */
        [[nodiscard]] bool reloadTerrainKeepingEdits();
        /** @brief 지금 LOD 로 청크 배치를 모두 만들어 등록합니다. */
        void createChunks();
        /** @brief 청크 하나의 메시를 현재 LOD 와 이웃 LOD 로 다시 만들어 배치에 적용합니다. */
        void rebuildChunkMesh( uint32 chunkIndex );
        /** @brief 청크 (x, z) 의 이웃 LOD 를 `TerrainChunkSide` 순서로 채웁니다(가장자리는 자기 LOD 를 넣습니다). */
        void fillNeighborLODs( uint32 chunkX, uint32 chunkZ, const vector<uint32>& listLOD, uint32 ( &outArrLOD )[4] ) const;
        /** @brief 머티리얼을 만들고 인스턴스에 레이어와 텍스처 값을 싣습니다. 엔진 서비스가 없으면(CPU 테스트) 건너뜁니다. */
        void acquireMaterial();
        /**
         * @brief 가중치 에셋(`.layerweights`)을 읽어 높이장에 주고, 엔진 서비스가 있으면 그 바이트로 자기 스플랫 텍스처를 만듭니다.
         * @return 경로가 비었거나 읽었으면 true 입니다. 정한 가중치를 읽지 못하면 오류를 남기고 false 입니다 — 레이어 0 으로 두지 않는다.
         */
        [[nodiscard]] bool loadSplatWeights();
        /** @brief 자기 스플랫 텍스처(`_cpuSplatPath`)를 캐시에 돌려줍니다. 없으면 아무것도 하지 않습니다. */
        void releaseCPUSplat();

    private:
        PROPERTY( Category = "Terrain", DisplayName = "Heightfield", AssetPath, Tooltip = "Imported .heightfield asset" )
        string _heightfieldPath;
        PROPERTY( Category = "Terrain", DisplayName = "Material", AssetPath, AssetType = "Material", Tooltip = "Terrain material (terrain.hlsl)" )
        string _materialPath;
        PROPERTY( Category = "Terrain", DisplayName = "Splat Map", AssetPath, Tooltip = "Layer weights asset (.layerweights, layers 0..3); read by client and dedicated server" )
        string _splatTexturePath;
        PROPERTY( Category = "Terrain", DisplayName = "Detail Map", AssetPath, AssetType = "Texture", Tooltip = "Tileable detail, one channel per layer" )
        string _detailTexturePath;
        PROPERTY( Category = "Terrain", DisplayName = "Layers", Tooltip = "Up to four layers, one per splat channel" )
        vector<TerrainLayer> _listLayer;
        PROPERTY( Category = "Terrain", DisplayName = "Size", Units = m, Tooltip = "World extent along x and z" )
        float2 _size;
        PROPERTY( Category = "Terrain", DisplayName = "Height Min", Units = m )
        float32 _heightMin;
        PROPERTY( Category = "Terrain", DisplayName = "Height Max", Units = m )
        float32 _heightMax;
        PROPERTY( Category = "Terrain", DisplayName = "Detail Tiling", Min = 0.0, Tooltip = "Detail texture repeats per meter" )
        float32 _detailTiling;
        PROPERTY( Category = "Terrain", DisplayName = "Cliff Start", Min = 0.0, Max = 1.0, Tooltip = "Normal Y below which the cliff layer takes over" )
        float32 _cliffStart;
        PROPERTY( Category = "Terrain", DisplayName = "Cliff Layer", Min = -1, Max = 3, Tooltip = "Layer forced on steep slopes (-1 = off)" )
        int32 _cliffLayer;
        PROPERTY( Category = "LOD", DisplayName = "Chunk Cells", Min = 2, Tooltip = "Cells per chunk side (power of two dividing resolution - 1)" )
        uint32 _chunkCells;
        PROPERTY( Category = "LOD", DisplayName = "LOD Distance", Min = 0.0, Units = m, Tooltip = "Distance of the first LOD step; each next step doubles it" )
        float32 _lodDistance;

        TerrainHeightfield  _heightfield;
        TerrainChunkLayout  _layout;
        vector<Chunk>       _listChunk;
        vector<uint32>      _listWantedLOD;       ///< updateLODs 의 스크래치(청크마다 고른 LOD)
        vector<uint32>      _listChunkGeneration; ///< 청크마다 높이를 마지막으로 바꾼 세대(`getChunkGenerations`)
        vector<uint32>      _listStaleChunk;      ///< 메시를 다시 만들 청크(쓴 순서, `rebuildStaleChunkMeshes`)
        EnvironmentMaterial _material;
        string              _cpuSplatPath;          ///< 가중치로 만든 자기 스플랫 텍스처(`getCPUSplatPath`)
        PrimitiveRegistry*  _pPrimitiveRegistry;    ///< 등록된 매니저의 레지스트리(구조 링크라 생포인터)
        uint32              _heightfieldGeneration; ///< `reloadTerrain` 과 `writeHeights` 마다 오른다(`getHeightfieldGeneration`)
        uint32              _splatRevision;         ///< `writeLayerWeights` 마다 오른다(`getSplatRevision`)
        atomic<bool>        _bOriginDirty;          ///< 오너가 움직여 지형 원점이 낡았다
        bool                _bHeightsWritten;       ///< 마지막 `reloadTerrain` 뒤에 `writeHeights` 가 성공했다(`writeState` 가 싣는다)
        string              _loadedHeightfieldPath; ///< 마지막으로 읽은 높이장 경로 — 같은 경로를 다시 읽을 때만 편집을 넘긴다
    };
} // namespace sw
