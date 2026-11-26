#include "pch.h"

#include "GameFramework/Runtime/Character/Camera/CameraShake.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/ValueNoise.h"

#include "Engine/Object/GameObject/Registry/CameraShakeChannel.h"

#include "GameFramework/Runtime/Character/Camera/CameraPreset.h"

namespace sw
{
    namespace
    {
        struct CameraShakeInternal
        {
            /** @brief 채널마다 시드를 이만큼 떨어뜨린다 — 여섯 채널이 같은 잡음을 따라가지 않게. */
            static constexpr uint32 kChannelSeedStride = HashUtil::kGoldenRatio32;
            /** @brief 채널마다 시간을 조금씩 어긋나게 한다 — 시드만 다르면 정수 자리(값 0)가 모든 채널에서 같은 때 온다. */
            static constexpr float32 kChannelTimeOffset = 17.31f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    CameraShakeOffset computeCameraNoise( const CameraNoiseDef& noise, float32 time )
    {
        CameraShakeOffset offset{};
        if ( noise._frequency <= 0.0f )
            return offset;
        const float32 position = time * noise._frequency;
        float32       arrValue[6]{};
        for ( uint32 channel = 0; channel < 6; ++channel )
        {
            const uint32  seed    = noise._seed + ( channel + 1u ) * CameraShakeInternal::kChannelSeedStride;
            const float32 shifted = position + static_cast<float32>( channel ) * CameraShakeInternal::kChannelTimeOffset;
            arrValue[channel]     = ValueNoise::samplePerlin1D( shifted, seed );
        }
        offset._position = float3{ arrValue[0] * noise._positionAmplitude._x, arrValue[1] * noise._positionAmplitude._y, arrValue[2] * noise._positionAmplitude._z };
        offset._rotation = float3{ arrValue[3] * noise._rotationAmplitude.getPitch().radians(), arrValue[4] * noise._rotationAmplitude.getYaw().radians(),
                                   arrValue[5] * noise._rotationAmplitude.getRoll().radians() };
        return offset;
    }

    CameraPose applyCameraShake( const CameraPose& pose, const CameraShakeOffset& offset )
    {
        CameraPose   shaken  = pose;
        const float3 right   = float3::transform( float3{ 1.0f, 0.0f, 0.0f }, pose._rotation );
        const float3 up      = float3::transform( float3{ 0.0f, 1.0f, 0.0f }, pose._rotation );
        const float3 forward = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, pose._rotation );
        shaken._position     = pose._position + right * offset._position._x + up * offset._position._y + forward * offset._position._z;
        // 회전은 카메라 로컬에서 먼저 돈다 — 해밀턴 곱 `포즈 × 로컬` 은 로컬을 먼저 적용한다(`makeFromYawPitchRoll` = 요 × 피치 와 같은 규약).
        const quaternion local = quaternion::makeFromYawPitchRoll( offset._rotation._y, offset._rotation._x, offset._rotation._z );
        shaken._rotation       = pose._rotation * local;
        return shaken;
    }

    float32 CameraImpulse::computeEnvelope() const
    {
        if ( _def._duration <= 0.0f || _elapsed >= _def._duration )
            return 0.0f;
        // 남은 비율이 끝에서 정확히 0 을 보장하고, 감쇠 시간이 있으면 처음이 더 세다.
        const float32 remaining = MathUtil::saturate( 1.0f - _elapsed / _def._duration );
        const float32 decay     = _def._decayTime > 0.0f ? ::expf( -_elapsed / _def._decayTime ) : 1.0f;
        return remaining * decay;
    }

    CameraShakeOffset CameraImpulse::computeOffset( const float3& listenerPosition ) const
    {
        CameraShakeOffset offset{};
        float32           falloff = 1.0f;
        if ( _def._falloffRadius > 0.0f )
            falloff = MathUtil::saturate( 1.0f - ( listenerPosition - _origin ).getLength() / _def._falloffRadius );
        const float32 amplitude = _def._amplitude * computeEnvelope() * falloff;
        if ( amplitude <= 0.0f )
            return offset;
        // 위상은 흐른 시간으로 간다. 두 축의 진동수를 어긋나게 해 한 직선 위를 오가지 않게 한다.
        const float32 horizontal = MathUtil::sin( _elapsed * _def._frequency ) * amplitude;
        const float32 vertical   = MathUtil::cos( _elapsed * ( _def._frequency * 1.3f ) ) * ( amplitude * 0.75f );
        offset._position         = float3{ horizontal, vertical, 0.0f };
        offset._rotation         = float3{ vertical * _def._rotationScale, horizontal * _def._rotationScale, 0.0f };
        return offset;
    }

