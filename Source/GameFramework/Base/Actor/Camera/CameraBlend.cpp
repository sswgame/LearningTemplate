#include "pch.h"

#include "GameFramework/Base/Actor/Camera/CameraBlend.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct CameraBlendInternal
        {
            static CameraPostProcessValues blendPostProcess( const CameraPostProcessValues& from, const CameraPostProcessValues& to, float32 weight )
            {
                CameraPostProcessValues values;
                values._exposureCompensation = MathUtil::lerp( from._exposureCompensation, to._exposureCompensation, weight );
                values._bloomScale           = MathUtil::lerp( from._bloomScale, to._bloomScale, weight );
                values._vignetteIntensity    = MathUtil::lerp( from._vignetteIntensity, to._vignetteIntensity, weight );
                values._saturation           = MathUtil::lerp( from._saturation, to._saturation, weight );
                values._contrast             = MathUtil::lerp( from._contrast, to._contrast, weight );
                // 레터박스는 양쪽에 있을 때만 비율을 섞는다. 한쪽에만 있으면 0(없음)과 섞을 비율이 없으므로 투영처럼 절반에서 바꾼다.
                const bool bBoth             = from._letterboxAspectRatio > 0.0f && to._letterboxAspectRatio > 0.0f;
                values._letterboxAspectRatio = bBoth ? MathUtil::lerp( from._letterboxAspectRatio, to._letterboxAspectRatio, weight )
                                                     : ( weight < 0.5f ? from._letterboxAspectRatio : to._letterboxAspectRatio );
                return values;
            }
        };
    } // namespace
} // namespace sw

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
        pose._focusDistance = MathUtil::lerp( from._focusDistance, to._focusDistance, weight );
        pose._aperture      = MathUtil::lerp( from._aperture, to._aperture, weight );
        pose._lensShift     = float2::lerp( from._lensShift, to._lensShift, weight );
        pose._postProcess   = CameraBlendInternal::blendPostProcess( from._postProcess, to._postProcess, weight );
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

    void CameraPoseBlender::startFromPose( const CameraPose& from, const BlendCurveDef& blend )
    {
        _bFromLive = SW_FALSE;
        if ( blend._curve == BlendCurve::Cut || blend._duration <= 0.0f )
        {
            _bBlending = SW_FALSE;
            _weight    = 1.0f;
            return;
        }
        _fromPose   = from;
        _outputPose = from;
        _blend      = blend;
        _elapsed    = 0.0f;
        _weight     = 0.0f;
        _bBlending  = SW_TRUE;
        _bHasPose   = SW_TRUE;
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
