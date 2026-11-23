#include "pch.h"

#include "GameFramework/Runtime/World/Environment/DaylightComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"

#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    DaylightComponent::DaylightComponent()
        : _gameMode{}
        , _nightIntensity{ 0.2f }
        , _sunriseHour{ 6.0f }
        , _sunsetHour{ 20.0f }
        , _nightEndHour{ 5.0f }
        , _horizonIntensity{ 0.25f }
        , _noonIntensity{ 1.5f }
        , _horizonPitch{ Radians::fromRadians( 0.25f ) }
        , _noonPitch{ Radians::fromRadians( 1.35f ) }
        , _morningYaw{ Radians::fromRadians( -1.2f ) }
        , _eveningYaw{ Radians::fromRadians( 1.2f ) }
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
        GameObject*                pOwner = getOwner();
        DirectionalLightComponent* pSun   = pOwner != nullptr ? pOwner->getComponent<DirectionalLightComponent>() : nullptr;
        if ( pSun == nullptr )
            return;
        const GameModeComponent* pGameMode = GameModeComponent::resolve<GameModeComponent>( context.getManager(), _gameMode );
        float32                  hour      = 0.0f;
        if ( pGameMode == nullptr || pGameMode->readHourOfDay( hour ) == false )
            return;
        const float32 dayLength  = MathUtil::max( _sunsetHour - _sunriseHour, MathUtil::kEpsilon );
        const float32 dayRatio   = MathUtil::clamp( ( hour - _sunriseHour ) / dayLength, 0.0f, 1.0f );
        const float32 height     = MathUtil::sin( dayRatio * MathUtil::kPi );
        const float32 light      = MathUtil::lerp( _horizonIntensity, _noonIntensity, height );
        const bool    bNightTime = hour > _sunsetHour || hour < _nightEndHour; // 자정을 넘긴 밤(밤 끝 시각까지)도 밤
        pSun->setIntensity( bNightTime ? _nightIntensity : light );
        pSun->setLocalRotation( float3{ MathUtil::lerp( _horizonPitch.radians(), _noonPitch.radians(), height ), MathUtil::lerp( _morningYaw.radians(), _eveningYaw.radians(), dayRatio ), 0.0f } );
    }
} // namespace sw
