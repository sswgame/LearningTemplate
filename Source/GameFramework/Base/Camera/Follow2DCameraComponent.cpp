#include "pch.h"

#include "GameFramework/Base/Camera/Follow2DCameraComponent.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Camera/CameraShake.h"

namespace sw
{
    Follow2DCameraComponent::Follow2DCameraComponent()
        : _targetPos{ 0.0f, 0.0f }
        , _appliedShake{ 0.0f, 0.0f }
        , _followSpeed{ 0.0f }
        , _shakeIntensity{ 0.0f }
        , _shakeDuration{ 0.0f }
        , _shakeFrequency{ kDefaultShakeFrequency }
        , _shakeElapsed{ 0.0f }
        , _shakeTotalDuration{ 0.0f }
    {
    }

    void Follow2DCameraComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
    }

    void Follow2DCameraComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        if ( _shakeDuration > 0.0f )
        {
            _shakeDuration = MathUtil::max( _shakeDuration - deltaTime, 0.0f );
            _shakeElapsed += deltaTime;
        }
        const float2 shakeOffset = getShakeOffset();

        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;

        SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return;

        // 기준은 주인이 지금 놓인 자리에서 지난 틱에 얹은 흔들림을 걷어 낸 자리다 — 놓은 자리 · 다른 코드가 옮긴 자리를 따른다. 따라가는 속도가 있을 때만
        // 목표 쪽으로 옮긴다.
        // 목표는 월드 자리다 — 월드로 읽고 쓴다(카메라를 리그 · 플레이어 아래에 둬도 같다).
        const float3 pos     = pSceneComp->getWorldPosition();
        float2       basePos = float2{ pos._x, pos._y } - _appliedShake;
        if ( _followSpeed > 0.0f )
            basePos = float2::lerp( basePos, _targetPos, MathUtil::saturate( _followSpeed * deltaTime ) );
        _appliedShake = shakeOffset;
        pSceneComp->setWorldPosition( float3{ basePos + shakeOffset, pos._z } );
    }

    float2 Follow2DCameraComponent::getShakeOffset() const
    {
        if ( _shakeDuration <= 0.0f || _shakeTotalDuration <= 0.0f )
            return float2{ 0.0f, 0.0f };

        // 흔들림 모양은 카메라 충격(`CameraImpulse`)과 같은 식이다 — 크기는 남은 비율로 잦아들고 위상은 흐른 시간으로 간다.
        CameraImpulse impulse;
        impulse._def._amplitude        = _shakeIntensity;
        impulse._def._duration         = _shakeTotalDuration;
        impulse._def._frequency        = _shakeFrequency;
        impulse._elapsed               = _shakeElapsed;
        const CameraShakeOffset offset = impulse.computeOffset( impulse._origin );
        return float2{ offset._position._x, offset._position._y };
    }

    void Follow2DCameraComponent::shake( float32 intensity, float32 duration, float32 frequency )
    {
        _shakeIntensity     = intensity;
        _shakeDuration      = MathUtil::max( duration, 0.0f );
        _shakeTotalDuration = _shakeDuration;
        _shakeElapsed       = 0.0f;
        // **여기서 진동 수를 넣는다.** 안 넣으면 코드로 부른 흔들림이 떨리지 않는다(기본 0).
        _shakeFrequency = MathUtil::max( frequency, 0.0f );
    }
} // namespace sw
