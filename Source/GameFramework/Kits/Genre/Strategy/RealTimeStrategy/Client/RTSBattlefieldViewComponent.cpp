#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSBattlefieldViewComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/XorShift32.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSUnitViewComponent.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/RTSGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        /** @brief 유닛 · 절벽은 월드에서 다시 세우는 모습이라 상태를 저장하기 전에 걷는다 — 걷지 않으면 복원한 씬에서 다시 세운 것과 겹친다. */
        const StatefulComponentRegistrar<RTSBattlefieldViewComponent> s_viewRegistration{ "RTSBattlefieldViewComponent" };

        struct RTSBattlefieldViewComponentInternal
        {
            static constexpr int32 kUnitLookCategory = 5; ///< 0번 팀, 1번 팀, 주인 없음, 광물, 가스.

            /** @brief 모습 종류입니다. 팀(0번, 1번, 그 밖)이거나 자원(광물, 가스)입니다. */
            static int32 computeLookCategory( const RTSUnit& unit )
            {
                if ( unit.isResource() )
                    return unit._pDef->_resourceType == RTSResourceKind::Gas ? 4 : 3;
                if ( unit._owner == 0 || unit._owner == 1 )
                    return unit._owner;
                return 2;
            }

            /**
             * @brief 해당 모습의 모델에 곱할 색입니다. 키트의 흰색과 회색 몸체가 팀 색(파랑, 빨강)으로, 자원은 광물이 하늘색, 가스가 초록색으로 보입니다. 선택하면 흰색 쪽으로 절반 섞습니다.
             * @details 건물과 일꾼을 어둡게, 밝게 나누던 일은 모델이 대신하므로 팀 색을 그대로 곱한다(곱하면 이미 어두워진다).
             */
            static float4 computeLookColor( const float4& categoryColor, bool bSelected )
            {
                if ( bSelected == false )
                    return categoryColor;
                return float4{ 0.5f + categoryColor._x * 0.5f, 0.5f + categoryColor._y * 0.5f, 0.5f + categoryColor._z * 0.5f, 1.0f };
            }
        };
    } // namespace

    RTSBattlefieldViewComponent::RTSBattlefieldViewComponent()
        : _unitModelPath{}
        , _listCliffPrefab{}
        , _unitPrefab{}
        , _unitPool{}
        , _playerZeroColor{ 0.25f, 0.45f, 0.95f, 1.0f }
        , _playerOneColor{ 0.9f, 0.25f, 0.2f, 1.0f }
        , _neutralColor{ 0.7f, 0.7f, 0.7f, 1.0f }
        , _mineralColor{ 0.35f, 0.85f, 1.0f, 1.0f }
        , _gasColor{ 0.3f, 0.8f, 0.4f, 1.0f }
        , _director{}
        , _pUnitModelCatalog{}
        , _shownUnitModels{}
        , _unitModels{}
        , _unitViewPool{}
        , _unitViews{}
        , _listCliff{}
        , _tintCache{}
        , _listUnitLook{}
        , _bModelsChecked{ SW_FALSE }
        , _bModelsValid{ SW_FALSE }
        , _bCliffsSpawned{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    RTSBattlefieldViewComponent::~RTSBattlefieldViewComponent() = default;

    void RTSBattlefieldViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        const GameObject* const pOwner = getOwner();
        _director                      = TComponentHandle<RTSGameModeComponent>( pOwner != nullptr ? pOwner->getComponent<RTSGameModeComponent>() : nullptr );
        if ( _director.isValid() == false )
            SW_LOG_ERROR_ID( "RTSBattlefieldView.NoGameMode", "RTS battlefield view on '%#': no RTS game mode in front of it on the same object - nothing is drawn", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
    }

    void RTSBattlefieldViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bModelsChecked = SW_FALSE;
    }

    void RTSBattlefieldViewComponent::onEndPlay()
    {
        despawnViews();
        _unitModels.clear();
        Component::onEndPlay();
    }

    void RTSBattlefieldViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        context.deferPostTickOnComponent( *this, []( RTSBattlefieldViewComponent& view, GameObjectManager& manager )
        { view.updateViews( manager ); } );
    }

    void RTSBattlefieldViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            if ( _unitViewPool.has_value() )
                _unitViewPool->destroyAll( *pManager );
            for ( const GameObjectHandle handle : _listCliff )
            {
                GameObject* const pCliff = pManager->resolveGameObject( handle );
                if ( pCliff != nullptr )
                    pManager->destroyGameObject( pCliff );
            }
        }
        _unitViewPool.reset();
        _unitViews.clear();
        _listCliff.clear();
        _bCliffsSpawned = SW_FALSE;
    }

    const shared_ptr<MaterialInstance>& RTSBattlefieldViewComponent::findUnitLook( const RTSUnit& unit, bool bSelected ) const
    {
        static const shared_ptr<MaterialInstance> kNoLook{};
        const int32                               lookIndex = RTSBattlefieldViewComponentInternal::computeLookCategory( unit ) * 2 + ( bSelected ? 1 : 0 );
        if ( lookIndex >= static_cast<int32>( _listUnitLook.size() ) )
            return kNoLook;
        // 유닛 뷰가 워커에서 부른다. 컨테이너 인덱스 대신 포인터로 읽는다.
        return _listUnitLook.data()[lookIndex];
    }

    void RTSBattlefieldViewComponent::updateViews( GameObjectManager& manager )
    {
        const RTSGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        if ( _bModelsChecked == SW_FALSE )
        {
            _bModelsChecked = SW_TRUE;
            _bModelsValid   = acquireUnitModels( *pDirector ) ? SW_TRUE : SW_FALSE;
        }
        if ( _bModelsValid == SW_FALSE )
            return;
        if ( _pUnitModelCatalog.get() != _shownUnitModels.get() )
        {
            despawnViews();
            _shownUnitModels = _pUnitModelCatalog.snapshot();
        }
        const RTSWorld& world = pDirector->getWorld();
        if ( _bCliffsSpawned == SW_FALSE )
            spawnCliffs( manager, world );

        // 보일 유닛은 사람 쪽 화면이 안 보이는 적을 그리지 않게 정한다(자원은 항상 보인다). 슬롯의 유닛이 바뀌었거나 이번에 못 본 슬롯(죽음, 안개)만 쌓인다.
        const bool  bFog        = pDirector->isLocalPlayerHuman();
        const int32 localPlayer = pDirector->getLocalPlayer();
        _unitViews.beginCollect();
        world.forEachUnit( [this, &world, bFog, localPlayer]( const RTSUnit& unit )
        {
            const bool bHiddenByFog = bFog && unit._owner != localPlayer && unit._owner != RTSWorld::kNoOwner && world.isVisibleTo( localPlayer, unit._id ) == false;
            if ( bHiddenByFog || _shownUnitModels->find( unit._pDef->_id ) == nullptr ) // 모든 유닛에 모델 줄이 있음은 처음에 보았다 — 없는 것은 다시 읽힌 카탈로그의 새 유닛이다
                return;
            _unitViews.show( static_cast<size_t>( unit._id.index() ), unit._id );
        } );
        _unitViews.endCollect();
        _unitViews.flush( [this, &manager]( GameObjectHandle& inoutHandle )
        {
            if ( _unitViewPool.has_value() )
                _unitViewPool->release( manager, inoutHandle ); // 죽음 · 안개로 빠진 유닛 뷰는 풀에 돌려준다
            inoutHandle = GameObjectHandle{};
        }, [this, &manager, &world]( size_t, RTSUnitID unitID )
        { return spawnUnit( manager, world, unitID ); } );
    }

    bool RTSBattlefieldViewComponent::acquireUnitModels( const RTSGameModeComponent& director )
    {
        static constexpr const utf8* kOwner = "RTSBattlefieldViewComponent";
        _pUnitModelCatalog                  = _unitModelPath.acquire();
        if ( _pUnitModelCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "RTSBattlefieldView.UnitModelsMissing", "%#: the unit model table '%#' is not set or could not be read - nothing is drawn", kOwner, _unitModelPath.c_str() );
            return false;
        }
        bool bValid = true;
        // 모델 표와 유닛 카탈로그는 서로 같은 id 를 가져야 한다. 모델이 없는 유닛은 조용히 그려지지 않으므로 처음에 막는다.
        for ( const RTSUnitDef& def : director.findCatalog()->getAll() ) // 열린 판은 카탈로그를 쥐고 있다
        {
            if ( _pUnitModelCatalog->find( def._id ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "RTSBattlefieldView.UnitNoRow", "%#: unit '%#' of the game mode's unit catalog has no row in %#", kOwner, def._id.c_str(), _unitModelPath.c_str() );
            bValid = false;
        }
        for ( const RuleViewDef& model : _pUnitModelCatalog->getAll() )
        {
            if ( director.findCatalog()->find( model._id ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "RTSBattlefieldView.ModelRowNot", "%#: model row '%#' of %# is not in the game mode's unit catalog", kOwner, model._id.c_str(), _unitModelPath.c_str() );
            bValid = false;
        }
        if ( bValid == false )
            return false;
        // 처음 짓는 건물 · 유닛의 모델을 게임 스레드가 플레이 중에 디스크에서 읽지 않게 워커로 미리 읽는다(느린 디스크의 히치).
        _unitModels.clear();
        for ( const string& path : _pUnitModelCatalog->makeAllMeshPaths() )
        {
            _unitModels.add( MeshCache::request( path, MeshCPUAccess::ReleaseAfterUpload ) );
        }
        return true;
    }

    void RTSBattlefieldViewComponent::spawnCliffs( GameObjectManager& manager, const RTSWorld& world )
    {
        _bCliffsSpawned             = SW_TRUE;
        AssetManager* const pAssets = game::findSubsystem<AssetManager>();
        // 절벽은 타일마다 바위 하나다(바위는 늘이면 모양이 망가져 줄을 상자 하나로 잇지 않는다). 고르기 · 요는 타일 자리의 해시다.
        const int32 width  = world.getGrid().getWidth();
        const int32 height = world.getGrid().getHeight();
        for ( int32 y = 0; y < height; ++y )
        {
            for ( int32 x = 0; x < width; ++x )
            {
                if ( world.isTerrainBlocked( x, y ) == false )
                    continue;
                if ( _listCliffPrefab.empty() || pAssets == nullptr )
                {
                    SW_LOG_ERROR_ID( "RTSBattlefieldView.CliffPrefabsEmpty", "RTSBattlefieldViewComponent: the map has blocked terrain but _listCliffPrefab is empty - the scene must give at least one cliff prefab" );
                    return;
                }
                const uint32      hash   = XorShift32::next( XorShift32::next( static_cast<uint32>( y * width + x ) + 1u ) );
                const string&     prefab = _listCliffPrefab[hash % static_cast<uint32>( _listCliffPrefab.size() )].path();
                GameObject* const pCliff = pAssets->getAssetCache<PrefabCache>().spawn( &manager, prefab, "RTSCliff" );
                SceneComponent*   pRoot  = pCliff != nullptr ? pCliff->getComponent<SceneComponent>() : nullptr;
                if ( pCliff != nullptr )
                    _listCliff.push_back( pCliff->getHandle() );
                if ( pRoot == nullptr )
                    continue;
                pRoot->setLocalPosition( manager.getWorldOrigin().toLocal( float3{ static_cast<float32>( x ) + 0.5f, 0.0f, static_cast<float32>( y ) + 0.5f } ) ); // RTS 월드는 절대 자리다
                pRoot->setLocalRotation( float3{ 0.0f, XorShift32::toUnit24( hash ) * MathUtil::kTwoPi, 0.0f } );
            }
        }
    }

    GameObjectHandle RTSBattlefieldViewComponent::spawnUnit( GameObjectManager& manager, const RTSWorld& world, RTSUnitID unitID )
    {
        const RTSUnit* const     pUnit  = world.findUnit( unitID );
        const RuleViewDef* const pModel = pUnit != nullptr ? _shownUnitModels->find( pUnit->_pDef->_id ) : nullptr;
        if ( pModel == nullptr )
            return GameObjectHandle{};
        if ( _unitViewPool.has_value() == false )
            _unitViewPool.emplace( string( _unitPrefab.path() ), _unitPool._maxIdleCount );
        GameObject* const pObject = _unitViewPool->acquire( manager, "RTSUnit" );
        if ( pObject == nullptr )
            return GameObjectHandle{};
        MeshComponent* const pMesh = pObject->getComponent<MeshComponent>();
        if ( pMesh != nullptr )
        {
            // 모델은 유닛 종류마다 정해져 바뀌지 않는다. 그래서 여기(게임 스레드)에서 지정한다. 색, 위치, 크기, 방향은 유닛 뷰가 맞춘다.
            if ( _listUnitLook.empty() && pMesh->getMaterial() != nullptr ) // 머티리얼 없이 만들면 빈 모습이 남아 다시 만들지 않는다
                prepareUnitLooks( pMesh->getMaterial() );
            pMesh->setMeshID( pModel->_mesh.path() );
        }
        RTSUnitViewComponent* const pView = pObject->getComponent<RTSUnitViewComponent>();
        if ( pView != nullptr )
            pView->assignUnit( getOwner()->getHandle(), unitID, pModel->_width );
        return pObject->getHandle();
    }

    void RTSBattlefieldViewComponent::prepareUnitLooks( Material* pMaterial )
    {
        using Internal = RTSBattlefieldViewComponentInternal;
        _listUnitLook.assign( static_cast<size_t>( Internal::kUnitLookCategory * 2 ), nullptr );
        const float4 arrCategoryColor[Internal::kUnitLookCategory] = { _playerZeroColor, _playerOneColor, _neutralColor, _mineralColor, _gasColor };
        for ( int32 category = 0; category < Internal::kUnitLookCategory; ++category )
        {
            for ( int32 selected = 0; selected < 2; ++selected )
            {
                _listUnitLook[static_cast<size_t>( category * 2 + selected )] = _tintCache.acquire( pMaterial, Internal::computeLookColor( arrCategoryColor[category], selected == 1 ) );
            }
        }
    }
} // namespace sw