    CameraImpulseListener::CameraImpulseListener()
        : _listImpulse{}
    {
    }

    void CameraImpulseListener::addImpulse( const CameraImpulseDef& def, const float3& origin )
    {
        if ( def._duration <= 0.0f || def._amplitude <= 0.0f )
            return;
        CameraImpulse impulse;
        impulse._def    = def;
        impulse._origin = origin;
        _listImpulse.push_back( impulse );
    }

    void CameraImpulseListener::addRequest( const CameraShakeRequest& request )
    {
        CameraImpulseDef def;
        def._amplitude     = request._amplitude;
        def._duration      = request._duration;
        def._frequency     = request._frequency;
        def._falloffRadius = request._falloffRadius;
        addImpulse( def, request._origin );
    }

    void CameraImpulseListener::applyWorldOffset( const float3& delta )
    {
        for ( CameraImpulse& impulse : _listImpulse )
        {
            impulse._origin += delta;
        }
    }

    void CameraImpulseListener::step( float32 deltaTime )
    {
        const float32 elapsed = MathUtil::max( 0.0f, deltaTime );
        for ( CameraImpulse& impulse : _listImpulse )
        {
            impulse._elapsed += elapsed;
        }
        VectorUtil::removeIf( _listImpulse, []( const CameraImpulse& impulse )
        { return impulse.isFinished(); } );
    }

    CameraShakeOffset CameraImpulseListener::computeOffset( const float3& listenerPosition ) const
    {
        CameraShakeOffset sum{};
        for ( const CameraImpulse& impulse : _listImpulse )
        {
            sum += impulse.computeOffset( listenerPosition );
        }
        return sum;
    }
} // namespace sw

namespace sw
{
    float32 CameraFieldOfViewKick::computeEnvelope() const
    {
        // 오르내림은 블렌드 곡선(SmoothStep)과 같은 함수다 — 끝 속도가 0 이라 툭 끊기지 않는다.
        const BlendCurveDef smooth{};
        if ( _elapsed < 0.0f || isFinished() )
            return 0.0f;
        if ( _elapsed < _def._attackTime )
            return evaluateBlendWeight( smooth, _elapsed / _def._attackTime );
        const float32 released = _elapsed - _def._attackTime - _def._holdTime;
        if ( released <= 0.0f )
            return 1.0f;
        return _def._releaseTime > 0.0f ? 1.0f - evaluateBlendWeight( smooth, released / _def._releaseTime ) : 0.0f;
    }

    CameraFieldOfViewKickListener::CameraFieldOfViewKickListener()
        : _listKick{}
    {
    }

    void CameraFieldOfViewKickListener::addKick( const CameraFieldOfViewKickDef& def )
    {
        const float32 length = def._attackTime + def._holdTime + def._releaseTime;
        if ( length <= 0.0f || def._amount == 0.0f )
            return;
        CameraFieldOfViewKick kick;
        kick._def = def;
        _listKick.push_back( kick );
    }

    void CameraFieldOfViewKickListener::step( float32 deltaTime )
    {
        const float32 elapsed = MathUtil::max( 0.0f, deltaTime );
        for ( CameraFieldOfViewKick& kick : _listKick )
        {
            kick._elapsed += elapsed;
        }
        VectorUtil::removeIf( _listKick, []( const CameraFieldOfViewKick& kick )
        { return kick.isFinished(); } );
    }

    float32 CameraFieldOfViewKickListener::computeScale() const
    {
        float32 scale = 1.0f;
        for ( const CameraFieldOfViewKick& kick : _listKick )
        {
            scale += kick._def._amount * kick.computeEnvelope();
        }
        return MathUtil::max( kMinScale, scale );
    }

    CameraPose CameraFieldOfViewKickListener::applyToPose( const CameraPose& pose ) const
    {
        const float32 scale  = computeScale();
        CameraPose    kicked = pose;
        kicked._fieldOfViewY = MathUtil::min( pose._fieldOfViewY * scale, MathUtil::kPi - 0.01f );
        kicked._orthoHeight  = pose._orthoHeight * scale;
        return kicked;
    }
} // namespace sw
