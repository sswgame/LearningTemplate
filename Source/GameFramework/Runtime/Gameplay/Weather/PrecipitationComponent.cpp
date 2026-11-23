#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Weather/PrecipitationComponent.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Environment/EnvironmentUtil.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/VFX/VFXComponent.h"

#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    PrecipitationComponent::PrecipitationComponent()
        : _weatherValue{}
        , _intensityParameter{}
        , _height{ 10.0f }
        , _writtenIntensity{ 0.0f }
    {
        setCanEverTick( true );
    }

    void PrecipitationComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*   pOwner  = getOwner();
        VFXComponent* pEffect = pOwner != nullptr ? pOwner->getComponent<VFXComponent>() : nullptr;
        if ( pEffect == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "Precipitation.NoEffect", "'%#' has a PrecipitationComponent without a VFXComponent - nothing falls", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        const float32 intensity = MathUtil::saturate( GameStateComponent::readWeather( context.getManager(), _weatherValue ) ); // 날씨가 없는 씬은 강수가 없다(0)
        float3        view{};
        const bool    bActive = intensity > 0.0f && EnvironmentUtil::findViewPosition( context.getManager(), view );
        if ( bActive == false )
        {
            if ( pEffect->isPlaying() )
                pEffect->stop();
            _writtenIntensity = 0.0f;
            return;
        }
        pEffect->setWorldPosition( view + float3{ 0.0f, _height, 0.0f } );
        if ( intensity != _writtenIntensity )
        {
            (void)pEffect->setUserParameter( _intensityParameter.view(), float4{ 1.0f, 1.0f, 1.0f, intensity } ); // 없는 이름이면 효과가 오류를 남긴다
            _writtenIntensity = intensity;
        }
        if ( pEffect->isPlaying() == false )
            pEffect->play();
    }
} // namespace sw
