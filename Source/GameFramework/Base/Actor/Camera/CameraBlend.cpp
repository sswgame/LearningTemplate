#include "pch.h"

#include "GameFramework/Base/Actor/Camera/CameraBlend.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
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

    bool CameraPoseBlender::start( const BlendCurveDef& blend )
    {
        const bool bCut = _bHasPose == SW_FALSE || blend._curve == BlendCurve::Cut || blend._duration <= 0.0f;
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
