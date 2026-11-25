#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Client/BattleRoyaleViewComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Client/BattleRoyaleHUDViewModel.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/Component/BattleRoyaleGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {

        struct BattleRoyaleViewComponentInternal
        {
            static constexpr float32 kDownedHeightRatio = 0.35f; ///< 기절한 사람의 높이 비율
            static constexpr float32 kDiscHeight        = 0.02f; ///< 구역 원판 두께(m)
            static constexpr float32 kNextDiscLift      = 0.02f; ///< 다음 구역 원판을 지금 원판 위로 띄우는 높이(m)
            static constexpr float32 kItemLift          = 0.4f;  ///< 무더기를 띄우는 높이(m)

            static void placeDisc( GameObjectManager& manager, GameObjectHandle disc, const AbsolutePosition& center, float32 radius )
            {
                GameObject* const     pObject = disc.isValid() ? manager.resolveGameObject( disc ) : nullptr;
                SceneComponent* const pRoot   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
                if ( pRoot == nullptr )
                    return;
                pRoot->setLocalScale( float3( radius * 2.0f, kDiscHeight, radius * 2.0f ) );
                pRoot->setWorldPosition( manager.getWorldOrigin().toLocal( center ) );
            }
        };
    } // namespace

    BattleRoyaleViewComponent::BattleRoyaleViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&BattleRoyaleViewComponent::updateView, &BattleRoyaleViewComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _fireAction{}
        , _useAction{}
        , _listPersonBody{}
        , _zoneDisc{}
        , _nextZoneDisc{}
        , _personHeight{ 1.8f }
        , _personWidth{ 0.8f }
        , _itemPrefab{}
        , _itemPoolSize{ 0 }
        , _hudViewModel{}
        , _itemPool{ string{}, 0, SpawnLifetime::Derived }
        , _shownItemRevision{ 0 }
    {
    }

    BattleRoyaleViewComponent::~BattleRoyaleViewComponent() = default;

    void BattleRoyaleViewComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _itemPool = PrefabPool( _itemPrefab.path(), _itemPoolSize, SpawnLifetime::Derived );
    }

    void BattleRoyaleViewComponent::onEndPlay()
    {
        despawnViews();
        GameModeViewComponent::onEndPlay();
    }

    void BattleRoyaleViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _itemPool.destroyAll( *pManager );
        _shownItemRevision = 0;
    }

    void BattleRoyaleViewComponent::updateView( GameObjectManager& manager, BattleRoyaleGameModeComponent& director )
    {
        dispatchInput( manager, director );
        updatePeople( manager, director );
        updateZone( manager, director );
        syncGroundItems( manager, director );
        updateHUD( director );
    }

    void BattleRoyaleViewComponent::submitInput( const GameModeInput& input, BattleRoyaleGameModeComponent& director ) const
    {
        // 판이 끝났으면 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
        (void)director.submitPlayerInput( input.getVector2D( _moveAction ), input.isActionDown( _fireAction ), input.isActionDown( _useAction ), input.wasActionTriggered( _useAction ) );
    }

    void BattleRoyaleViewComponent::updatePeople( GameObjectManager& manager, const BattleRoyaleGameModeComponent& director ) const
    {
        for ( size_t index = 0; index < _listPersonBody.size(); ++index )
        {
            const int32                     person  = static_cast<int32>( index );
            GameObject* const               pObject = _listPersonBody[index].isValid() ? manager.resolveGameObject( _listPersonBody[index] ) : nullptr;
            MeshComponent* const            pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
            const BattleRoyalePlayer* const pPlayer = director.getMatch().findPlayer( person );
            if ( pMesh == nullptr )
                continue;
            const bool bShown = pPlayer != nullptr && pPlayer->isDead() == false;
            if ( pMesh->isVisible() != bShown ) // 같은 값이어도 렌더 상태를 다시 짓는다
                pMesh->setVisible( bShown );
            if ( bShown == false )
                continue;
            const float32 height = pPlayer->isDowned() ? _personHeight * BattleRoyaleViewComponentInternal::kDownedHeightRatio : _personHeight;
            pMesh->setLocalScale( float3( _personWidth, height, _personWidth ) );
            pMesh->setWorldPosition( manager.getWorldOrigin().toLocal( director.getPersonPosition( person ) + float3( 0.0f, height * 0.5f, 0.0f ) ) );
        }
    }

    void BattleRoyaleViewComponent::updateZone( GameObjectManager& manager, const BattleRoyaleGameModeComponent& director ) const
    {
        using Internal               = BattleRoyaleViewComponentInternal;
        const BattleRoyaleZone& zone = director.getMatch().getZone();
        Internal::placeDisc( manager, _zoneDisc, director.toAbsolute( zone.getCenter() ), zone.getRadius() );
        Internal::placeDisc( manager, _nextZoneDisc, director.toAbsolute( zone.getNextCenter() ) + float3( 0.0f, Internal::kNextDiscLift, 0.0f ), zone.getNextRadius() );
    }

    void BattleRoyaleViewComponent::syncGroundItems( GameObjectManager& manager, const BattleRoyaleGameModeComponent& director )
    {
        if ( _shownItemRevision == director.getGroundItemRevision() )
            return;
        _itemPool.releaseBusyIf( manager, []( const GameObject& object )
        {
            (void)object;
            return true;
        } );
        for ( const BattleRoyaleGroundItem& item : director.getGroundItems() )
        {
            GameObject* const     pObject = _itemPool.acquire( manager, "BattleRoyaleItem" );
            SceneComponent* const pRoot   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pRoot != nullptr )
                pRoot->setWorldPosition( manager.getWorldOrigin().toLocal( director.toAbsolute( item._position ) + float3( 0.0f, BattleRoyaleViewComponentInternal::kItemLift, 0.0f ) ) );
        }
        _shownItemRevision = director.getGroundItemRevision();
    }

    void BattleRoyaleViewComponent::updateHUD( const BattleRoyaleGameModeComponent& director )
    {
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<BattleRoyaleHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>();
        GameObject* const          pOwner  = getOwner();
        if ( pBinder != nullptr && pOwner != nullptr )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const bool bEnded = director.getMatchPhase() == BattleRoyalePhase::Ended;
        _hudViewModel->setHealth( director.computePlayerHealthRatio() );
        _hudViewModel->setRemaining( director.getRemaining() );
        const ItemDef* const pWeaponItem = director.findPlayerWeaponItem();
        _hudViewModel->setWeapon( pWeaponItem != nullptr ? string_view( pWeaponItem->_name ) : string_view{}, director.getPlayerMagazine(), director.getPlayerSpareAmmo() );
        _hudViewModel->setZoneSeconds( director.getMatch().getZone().getStageRemaining() );
        _hudViewModel->setHints( director.getMatchPhase() == BattleRoyalePhase::Drop && director.getPlayerDropStage() == 0, director.isPlayerDowned() && bEnded == false );
        _hudViewModel->setResult( director.hasWon(), bEnded && director.hasWon() == false );
    }
} // namespace sw
