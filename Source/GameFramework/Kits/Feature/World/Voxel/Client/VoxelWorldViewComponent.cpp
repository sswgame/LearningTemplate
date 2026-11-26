#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"
#include "GameFramework/Kits/Feature/World/Voxel/Client/VoxelChunkComponent.h"
#include "GameFramework/Kits/Feature/World/Voxel/Component/VoxelGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    class GameObjectManager;
    class VoxelChunkComponent;
    class VoxelGameModeComponent;

    /**
     * @class VoxelWorldViewComponent
     * @brief 디렉터와 같은 오브젝트에 붙이는 청크 뷰의 주인입니다. 청크 프리팹의 스폰 · 정리, 다시 메싱 차례, 블록 머티리얼의 아틀라스 칸 검사를 맡습니다.
     * @details 스폰한 청크는 상태에서 다시 세우는 파생이라 상태에 들지 않는다(`SpawnLifetime::Derived`).
     *          블록 머티리얼의 아틀라스 칸이 카탈로그와 같다고 견주기 전 · 달랐으면 메싱을 요청하지 않는다(청크 프리팹의 메시는 숨겨져 있다 — 검사 없이 그리는 때가 없다).
     */
    REFLECT( Category = "Voxel", DisplayName = "Voxel World View",
             Tooltip = "Client side of a voxel world: spawns and rebuilds the chunk meshes around the body of the Voxel Game Mode on the same object" )
    class VoxelWorldViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        VoxelWorldViewComponent();
        virtual ~VoxelWorldViewComponent() override;

        /** @brief 세운 청크를 걷습니다. */
        void onEndPlay() override;

    private:
        /** @brief 세운 모습을 모두 지웁니다(플레이 끝 · 다시 세울 때). 모습은 파생이라 상태를 실을 때 걷지 않는다(`SpawnLifetime::Derived`). */
        void despawnViews();
        /** @brief 세운 청크 하나입니다. */
        struct ChunkView
        {
            GameObjectHandle _object{};
            optional<uint32> _builtRevision{}; ///< 마지막으로 메싱을 요청한 청크 판 번호. 아직 요청하지 않았으면 비어 있다
        };

        /** @brief 틱 뒤 게임 스레드: 그릴 청크를 맞추고 낡은 청크에 다시 메싱을 요청합니다. */
        void updateViews( GameObjectManager& manager, const VoxelGameModeComponent& director );
        /** @brief 그릴 범위를 벗어났거나 내려간 청크의 뷰를 지웁니다. */
        void removeHiddenViews( GameObjectManager& manager, const VoxelGameModeComponent& director );
        /** @brief 청크 하나의 뷰를 세웁니다. 프리팹이 틀리면 한 번 오류를 남기고 더 세우지 않습니다. */
        void spawnView( GameObjectManager& manager, const VoxelGameModeComponent& director, int32 chunkX, int32 chunkZ );
        /** @brief 청크 뷰 하나의 블록 · 물 머티리얼이 준비됐는지 보고, 준비됐으면 아틀라스 칸을 카탈로그와 월드당 한 번 견줍니다. 다르면 오류 한 줄이 남습니다. */
        void updateAtlasGate( const GameObjectManager& manager, const VoxelGameModeComponent& director );
        /** @brief 세운 청크의 컴포넌트입니다. 오브젝트가 사라졌으면 nullptr 입니다. */
        static VoxelChunkComponent* findChunk( GameObjectManager& manager, const ChunkView& view );

    private:
        PROPERTY( Required, Category = "Prefabs", DisplayName = "Chunk Prefab", Tooltip = "Prefab with a VoxelChunkComponent and its opaque and water meshes" )
        AssetRef<PrefabAsset> _chunkPrefab;
        PROPERTY( Category = "World", DisplayName = "Chunk Builds Per Frame", Tooltip = "Changed chunks handed out for meshing each frame, nearest to the body first", Min = 1 )
        int32 _chunkBuildsPerFrame;

        SW_TRANSIENT unordered_map<uint64, ChunkView> _mapChunkView;       ///< 세운 청크. 키는 `VoxelWorld::makeChunkKey` 이다
        SW_TRANSIENT VoxelAtlasGridGate               _atlasGate;          ///< 블록 머티리얼 아틀라스 칸 검사(월드당 한 번). 카탈로그가 정본이다
        SW_TRANSIENT uint8                            _bPrefabInvalid : 1; ///< 청크 프리팹이 틀렸다고 이미 알렸다
        [[maybe_unused]] uint8                        _reserved       : 7;
    };

    namespace
    {

        struct VoxelWorldViewComponentInternal
        {
            /** @brief 청크가 그릴 범위의 가운데에서 떨어진 체비쇼프 거리입니다. */
            static int32 computeDistance( const VoxelGameModeComponent& director, int32 chunkX, int32 chunkZ )
            {
                return MathUtil::max( MathUtil::abs( chunkX - director.getCenterChunkX() ), MathUtil::abs( chunkZ - director.getCenterChunkZ() ) );
            }
        };
    } // namespace

    VoxelWorldViewComponent::VoxelWorldViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&VoxelWorldViewComponent::updateViews>() )
        , _chunkPrefab{}
        , _chunkBuildsPerFrame{ 4 }
        , _mapChunkView{}
        , _atlasGate{}
        , _bPrefabInvalid{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    VoxelWorldViewComponent::~VoxelWorldViewComponent() = default;

    void VoxelWorldViewComponent::onEndPlay()
    {
        despawnViews();
        GameModeViewComponent::onEndPlay();
    }

    void VoxelWorldViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            for ( const auto& entry : _mapChunkView )
            {
                GameObject* const pObject = pManager->resolveGameObject( entry.second._object );
                if ( pObject != nullptr )
                    pManager->destroyGameObject( pObject );
            }
        }
        _mapChunkView.clear();
    }

    void VoxelWorldViewComponent::updateViews( GameObjectManager& manager, const VoxelGameModeComponent& director )
    {
        removeHiddenViews( manager, director );
        if ( _atlasGate.isPending() )
            updateAtlasGate( manager, director );

        // 몸에서 가까운 청크부터 본다 — 블록을 부순 곳이 먼저 다시 메싱된다. 처음 짓는 청크는 한도에 들지 않는다.
        const VoxelWorld& world        = director.getWorld();
        const int32       centerX      = director.getCenterChunkX();
        const int32       centerZ      = director.getCenterChunkZ();
        int32             rebuiltCount = 0;
        for ( int32 ring = 0; ring <= director.getViewChunkRadius(); ++ring )
        {
            for ( int32 chunkZ = centerZ - ring; chunkZ <= centerZ + ring; ++chunkZ )
            {
                for ( int32 chunkX = centerX - ring; chunkX <= centerX + ring; ++chunkX )
                {
                    const bool              bOnRing = VoxelWorldViewComponentInternal::computeDistance( director, chunkX, chunkZ ) == ring;
                    const VoxelChunk* const pChunk  = bOnRing ? world.findChunk( chunkX, chunkZ ) : nullptr;
                    if ( pChunk == nullptr )
                        continue;
                    const uint64 key = VoxelWorld::makeChunkKey( chunkX, chunkZ );
                    if ( _mapChunkView.contains( key ) == false )
                        spawnView( manager, director, chunkX, chunkZ );
                    const auto viewIt = _mapChunkView.find( key );
                    if ( viewIt == _mapChunkView.end() || _atlasGate.canDraw() == false )
                        continue;
                    ChunkView& view        = viewIt->second;
                    const bool bFirstBuild = view._builtRevision.has_value() == false;
                    const bool bUpToDate   = bFirstBuild == false && *view._builtRevision == pChunk->_revision;
                    if ( bUpToDate || ( bFirstBuild == false && rebuiltCount >= _chunkBuildsPerFrame ) )
                        continue;
                    view._builtRevision                   = pChunk->_revision;
                    VoxelChunkComponent* const pViewChunk = findChunk( manager, view );
                    if ( pViewChunk != nullptr )
                        pViewChunk->requestRebuild();
                    if ( bFirstBuild == false )
                        ++rebuiltCount;
                }
            }
        }
    }

    void VoxelWorldViewComponent::removeHiddenViews( GameObjectManager& manager, const VoxelGameModeComponent& director )
    {
        // 지우면 사전이 원소를 옮기므로 키를 먼저 모은다. 사라질 청크가 없는 프레임은 할당이 없다.
        vector<uint64> listGoneKey;
        for ( const auto& entry : _mapChunkView )
        {
            const int32 chunkX = VoxelWorld::toChunkX( entry.first );
            const int32 chunkZ = VoxelWorld::toChunkZ( entry.first );
            const bool  bNear  = VoxelWorldViewComponentInternal::computeDistance( director, chunkX, chunkZ ) <= director.getViewChunkRadius();
            if ( bNear == false || director.getWorld().isChunkResident( chunkX, chunkZ ) == false )
                listGoneKey.push_back( entry.first );
        }
        for ( const uint64 key : listGoneKey )
        {
            const auto        viewIt  = _mapChunkView.find( key );
            GameObject* const pObject = manager.resolveGameObject( viewIt->second._object );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
            _mapChunkView.erase( viewIt );
        }
    }

    void VoxelWorldViewComponent::spawnView( GameObjectManager& manager, const VoxelGameModeComponent& director, int32 chunkX, int32 chunkZ )
    {
        AssetManager* const pAssets = game::findSubsystem<AssetManager>();
        if ( _bPrefabInvalid == SW_TRUE || pAssets == nullptr )
            return;
        GameObject* const          pObject = pAssets->getAssetCache<PrefabCache>().spawn( &manager, _chunkPrefab.path(), SpawnLifetime::Derived, "VoxelChunk" );
        VoxelChunkComponent* const pChunk  = pObject != nullptr ? pObject->getComponent<VoxelChunkComponent>() : nullptr;
        MeshComponent* const       pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        if ( pChunk == nullptr || pMesh == nullptr )
        {
            _bPrefabInvalid = SW_TRUE;
            SW_LOG_ERROR_ID( "VoxelWorldView.ChunkPrefabInvalid", "Voxel world view: chunk prefab '%#' did not spawn a VoxelChunkComponent with a MeshComponent - no chunk is drawn", _chunkPrefab.path() );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
            return;
        }
        pMesh->setLocalPosition( manager.getWorldOrigin().toLocal( float3{ static_cast<float32>( chunkX * kVoxelChunkSize ), 0.0f, static_cast<float32>( chunkZ * kVoxelChunkSize ) } ) );
        pChunk->assignChunk( director.getOwner()->getHandle(), chunkX, chunkZ );
        _mapChunkView[VoxelWorld::makeChunkKey( chunkX, chunkZ )]._object = pObject->getHandle();
    }

    void VoxelWorldViewComponent::updateAtlasGate( const GameObjectManager& manager, const VoxelGameModeComponent& director )
    {
        if ( _mapChunkView.empty() )
            return;
        const GameObject* const          pObject = manager.resolveGameObject( _mapChunkView.begin()->second._object );
        const VoxelChunkComponent* const pChunk  = pObject != nullptr ? pObject->getComponent<VoxelChunkComponent>() : nullptr;
        if ( pChunk == nullptr )
            return;
        MeshComponent* pOpaque = nullptr;
        MeshComponent* pWater  = nullptr;
        pChunk->findMeshes( pOpaque, pWater );
        const Material* const pOpaqueMaterial = pOpaque != nullptr ? pOpaque->getMaterial() : nullptr;
        const Material* const pWaterMaterial  = pWater != nullptr ? pWater->getMaterial() : nullptr;
        if ( pOpaqueMaterial == nullptr || pWaterMaterial == nullptr )
            return; // 아직 준비되지 않았다 — 다음 프레임에 다시 본다(그동안 청크는 그리지 않는다)
        // 프로퍼티는 게임 스레드 초기화(`Ready`)가 끝나야 채워진다 — 그 전에는 견줄 값이 없으니 다음 프레임에 다시 본다.
        const bool bMaterialsReady = pOpaqueMaterial->getInitState() == MaterialInitState::Ready && pWaterMaterial->getInitState() == MaterialInitState::Ready;
        (void)_atlasGate.update( *director.getWorld().getCatalog(), bMaterialsReady, static_cast<const float4*>( pOpaqueMaterial->getParameterData( "_atlasGrid" ) ),
                                 static_cast<const float4*>( pWaterMaterial->getParameterData( "_atlasGrid" ) ), pOpaque->getMaterialPath().c_str() ); // 결과는 `canDraw` 로 읽는다
    }

    VoxelChunkComponent* VoxelWorldViewComponent::findChunk( GameObjectManager& manager, const ChunkView& view )
    {
        GameObject* const pObject = manager.resolveGameObject( view._object );
        return pObject != nullptr ? pObject->getComponent<VoxelChunkComponent>() : nullptr;
    }
} // namespace sw

#include "VoxelWorldViewComponent.cpp.gen.inl"
