#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Client/MetroidvaniaViewComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Client/MetroidvaniaHUDViewModel.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Component/MetroidvaniaGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {
        /** @brief 액션 누름을 입력 깃발로 바꿉니다. */
        uint8 toMetroidvaniaInputFlag( bool bPressed )
        {
            return bPressed ? SW_TRUE : SW_FALSE;
        }
    } // namespace

    MetroidvaniaViewComponent::MetroidvaniaViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&MetroidvaniaViewComponent::updateView, &MetroidvaniaViewComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _jumpAction{}
        , _dashAction{}
        , _attackAction{}
        , _guardAction{}
        , _useAction{}
        , _flaskAction{}
        , _playerBody{}
        , _corpseBody{}
        , _listEnemyBody{}
        , _listPickupBody{}
        , _enemySize{ 0.9f, 1.0f, 0.9f }
        , _startupScale{ 1.4f }
        , _markerSize{ 0.5f, 0.5f, 0.5f }
        , _hudViewModel{}
    {
    }

    MetroidvaniaViewComponent::~MetroidvaniaViewComponent() = default;

    void MetroidvaniaViewComponent::updateView( GameObjectManager& manager, MetroidvaniaGameModeComponent& director )
    {
        dispatchInput( manager, director );
        updateScene( manager, director );
        updateHUD( director );
    }

    void MetroidvaniaViewComponent::submitInput( const GameModeInput& input, MetroidvaniaGameModeComponent& director ) const
    {
        MetroidvaniaPlayerCommand command;
        command._move           = input.getVector2D( _moveAction );
        command._bJumpHeld      = toMetroidvaniaInputFlag( input.isActionDown( _jumpAction ) );
        command._bJumpPressed   = toMetroidvaniaInputFlag( input.wasActionPressed( _jumpAction ) );
        command._bDashPressed   = toMetroidvaniaInputFlag( input.wasActionTriggered( _dashAction ) );
        command._bAttackPressed = toMetroidvaniaInputFlag( input.wasActionTriggered( _attackAction ) );
        command._bGuardHeld     = toMetroidvaniaInputFlag( input.isActionDown( _guardAction ) );
        command._bParryPressed  = toMetroidvaniaInputFlag( input.wasActionPressed( _guardAction ) );
        command._bUsePressed    = toMetroidvaniaInputFlag( input.wasActionTriggered( _useAction ) );
        command._bFlaskPressed  = toMetroidvaniaInputFlag( input.wasActionTriggered( _flaskAction ) );
        // 이겼으면 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
        (void)director.submitPlayerInput( command );
    }

    void MetroidvaniaViewComponent::placeBody( GameObjectManager& manager, GameObjectHandle body, bool bShown, const float2& position, const float3& size )
    {
        GameObject* const    pObject = body.isValid() ? manager.resolveGameObject( body ) : nullptr;
        MeshComponent* const pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        if ( pMesh->isVisible() != bShown ) // 같은 값이어도 렌더 상태를 다시 짓는다
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        pMesh->setLocalScale( size );
        pMesh->setWorldPosition( manager.getWorldOrigin().toLocal( MetroidvaniaGameModeComponent::toAbsolute( position ) ) );
    }

    void MetroidvaniaViewComponent::updateScene( GameObjectManager& manager, const MetroidvaniaGameModeComponent& director ) const
    {
        const float2 halfExtents = director.getMotor().getSettings()._halfExtents;
        const bool   bAlive      = director.getExplorePhase() != MetroidvaniaPhase::Dead;
        placeBody( manager, _playerBody, bAlive, director.getMotor().getPosition(), float3( halfExtents._x * 2.0f, halfExtents._y * 2.0f, halfExtents._x * 2.0f ) );
        const MetroidvaniaCorpse& corpse = director.getDeathRest().getCorpse();
        placeBody( manager, _corpseBody, corpse._bActive == SW_TRUE, corpse._position, _markerSize );
        const vector<MetroidvaniaEnemy>& listEnemy = director.getEnemies();
        for ( size_t index = 0; index < _listEnemyBody.size() && index < listEnemy.size(); ++index )
        {
            const float32 heightScale = listEnemy[index]._startup.isActive() ? _startupScale : 1.0f;
            placeBody( manager, _listEnemyBody[index], director.isEnemyAlive( index ), listEnemy[index]._position, float3( _enemySize._x, _enemySize._y * heightScale, _enemySize._z ) );
        }
        const vector<float2>& listPickupPosition = director.getPickupPositions();
        for ( size_t index = 0; index < _listPickupBody.size() && index < listPickupPosition.size(); ++index )
        {
            placeBody( manager, _listPickupBody[index], director.isPickupLeft( index ), listPickupPosition[index], _markerSize );
        }
    }

    void MetroidvaniaViewComponent::updateHUD( const MetroidvaniaGameModeComponent& director )
    {
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<MetroidvaniaHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>();
        GameObject* const          pOwner  = getOwner();
        if ( pBinder != nullptr && pOwner != nullptr )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const HealthStateMachine& health = director.getPlayer().getHealthMachine();
        _hudViewModel->setVitals( health.getHealth() / health.getSettings()._maxHealth, director.getPlayer().getStamina().getRatio(), director.getDeathRest().getFlaskCharges() );
        _hudViewModel->setProgress( director.getCurrency(), director.getMapState().computeCompletionPercent() );
        _hudViewModel->setResult( director.getExplorePhase() == MetroidvaniaPhase::Dead, director.getExplorePhase() == MetroidvaniaPhase::Won );
    }
} // namespace sw
