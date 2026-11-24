#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Client/ActionPlatformerAvatarComponent.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/WorldOrigin.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Component/ActionPlatformerGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    ActionPlatformerAvatarComponent::ActionPlatformerAvatarComponent()
        : _moveAction{ CommonInputActions::kMove }
        , _jumpAction{}
        , _director{}
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ActionPlatformerAvatarComponent::~ActionPlatformerAvatarComponent() = default;

    void ActionPlatformerAvatarComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                              pOwner    = getOwner();
        const ActionPlatformerGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<ActionPlatformerGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "ActionPlatformer.AvatarWithoutGameMode",
                             "Action platformer avatar on '%#': no ActionPlatformerGameModeComponent in front of it on the same object - the avatar does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<ActionPlatformerGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<ActionPlatformerGameModeComponent>( pDirector );
    }

    void ActionPlatformerAvatarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bActionsValid = _moveAction.empty() == false && _jumpAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "ActionPlatformer.AvatarActionMissing", "Action platformer avatar: move '%#' and jump '%#' actions must both be named - the avatar reads no input", _moveAction.c_str(),
                             _jumpAction.c_str() );
    }

    void ActionPlatformerAvatarComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( ActionPlatformerAvatarComponent& avatar, GameObjectManager& manager )
        { avatar.updateAvatar( manager ); } );
    }

    void ActionPlatformerAvatarComponent::updateAvatar( GameObjectManager& manager )
    {
        ActionPlatformerGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        const bool                bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE && pDirector->isAcceptingInput();
        const InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput != nullptr )
        {
            const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
            // 판이 끝났으면 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
            (void)pDirector->submitPlayerInput( input.getVector2D( _moveAction )._x, input.isActionDown( _jumpAction ) );
        }
        drawStage( *pDirector, manager );
    }

    void ActionPlatformerAvatarComponent::drawStage( const ActionPlatformerGameModeComponent& director, const GameObjectManager& manager )
    {
        DebugDrawQueue* pDraw = game::findSubsystem<DebugDrawQueue>();
        if ( pDraw == nullptr )
            return;
        // 화면은 x 오른쪽 · y 위 평면이다(카메라가 -Z 에서 본다).
        const WorldOrigin&     origin = manager.getWorldOrigin();
        const PlatformTileMap& map    = director.getTileMap();
        const float32          half   = map.getTileSize() * 0.5f;
        for ( int32 y = 0; y < map.getHeight(); ++y )
        {
            for ( int32 x = 0; x < map.getWidth(); ++x )
            {
                if ( map.getTile( x, y ) != PlatformTile::Solid )
                    continue;
                pDraw->drawBox( origin.toLocal( float3( map.getTileLeft( x ) + half, map.getTileBottom( y ) + half, 0.0f ) ), float3( half, half, half ), float4( 0.45f, 0.4f, 0.35f, 1.0f ) );
            }
        }
        const ActionStageRun& run            = director.getRun();
        const ActionStageDef* pStage         = run.getStage();
        const vector<float2>& listSecretSpot = director.getSecretPositions();
        for ( size_t secretIndex = 0; pStage != nullptr && secretIndex < listSecretSpot.size(); ++secretIndex )
        {
            if ( run.isSecretFound( pStage->_listSecret[secretIndex] ) )
                continue;
            pDraw->drawSphere( origin.toLocal( float3( listSecretSpot[secretIndex]._x, listSecretSpot[secretIndex]._y, 0.0f ) ), 0.25f, float4( 1.0f, 0.85f, 0.2f, 1.0f ) );
        }
        const float2  position = director.getPlayerPosition();
        const float32 goalX    = director.getGoalX();
        pDraw->drawBox( origin.toLocal( float3( position._x, position._y, 0.0f ) ), float3( 0.35f, 0.45f, 0.35f ), float4( 0.2f, 0.6f, 1.0f, 1.0f ) );
        pDraw->drawLine( origin.toLocal( float3( goalX, 0.0f, 0.0f ) ), origin.toLocal( float3( goalX, 4.0f, 0.0f ) ), float4( 0.3f, 1.0f, 0.4f, 1.0f ) );
    }
} // namespace sw
