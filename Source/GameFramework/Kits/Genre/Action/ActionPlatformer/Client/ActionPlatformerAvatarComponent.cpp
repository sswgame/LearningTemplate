#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Client/ActionPlatformerAvatarComponent.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Component/ActionPlatformerGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    ActionPlatformerAvatarComponent::ActionPlatformerAvatarComponent()
        : _moveAction{ CommonInputActions::kMove }
        , _jumpAction{}
        , _cubePrefab{}
        , _spherePrefab{}
        , _tileColor{ 0.45f, 0.4f, 0.35f, 1.0f }
        , _secretRadius{ 0.25f }
        , _secretColor{ 1.0f, 0.85f, 0.2f, 1.0f }
        , _playerHalfExtent{ 0.35f, 0.45f, 0.35f }
        , _playerColor{ 0.2f, 0.6f, 1.0f, 1.0f }
        , _goalHeight{ 4.0f }
        , _goalWidth{ 0.1f }
        , _goalColor{ 0.3f, 1.0f, 0.4f, 1.0f }
        , _director{}
        , _cubeViews{}
        , _secretViews{}
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

    void ActionPlatformerAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _cubeViews.destroyAll( *pManager );
            _secretViews.destroyAll( *pManager );
        }
        Component::onEndPlay();
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
        showStage( *pDirector, manager );
    }

    void ActionPlatformerAvatarComponent::showStage( const ActionPlatformerGameModeComponent& director, GameObjectManager& manager )
    {
        // 화면은 x 오른쪽 · y 위 평면이다(카메라가 -Z 에서 본다). 가로로 이어진 타일은 상자 하나로 보인다.
        const PlatformTileMap& map  = director.getTileMap();
        const float32          size = map.getTileSize();
        const float32          half = size * 0.5f;
        _cubeViews.begin();
        for ( int32 y = 0; y < map.getHeight(); ++y )
        {
            for ( int32 x = 0; x < map.getWidth(); ++x )
            {
                if ( map.getTile( x, y ) != PlatformTile::Solid )
                    continue;
                int32 endX = x + 1;
                while ( endX < map.getWidth() && map.getTile( endX, y ) == PlatformTile::Solid )
                    ++endX;
                const float32 width = static_cast<float32>( endX - x ) * size;
                (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( map.getTileLeft( x ) + width * 0.5f, map.getTileBottom( y ) + half, 0.0f ) }, float3( width, size, size ), 0.0f,
                                       _tileColor );
                x = endX;
            }
        }
        const float2  position = director.getPlayerPosition();
        const float32 goalX    = director.getGoalX();
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( position._x, position._y, 0.0f ) }, _playerHalfExtent * 2.0f, 0.0f, _playerColor );
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( goalX, _goalHeight * 0.5f, 0.0f ) }, float3( _goalWidth, _goalHeight, _goalWidth ), 0.0f, _goalColor );
        _cubeViews.end( manager );

        const ActionStageRun& run            = director.getRun();
        const ActionStageDef* pStage         = run.getStage();
        const vector<float2>& listSecretSpot = director.getSecretPositions();
        const float3          secretScale{ _secretRadius * 2.0f };
        _secretViews.begin();
        for ( size_t secretIndex = 0; pStage != nullptr && secretIndex < listSecretSpot.size(); ++secretIndex )
        {
            if ( run.isSecretFound( pStage->_listSecret[secretIndex] ) )
                continue;
            (void)_secretViews.show( manager, _spherePrefab, AbsolutePosition{ float3( listSecretSpot[secretIndex]._x, listSecretSpot[secretIndex]._y, 0.0f ) }, secretScale, 0.0f, _secretColor );
        }
        _secretViews.end( manager );
    }
} // namespace sw
