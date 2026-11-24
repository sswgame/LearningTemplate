#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityFastForwardInputComponent.h"

#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    CityFastForwardInputComponent::CityFastForwardInputComponent()
        : _fastForwardAction{}
        , _director{}
        , _bInputLive{ SW_FALSE }
        , _bActionValid{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    CityFastForwardInputComponent::~CityFastForwardInputComponent() = default;

    void CityFastForwardInputComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        const GameObject* const pOwner = getOwner();
        _director                      = TComponentHandle<CityGameModeComponent>( pOwner != nullptr ? pOwner->getComponent<CityGameModeComponent>() : nullptr );
        if ( _director.isValid() == false )
            SW_LOG_ERROR_ID( "City.FastForwardWithoutGameMode", "City fast forward input on '%#': no CityGameModeComponent in front of it on the same object - it reads no input",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
    }

    void CityFastForwardInputComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bActionValid = _fastForwardAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionValid == SW_FALSE )
            SW_LOG_ERROR_ID( "City.FastForwardActionMissing", "City fast forward input: the action must be named - it reads no input" );
    }

    void CityFastForwardInputComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( CityFastForwardInputComponent& input, GameObjectManager& manager )
        { input.updateInput( manager ); } );
    }

    void CityFastForwardInputComponent::updateInput( GameObjectManager& manager )
    {
        CityGameModeComponent* const pDirector  = manager.resolveComponent( _director );
        const bool                   bReadInput = pDirector != nullptr && _bInputLive == SW_TRUE && _bActionValid == SW_TRUE;
        const InputManager* const    pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput == nullptr )
            return;
        const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
        (void)pDirector->submitFastForward( input.isActionDown( _fastForwardAction ) ); // 판이 열리기 전이면 거절한다
    }
} // namespace sw
