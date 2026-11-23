#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityViewComponent.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/StatefulComponentRegistrar.h"

namespace sw
{
    namespace
    {
        /** @brief 상태를 싣기 전에 세운 모습을 걷습니다 — 씬 섹션에 실리면 복원한 뒤 다시 세운 것과 겹친다. */
        const StatefulComponentRegistrar<CityViewComponent> s_viewRegistration{ "CityViewComponent" };

        struct CityViewComponentInternal
        {
            static constexpr float32 kHalfCell       = 0.5f;
            static constexpr float32 kBuildingFill   = 0.96f; ///< 건물 판이 차지하는 칸 비율(이웃 건물과 붙어 보이지 않게)
            static constexpr float32 kBuildingHeight = 1.0f;
            static constexpr float32 kRoadHeight     = 0.05f; ///< 도로 판 가운데의 높이

            static void destroyView( GameObjectManager& manager, GameObjectHandle handle )
            {
                GameObject* const pView = manager.resolveGameObject( handle );
                if ( pView != nullptr )
                    manager.destroyGameObject( pView );
            }

            template <typename TID>
            static void destroyViews( GameObjectManager& manager, SimulationViewSync<TID>& inoutViews )
            {
                for ( const typename SimulationViewSync<TID>::Slot& slot : inoutViews.getSlots() )
                {
                    destroyView( manager, slot._object );
                }
                inoutViews.clear();
            }
        };
    } // namespace

    CityViewComponent::CityViewComponent()
        : _tilePrefab{}
        , _fastForwardAction{}
        , _buildingColor{ 0.85f, 0.72f, 0.45f, 1.0f }
        , _roadColor{ 0.45f, 0.42f, 0.38f, 1.0f }
        , _director{}
        , _tintCache{}
        , _buildingViews{}
        , _roadViews{}
        , _shownRevision{ 0 }
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    CityViewComponent::~CityViewComponent() = default;

    void CityViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                  pOwner    = getOwner();
        const CityGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<CityGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "City.ViewWithoutGameMode", "City view on '%#': no CityGameModeComponent in front of it on the same object - the view does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<CityGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<CityGameModeComponent>( pDirector );
    }

    void CityViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );
        _bActionsValid = _fastForwardAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "City.ViewActionMissing", "City view: the fast forward action must be named - the view reads no input" );
    }

    void CityViewComponent::onEndPlay()
    {
        despawnViews();
        Component::onEndPlay();
    }

    void CityViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령 · 스폰은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( CityViewComponent& view, GameObjectManager& manager )
        { view.updateView( manager ); } );
    }

    void CityViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            CityViewComponentInternal::destroyViews( *pManager, _buildingViews );
            CityViewComponentInternal::destroyViews( *pManager, _roadViews );
        }
        _shownRevision = 0;
    }

    void CityViewComponent::updateView( GameObjectManager& manager )
    {
        CityGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        const bool                bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE;
        const InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput != nullptr )
        {
            const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
            (void)pDirector->submitFastForward( input.isActionDown( _fastForwardAction ) ); // 열린 판인지는 위에서 보았다
        }
        if ( _shownRevision == pDirector->getLayoutRevision() )
            return;
        syncTiles( manager, *pDirector );
        _shownRevision = pDirector->getLayoutRevision();
    }

    void CityViewComponent::syncTiles( GameObjectManager& manager, const CityGameModeComponent& director )
    {
        const CitySimulation&       city         = director.getCity();
        const vector<CityBuilding>& listBuilding = city.getBuildings();
        _buildingViews.beginCollect();
        for ( size_t buildingIndex = 0; buildingIndex < listBuilding.size(); ++buildingIndex )
        {
            const CityBuilding& building = listBuilding[buildingIndex];
            if ( building._bAlive != SW_FALSE )
                _buildingViews.show( buildingIndex, BuildingView{ building._pDef, building._origin } );
        }
        _buildingViews.endCollect();
        const GridTopology& topology = city.getTopology();
        for ( int32 tileIndex = 0; tileIndex < topology.getCellCount(); ++tileIndex )
        {
            const int2      cell  = topology.toCell( tileIndex );
            const CityTile* pTile = city.findTile( cell._x, cell._y );
            _roadViews.show( static_cast<size_t>( tileIndex ), pTile != nullptr && pTile->_bRoad != SW_FALSE ? SW_TRUE : SW_FALSE );
        }
        const auto destroy = [&manager]( GameObjectHandle& inoutHandle )
        {
            CityViewComponentInternal::destroyView( manager, inoutHandle );
            inoutHandle = GameObjectHandle{};
        };
        _buildingViews.flush( destroy, [this, &manager]( size_t, const BuildingView& view )
        {
            GameObjectHandle     handle;
            MeshComponent* const pMesh = spawnTile( manager, "CityBuilding", handle );
            if ( pMesh != nullptr )
            {
                const float32 size = static_cast<float32>( view._pDef->_size );
                const float32 half = size * CityViewComponentInternal::kHalfCell;
                pMesh->setLocalPosition( float3{ static_cast<float32>( view._origin._x ) + half, CityViewComponentInternal::kBuildingHeight * 0.5f, static_cast<float32>( view._origin._y ) + half } );
                pMesh->setLocalScale( float3{ size * CityViewComponentInternal::kBuildingFill, CityViewComponentInternal::kBuildingHeight, size * CityViewComponentInternal::kBuildingFill } );
                _tintCache.apply( *pMesh, _buildingColor );
            }
            return handle;
        } );
        _roadViews.flush( destroy, [this, &manager, &topology]( size_t tileIndex, uint8 )
        {
            GameObjectHandle     handle;
            MeshComponent* const pMesh = spawnTile( manager, "CityRoad", handle );
            if ( pMesh != nullptr )
            {
                const int2 cell = topology.toCell( static_cast<int32>( tileIndex ) );
                pMesh->setLocalPosition( float3{ static_cast<float32>( cell._x ) + CityViewComponentInternal::kHalfCell, CityViewComponentInternal::kRoadHeight,
                                                 static_cast<float32>( cell._y ) + CityViewComponentInternal::kHalfCell } );
                _tintCache.apply( *pMesh, _roadColor );
            }
            return handle;
        } );
    }

    MeshComponent* CityViewComponent::spawnTile( GameObjectManager& manager, const utf8* pName, GameObjectHandle& outHandle ) const
    {
        AssetManager* const pAssets = game::findSubsystem<AssetManager>();
        GameObject* const   pView   = pAssets != nullptr && _tilePrefab.empty() == false ? pAssets->getAssetCache<PrefabCache>().spawn( &manager, _tilePrefab, pName ) : nullptr;
        if ( pView == nullptr )
            return nullptr;
        outHandle = pView->getHandle();
        return pView->getComponent<MeshComponent>();
    }
} // namespace sw
