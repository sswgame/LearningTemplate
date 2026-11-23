#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Weather/WeatherWaveComponent.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

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

    void WeatherWaveComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*         pOwner = getOwner();
        WaterBodyComponent* pWater = pOwner != nullptr ? pOwner->getComponent<WaterBodyComponent>() : nullptr;
        if ( pWater == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "WeatherWave.NoWater", "'%#' has a WeatherWaveComponent without a WaterBodyComponent - the weather changes no waves",
                                  pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        pWater->setWaveAmplitudeScale( computeAmplitudeScale( GameStateComponent::readWeather( context.getManager(), _weatherValue ), _stormScale ) ); // 날씨가 없는 씬은 잔잔하다(0)
    }
} // namespace sw
