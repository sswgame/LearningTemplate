#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityFastForwardInputComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    CityFastForwardInputComponent::CityFastForwardInputComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&CityFastForwardInputComponent::updateInput, &CityFastForwardInputComponent::submitInput>() )
        , _fastForwardAction{}
    {
        setCanEverTick( true );
    }

    CityFastForwardInputComponent::~CityFastForwardInputComponent() = default;

    void CityFastForwardInputComponent::updateInput( GameObjectManager& manager, CityGameModeComponent& director )
    {
        dispatchInput( manager, director );
    }

    void CityFastForwardInputComponent::submitInput( const GameModeInput& input, CityGameModeComponent& director )
    {
        (void)director.submitFastForward( input.isActionDown( _fastForwardAction ) ); // 열린 판인지는 베이스가 보았다
    }
} // namespace sw
