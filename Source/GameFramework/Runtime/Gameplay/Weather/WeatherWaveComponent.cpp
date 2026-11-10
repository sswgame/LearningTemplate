#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Weather/WeatherWaveComponent.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

namespace sw
{
    WeatherWaveComponent::WeatherWaveComponent()
        : _weatherValue{}
        , _stormScale{ 4.0f }
    {
        setCanEverTick( true );
        setTickGroup( TickGroup::PrePhysics ); // 부력(물리 스텝)과 물 틱(PostUpdate)이 같은 프레임의 배율을 읽는다
    }

    float32 WeatherWaveComponent::computeAmplitudeScale( float32 weatherValue, float32 stormScale )
    {
        return 1.0f + ( stormScale - 1.0f ) * MathUtil::max( weatherValue, 0.0f );
    }

    void WeatherWaveComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*         pOwner = getOwner();
        WaterBodyComponent* pWater = pOwner != nullptr ? pOwner->getComponent<WaterBodyComponent>() : nullptr;
        if ( pWater == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "WeatherWave.NoWater", "'%#' has a WeatherWaveComponent without a WaterBodyComponent - the weather changes no waves",
                                  pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        // 날씨가 없는 씬은 잔잔한 값(0)이다 — 판 상태가 날씨를 열지 않았다(SurfaceExposureComponent 의 비와 같은 규칙).
        const SessionStateComponent* pSession = SessionStateComponent::findFirstOpen( pOwner->getManager() );
        const float32                value    = pSession != nullptr ? pSession->getWeather().computeValue( _weatherValue ) : 0.0f;
        pWater->setWaveAmplitudeScale( computeAmplitudeScale( value, _stormScale ) );
    }
} // namespace sw
