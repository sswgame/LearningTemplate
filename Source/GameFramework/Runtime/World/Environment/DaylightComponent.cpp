#include "pch.h"

#include "GameFramework/Runtime/World/Environment/DaylightComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"

#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    DaylightComponent::DaylightComponent()
        : _gameMode{}
        , _rainDimming{ 0.55f }
        , _nightIntensity{ 0.2f }
        , _sunriseHour{ 6.0f }
        , _sunsetHour{ 20.0f }
        , _nightEndHour{ 5.0f }
        , _horizonIntensity{ 0.25f }
        , _noonIntensity{ 1.5f }
        , _horizonPitch{ 0.25f }
        , _noonPitch{ 1.35f }
        , _morningYaw{ -1.2f }
        , _eveningYaw{ 1.2f }
    {
        setCanEverTick( true );
    }

    void DaylightComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 시간을 흘린 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void DaylightComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*                pOwner   = getOwner();
        GameObjectManager*         pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        DirectionalLightComponent* pSun     = pOwner != nullptr ? pOwner->getComponent<DirectionalLightComponent>() : nullptr;
        if ( pManager == nullptr || pSun == nullptr )
            return;
        const GameModeComponent* pGameMode    = GameModeComponent::resolve<GameModeComponent>( *pManager, _gameMode );
        float32                  hour         = 0.0f;
        bool                     bPrecipitate = false;
        if ( pGameMode == nullptr || pGameMode->readDaylight( hour, bPrecipitate ) == false )
            return;
        const float32 dayLength  = MathUtil::max( _sunsetHour - _sunriseHour, MathUtil::kEpsilon );
        const float32 dayRatio   = MathUtil::clamp( ( hour - _sunriseHour ) / dayLength, 0.0f, 1.0f );
        const float32 height     = MathUtil::sin( dayRatio * MathUtil::kPi );
        const float32 light      = MathUtil::lerp( _horizonIntensity, _noonIntensity, height ) * ( bPrecipitate ? _rainDimming : 1.0f );
        const bool    bNightTime = hour > _sunsetHour || hour < _nightEndHour; // 자정을 넘긴 밤(밤 끝 시각까지)도 밤
        pSun->setIntensity( bNightTime ? _nightIntensity : light );
        pSun->setLocalRotation( float3{ MathUtil::lerp( _horizonPitch, _noonPitch, height ), MathUtil::lerp( _morningYaw, _eveningYaw, dayRatio ), 0.0f } );
    }
} // namespace sw
