#include "pch.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    GameModeViewComponent::GameModeViewComponent( const GameModeViewBinding& binding )
        : _binding{ binding }
        , _gameMode{}
        , _bTimeRunning{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    GameModeViewComponent::~GameModeViewComponent() = default;

    void GameModeViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const pOwner = getOwner();
        _gameMode                = TComponentHandle<GameModeComponent>( pOwner != nullptr ? _binding._pFind( *pOwner ) : nullptr );
    }

    void GameModeViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _gameMode.isValid() == false )
            SW_LOG_ERROR_ID( "GameMode.ViewWithoutGameMode", "%# on '%#': its game mode is not in front of it on the same object - it does nothing", getTypeName().c_str(),
                             getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
    }

    void GameModeViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bTimeRunning = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 명령 · 모습은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( GameModeViewComponent& view, GameObjectManager& manager )
        { view.updateOpenGame( manager ); } );
    }

    void GameModeViewComponent::updateOpenGame( GameObjectManager& manager )
    {
        GameModeComponent* const pGameMode = manager.resolveComponent( _gameMode );
        if ( pGameMode != nullptr && pGameMode->isStarted() )
            _binding._pUpdate( *this, manager, *pGameMode );
    }

    void GameModeViewComponent::dispatchInput( GameObjectManager& manager, GameModeComponent& gameMode )
    {
        const bool          bReadInput = isTimeRunning() && _binding._pInput != nullptr && gameMode.isAcceptingInput();
        InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput == nullptr )
            return;
        // 일시정지 메뉴 같은 UI 가 먹은 입력은 보이지 않는다(`GameInputView`).
        const IInputCaptureQuery* const pCapture = game::findSubsystem<IInputCaptureQuery>();
        if ( _binding._inputUser == GameModeInputUser::Primary )
        {
            _binding._pInput( *this, GameModeInput( GameInputView( pInput->getInputMap(), pCapture ), *pInput, manager, 0 ), gameMode );
            return;
        }
        for ( uint32 user = 0; user < pInput->getLocalUserCount(); ++user )
        {
            const InputMap* const pUserMap = pInput->findUserMap( user );
            if ( pUserMap != nullptr )
                _binding._pInput( *this, GameModeInput( GameInputView( *pUserMap, pCapture ), *pInput, manager, user ), gameMode );
        }
    }

    const GameModeComponent* GameModeViewComponent::findGameMode( const GameObjectManager& manager ) const
    {
        return manager.resolveComponent( _gameMode );
    }
} // namespace sw
