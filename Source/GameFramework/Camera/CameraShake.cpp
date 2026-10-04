#include "pch.h"

#include "GameFramework/Camera/CameraShake.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Camera/CameraPreset.h"

namespace sw
{
    namespace
    {
        struct CameraShakeInternal
        {
            /** @brief 채널마다 시드를 이만큼 떨어뜨린다 — 여섯 채널이 같은 잡음을 따라가지 않게. */
            static constexpr uint32 kChannelSeedStride = 0x9E3779B9u;
            /** @brief 채널마다 시간을 조금씩 어긋나게 한다 — 시드만 다르면 정수 자리(값 0)가 모든 채널에서 같은 때 온다. */
            static constexpr float32 kChannelTimeOffset = 17.31f;

            /** @brief 정수 격자점 하나의 기울기(−1..1)입니다. 시드와 자리를 섞은 정수 해시입니다. */
            static float32 computeGradient( int32 lattice, uint32 seed )
            {
                uint32 hash = static_cast<uint32>( lattice ) * 0x27D4EB2Du ^ seed * 0x165667B1u;
                hash ^= hash >> 15;
                hash *= 0x85EBCA6Bu;
                hash ^= hash >> 13;
                hash *= 0xC2B2AE35u;
                hash ^= hash >> 16;
                return static_cast<float32>( hash & 0xFFFFu ) / 32767.5f - 1.0f;
            }

            static float32 computeFade( float32 t ) { return t * t * t * ( t * ( t * 6.0f - 15.0f ) + 10.0f ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 computePerlinNoise1d( float32 position, uint32 seed )
    {
        const float32 floorValue = MathUtil::floor( position );
        const int32   lattice    = static_cast<int32>( floorValue );
        const float32 fraction   = position - floorValue;
        const float32 left       = CameraShakeInternal::computeGradient( lattice, seed ) * fraction;
        const float32 right      = CameraShakeInternal::computeGradient( lattice + 1, seed ) * ( fraction - 1.0f );
        // 1D 펄린의 값은 ±0.5 안이다 — 두 배 해서 진폭이 곧 최대 흔들림이 되게 한다.
        return MathUtil::lerp( left, right, CameraShakeInternal::computeFade( fraction ) ) * 2.0f;
    }

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
            arrValue[channel]     = computePerlinNoise1d( shifted, seed );
        }
        offset._position = float3{ arrValue[0] * noise._positionAmplitude._x, arrValue[1] * noise._positionAmplitude._y, arrValue[2] * noise._positionAmplitude._z };
        offset._rotation = float3{ arrValue[3] * noise._rotationAmplitude._x, arrValue[4] * noise._rotationAmplitude._y, arrValue[5] * noise._rotationAmplitude._z };
        return offset;
    }

    CameraPose applyCameraShake( const CameraPose& pose, const CameraShakeOffset& offset )
    {
        CameraPose   shaken  = pose;
        const float3 right   = float3::transform( float3{ 1.0f, 0.0f, 0.0f }, pose._rotation );
        const float3 up      = float3::transform( float3{ 0.0f, 1.0f, 0.0f }, pose._rotation );
        const float3 forward = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, pose._rotation );
        shaken._position     = pose._position + right * offset._position._x + up * offset._position._y + forward * offset._position._z;
        // 회전은 카메라 로컬에서 먼저 돈다 — 해밀턴 곱 `포즈 × 로컬` 은 로컬을 먼저 적용한다(`createFromYawPitchRoll` = 요 × 피치 와 같은 규약).
        const quaternion local = quaternion::createFromYawPitchRoll( offset._rotation._y, offset._rotation._x, offset._rotation._z );
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

    void CameraImpulseListener::step( float32 deltaTime )
    {
        const float32 elapsed = MathUtil::max( 0.0f, deltaTime );
        for ( CameraImpulse& impulse : _listImpulse )
            impulse._elapsed += elapsed;
        _listImpulse.erase( std::remove_if( _listImpulse.begin(), _listImpulse.end(), []( const CameraImpulse& impulse )
        { return impulse.isFinished(); } ),
                            _listImpulse.end() );
    }

    CameraShakeOffset CameraImpulseListener::computeOffset( const float3& listenerPosition ) const
    {
        CameraShakeOffset sum{};
        for ( const CameraImpulse& impulse : _listImpulse )
            sum += impulse.computeOffset( listenerPosition );
        return sum;
    }
} // namespace sw
