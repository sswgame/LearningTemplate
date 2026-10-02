#include "pch.h"

#include "GameFramework/Kits/Overworld/CameraControllerComponent.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    CameraControllerComponent::CameraControllerComponent()
        : _targetPos{ 0.0f, 0.0f }
        , _currentPos{ 0.0f, 0.0f }
        , _appliedShake{ 0.0f, 0.0f }
        , _followSpeed{ 0.0f }
        , _shakeIntensity{ 0.0f }
        , _shakeDuration{ 0.0f }
        , _shakeFrequency{ kDefaultShakeFrequency }
        , _shakeElapsed{ 0.0f }
        , _shakeTotalDuration{ 0.0f }
    {
    }

    void CameraControllerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
    }

    void CameraControllerComponent::onEndPlay()
    {
        Component::onEndPlay();
    }

    void CameraControllerComponent::onTick( float32 deltaTime )
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
        // 목표 쪽으로 옮긴다. 예전에는 `_currentPos`(기본 (0,0))를 매 틱 썼고 속도의 기본이 0 이라, 카메라가 놓인 자리와 상관없이 원점에 박혔다.
        // 목표는 월드 자리다 — 월드로 읽고 쓴다(카메라를 리그 · 플레이어 아래에 둬도 같다).
        const float3 pos     = pSceneComp->getWorldPosition();
        float2       basePos = float2{ pos._x, pos._y } - _appliedShake;
        if ( _followSpeed > 0.0f )
            basePos = float2::lerp( basePos, _targetPos, MathUtil::saturate( _followSpeed * deltaTime ) );
        _currentPos   = basePos;
        _appliedShake = shakeOffset;
        pSceneComp->setWorldPosition( float3{ basePos + shakeOffset, pos._z } );
    }

    float2 CameraControllerComponent::getShakeOffset() const
    {
        if ( _shakeDuration <= 0.0f || _shakeTotalDuration <= 0.0f )
            return float2{ 0.0f, 0.0f };

        // 크기는 **남은 비율**로 잦아들고, 위상은 **흐른 시간**으로 간다. 예전에는 둘 다
        // 남은 시간으로 계산해서, 끝나기 직전에 `cos` 항이 최대(=1)가 되어 가장 크게 튀고
        // 그 다음 프레임에 0 으로 끊겼다.
        const float32 amplitude = _shakeIntensity * MathUtil::saturate( _shakeDuration / _shakeTotalDuration );
        return float2{ MathUtil::sin( _shakeElapsed * _shakeFrequency ) * amplitude,
                       MathUtil::cos( _shakeElapsed * ( _shakeFrequency * 1.3f ) ) * ( amplitude * 0.75f ) };
    }

    void CameraControllerComponent::shake( float32 intensity, float32 duration, float32 frequency )
    {
        _shakeIntensity     = intensity;
        _shakeDuration      = MathUtil::max( duration, 0.0f );
        _shakeTotalDuration = _shakeDuration;
        _shakeElapsed       = 0.0f;
        // **여기서 진동 수를 넣는다.** 예전에는 안 넣어서 코드로 부른 흔들림이 떨리지 않았다.
        _shakeFrequency = MathUtil::max( frequency, 0.0f );
    }
} // namespace sw
