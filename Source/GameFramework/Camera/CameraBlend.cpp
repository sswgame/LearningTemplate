#include "pch.h"

#include "GameFramework/Camera/CameraBlend.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct CameraBlendInternal
        {
            static constexpr float32 kTwoPi = MathUtil::Pi * 2.0f;

            /** @brief 0 에서 출발해 1 로 다가가는 스프링의 자리입니다(속도 0 출발, 감쇠비 ≥ 1 이라 넘치지 않는다). */
            static float32 computeSpringPosition( float32 seconds, float32 angularFrequency, float32 dampingRatio )
            {
                const float32 omegaTime = angularFrequency * seconds;
                const float32 root      = MathUtil::sqrt( MathUtil::max( 0.0f, dampingRatio * dampingRatio - 1.0f ) );
                if ( root < 1.0e-3f )
                    return 1.0f - ( 1.0f + omegaTime ) * ::expf( -omegaTime );
                // 과감쇠 — 두 실근 r1 · r2 의 지수 합.
                const float32 rootSlow = -( dampingRatio - root );
                const float32 rootFast = -( dampingRatio + root );
                const float32 decay    = ( rootFast * ::expf( rootSlow * omegaTime ) - rootSlow * ::expf( rootFast * omegaTime ) ) / ( rootFast - rootSlow );
                return 1.0f - decay;
            }

            static float32 evaluateSpring( const CameraBlendSpec& spec, float32 normalizedTime )
            {
                const float32 angularFrequency = MathUtil::max( 0.01f, spec._springFrequency ) * kTwoPi;
                const float32 dampingRatio     = MathUtil::max( 1.0f, spec._springDamping );
                const float32 endPosition      = computeSpringPosition( spec._duration, angularFrequency, dampingRatio );
                if ( endPosition < 1.0e-6f )
                    return normalizedTime;
                // 끝의 자리로 나눠 1 에서 1 — 스프링이 다 붙기 전에 블렌드가 끝나도 튀지 않는다.
                return computeSpringPosition( normalizedTime * spec._duration, angularFrequency, dampingRatio ) / endPosition;
            }

            static float32 evaluateCustom( const vector<CameraBlendKey>& listKey, float32 normalizedTime )
            {
                if ( listKey.empty() )
                    return normalizedTime;
                if ( normalizedTime <= listKey.front()._time )
                    return listKey.front()._value;
                for ( size_t index = 1; index < listKey.size(); ++index )
                {
                    const CameraBlendKey& next = listKey[index];
                    if ( normalizedTime > next._time )
                        continue;
                    const CameraBlendKey& previous = listKey[index - 1];
                    const float32         span     = next._time - previous._time;
                    if ( span <= MathUtil::Epsilon )
                        return next._value;
                    return MathUtil::lerp( previous._value, next._value, ( normalizedTime - previous._time ) / span );
                }
                return listKey.back()._value;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 evaluateBlendWeight( const CameraBlendSpec& spec, float32 normalizedTime )
    {
        const float32 time     = MathUtil::saturate( normalizedTime );
        const float32 exponent = MathUtil::max( 0.01f, spec._exponent );
        switch ( spec._curve )
        {
            case CameraBlendCurve::Cut:
            {
                return time > 0.0f ? 1.0f : 0.0f;
            }
            case CameraBlendCurve::Linear:
            {
                return time;
            }
            case CameraBlendCurve::EaseIn:
            {
                return MathUtil::pow( time, exponent );
            }
            case CameraBlendCurve::EaseOut:
            {
                return 1.0f - MathUtil::pow( 1.0f - time, exponent );
            }
            case CameraBlendCurve::EaseInOut:
            {
                if ( time < 0.5f )
                    return 0.5f * MathUtil::pow( time * 2.0f, exponent );
                return 1.0f - 0.5f * MathUtil::pow( 2.0f - time * 2.0f, exponent );
            }
            case CameraBlendCurve::SmoothStep:
            {
                return time * time * ( 3.0f - 2.0f * time );
            }
            case CameraBlendCurve::Cubic:
            {
                if ( time < 0.5f )
                    return 4.0f * time * time * time;
                const float32 remaining = 2.0f - time * 2.0f;
                return 1.0f - remaining * remaining * remaining * 0.5f;
            }
            case CameraBlendCurve::Exponential:
            {
                return ( 1.0f - ::expf( -exponent * time ) ) / ( 1.0f - ::expf( -exponent ) );
            }
            case CameraBlendCurve::Spring:
            {
                return CameraBlendInternal::evaluateSpring( spec, time );
            }
            case CameraBlendCurve::Custom:
            {
                return CameraBlendInternal::evaluateCustom( spec._listCustomKey, time );
            }
        }
        return time;
    }

    CameraPose blendPoses( const CameraPose& from, const CameraPose& to, float32 weight )
    {
        CameraPose pose;
        pose._position      = float3::lerp( from._position, to._position, weight );
        pose._rotation      = quaternion::slerp( from._rotation, to._rotation, weight );
        pose._fieldOfViewY  = MathUtil::lerp( from._fieldOfViewY, to._fieldOfViewY, weight );
        pose._orthoHeight   = MathUtil::lerp( from._orthoHeight, to._orthoHeight, weight );
        pose._nearPlane     = MathUtil::lerp( from._nearPlane, to._nearPlane, weight );
        pose._farPlane      = MathUtil::lerp( from._farPlane, to._farPlane, weight );
        pose._bOrthographic = weight < 0.5f ? from._bOrthographic : to._bOrthographic;
        return pose;
    }
} // namespace sw

namespace sw
{
    CameraPoseBlender::CameraPoseBlender()
        : _blend{}
        , _fromPose{}
        , _outputPose{}
        , _elapsed{ 0.0f }
        , _weight{ 1.0f }
        , _bBlending{ SW_FALSE }
        , _bFromLive{ SW_FALSE }
        , _bHasPose{ SW_FALSE }
        , _bCutQueued{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool CameraPoseBlender::start( const CameraBlendSpec& blend )
    {
        const bool bCut = _bHasPose == SW_FALSE || blend._curve == CameraBlendCurve::Cut || blend._duration <= 0.0f;
        if ( bCut )
        {
            // 이미 화면에 무언가 있었으면 이것은 화면이 한 번에 바뀌는 컷이다 — 렌더러가 시간 누적을 버려야 한다.
            if ( _bHasPose == SW_TRUE )
                _bCutQueued = SW_TRUE;
            _bBlending = SW_FALSE;
            _bFromLive = SW_FALSE;
            _weight    = 1.0f;
            return false;
        }
        // 블렌드 도중이면 섞인 포즈를 고정하고(나가는 둘을 다시 섞지 않는다), 아니면 나가는 쪽을 살려 둔다. 어느 쪽이든 출발점은 지금 낸 포즈다.
        _bFromLive = _bBlending == SW_TRUE ? SW_FALSE : SW_TRUE;
        _fromPose  = _outputPose;
        _blend     = blend;
        _elapsed   = 0.0f;
        _weight    = 0.0f;
        _bBlending = SW_TRUE;
        return _bFromLive == SW_TRUE;
    }

    const CameraPose& CameraPoseBlender::step( float32 deltaTime, const CameraPose& incoming, const CameraPose* pLiveFrom )
    {
        if ( _bBlending == SW_TRUE )
        {
            if ( _bFromLive == SW_TRUE && pLiveFrom != nullptr )
                _fromPose = *pLiveFrom;
            _elapsed += MathUtil::max( 0.0f, deltaTime );
            const float32 normalizedTime = _blend._duration > 0.0f ? MathUtil::saturate( _elapsed / _blend._duration ) : 1.0f;
            _weight                      = evaluateBlendWeight( _blend, normalizedTime );
            _outputPose                  = blendPoses( _fromPose, incoming, _weight );
            if ( normalizedTime >= 1.0f )
            {
                _bBlending  = SW_FALSE;
                _bFromLive  = SW_FALSE;
                _weight     = 1.0f;
                _outputPose = incoming;
            }
        }
        else
        {
            _outputPose = incoming;
        }
        _bHasPose = SW_TRUE;
        return _outputPose;
    }

    bool CameraPoseBlender::consumeCut()
    {
        const bool bCut = _bCutQueued == SW_TRUE;
        _bCutQueued     = SW_FALSE;
        return bCut;
    }
} // namespace sw
