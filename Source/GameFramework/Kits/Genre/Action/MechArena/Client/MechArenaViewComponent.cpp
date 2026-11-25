#include "pch.h"

#include "GameFramework/Kits/Genre/Action/MechArena/Client/MechArenaViewComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Action/MechArena/Client/MechArenaHUDViewModel.h"
#include "GameFramework/Kits/Genre/Action/MechArena/Component/MechArenaGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {

        /** @brief 다운된 기체의 높이 비율입니다. */
        constexpr float32 kMechArenaDownHeightRatio = 0.4f;

        /** @brief 액션 누름을 입력 깃발로 바꿉니다. */
        uint8 toMechInputFlag( bool bPressed )
        {
            return bPressed ? SW_TRUE : SW_FALSE;
        }
    } // namespace

    MechArenaViewComponent::MechArenaViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&MechArenaViewComponent::updateView, &MechArenaViewComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _fireAction{}
        , _meleeAction{}
        , _specialAction{}
        , _dashAction{}
        , _jumpAction{}
        , _lockAction{}
        , _listPilotBody{}
        , _mechHeight{ 2.4f }
        , _projectilePrefab{}
        , _projectilePoolSize{ 0 }
        , _hudViewModel{}
        , _projectilePool{ string{}, 0, SpawnLifetime::Derived }
        , _listProjectileView{}
    {
    }

    MechArenaViewComponent::~MechArenaViewComponent() = default;

    void MechArenaViewComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _projectilePool = PrefabPool( _projectilePrefab.path(), _projectilePoolSize, SpawnLifetime::Derived );
    }

    void MechArenaViewComponent::onEndPlay()
    {
        despawnViews();
        GameModeViewComponent::onEndPlay();
    }

    void MechArenaViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _projectilePool.destroyAll( *pManager );
        _listProjectileView.clear();
    }

    void MechArenaViewComponent::updateView( GameObjectManager& manager, MechArenaGameModeComponent& director )
    {
        dispatchInput( manager, director );
        updatePilotBody( manager, director );
        syncProjectiles( manager, director );
        updateHUD( director );
    }

    void MechArenaViewComponent::submitInput( const GameModeInput& view, MechArenaGameModeComponent& director ) const
    {
        const float2 move = view.getVector2D( _moveAction );
        MechInput    input;
        input._moveX    = move._x;
        input._moveZ    = move._y;
        input._bFire    = toMechInputFlag( view.isActionDown( _fireAction ) );
        input._bMelee   = toMechInputFlag( view.wasActionTriggered( _meleeAction ) );
        input._bSpecial = toMechInputFlag( view.wasActionTriggered( _specialAction ) );
        input._bDash    = toMechInputFlag( view.wasActionTriggered( _dashAction ) );
        input._bJump    = toMechInputFlag( view.wasActionTriggered( _jumpAction ) );
        input._bHover   = toMechInputFlag( view.isActionDown( _jumpAction ) );
        input._bLockOn  = toMechInputFlag( view.wasActionTriggered( _lockAction ) );
        // 판이 끝났거나 덱이 하나면 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
        const bool bDestroyed = director.getPlayerState() == static_cast<int32>( MechPilotState::Destroyed );
        if ( bDestroyed && input._bSpecial == SW_TRUE )
            (void)director.requestNextMech();
        (void)director.submitPlayerInput( input );
    }

    void MechArenaViewComponent::updatePilotBody( GameObjectManager& manager, const MechArenaGameModeComponent& director ) const
    {
        for ( size_t index = 0; index < _listPilotBody.size(); ++index )
        {
            const int32            pilot   = static_cast<int32>( index );
            GameObject* const      pObject = _listPilotBody[index].isValid() ? manager.resolveGameObject( _listPilotBody[index] ) : nullptr;
            MeshComponent* const   pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
            const MechPilot* const pPilot  = director.getWorld().findPilot( pilot );
            if ( pMesh == nullptr )
                continue;
            const bool bShown = pPilot != nullptr && pPilot->getMech() != nullptr && pPilot->_state != MechPilotState::Destroyed && pPilot->_state != MechPilotState::Waiting;
            if ( pMesh->isVisible() != bShown ) // 같은 값이어도 렌더 상태를 다시 짓는다
                pMesh->setVisible( bShown );
            if ( bShown == false )
                continue;
            const float32 height = pPilot->_state == MechPilotState::Down ? _mechHeight * kMechArenaDownHeightRatio : _mechHeight;
            const float32 width  = pPilot->getMech()->_radius * 2.0f;
            pMesh->setLocalScale( float3( width, height, width ) );
            pMesh->setLocalRotation( float3( 0.0f, MathUtil::atan2( pPilot->_forward._x, pPilot->_forward._z ), 0.0f ) );
            pMesh->setWorldPosition( manager.getWorldOrigin().toLocal( director.getPilotPosition( pilot ) + float3( 0.0f, height * 0.5f, 0.0f ) ) );
        }
    }

    void MechArenaViewComponent::syncProjectiles( GameObjectManager& manager, const MechArenaGameModeComponent& director )
    {
        const vector<ProjectileState>& listProjectile = director.getWorld().getProjectiles();
        while ( _listProjectileView.size() > listProjectile.size() )
        {
            _projectilePool.release( manager, _listProjectileView.back() );
            _listProjectileView.pop_back();
        }
        while ( _listProjectileView.size() < listProjectile.size() )
        {
            GameObject* const pObject = _projectilePool.acquire( manager, "MechArenaProjectile" );
            if ( pObject == nullptr )
                return; // 프리팹을 읽지 못했다(오류는 풀이 남긴다) — 탄은 보이지 않아도 난다
            _listProjectileView.push_back( pObject->getHandle() );
        }
        for ( size_t index = 0; index < listProjectile.size(); ++index )
        {
            GameObject* const     pObject = manager.resolveGameObject( _listProjectileView[index] );
            SceneComponent* const pRoot   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pRoot != nullptr )
                pRoot->setWorldPosition( manager.getWorldOrigin().toLocal( AbsolutePosition{ listProjectile[index]._position } ) );
        }
    }

    void MechArenaViewComponent::updateHUD( const MechArenaGameModeComponent& director )
    {
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<MechArenaHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>();
        GameObject* const          pOwner  = getOwner();
        if ( pBinder != nullptr && pOwner != nullptr )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const MechPilot* const pPilot = director.getWorld().findPilot( MechArenaGameModeComponent::kPlayer );
        if ( pPilot == nullptr )
            return;
        const bool bEnded = director.getWorld().isEnded();
        _hudViewModel->setPilot( director.computeHealthRatio( MechArenaGameModeComponent::kPlayer ), 1.0f - pPilot->_boost.getRatio(), director.getPlayerAmmo() ); // 부스트는 열이 차는 과열형이다
        _hudViewModel->setTeamGauge( director.getPlayerGauge(), director.getEnemyGauge() );
        _hudViewModel->setHints( pPilot->_boost.isOverheated(), pPilot->_state == MechPilotState::Destroyed );
        _hudViewModel->setResult( director.hasWon(), bEnded && director.hasWon() == false );
    }
} // namespace sw
