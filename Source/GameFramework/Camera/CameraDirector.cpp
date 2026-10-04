#include "pch.h"

#include "GameFramework/Camera/CameraDirector.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    CameraDirector::CameraDirector()
        : _activeDef{}
        , _fromDef{}
        , _blend{}
        , _activePose{}
        , _fromPose{}
        , _outputPose{}
        , _blendElapsed{ 0.0f }
        , _blendWeight{ 1.0f }
        , _bHasActive{ SW_FALSE }
        , _bActivePoseFresh{ SW_FALSE }
        , _bBlending{ SW_FALSE }
        , _bFromLive{ SW_FALSE }
        , _bHasPose{ SW_FALSE }
    {
    }

    bool CameraDirector::activatePreset( const CameraPresetCatalog& catalog, const hashed_string& id )
    {
        const CameraPresetDef* pDef = catalog.findPreset( id );
        if ( pDef == nullptr )
            return false;
        const hashed_string fromId = _bHasActive == SW_TRUE ? _activeDef._id : hashed_string{};
        activatePreset( *pDef, catalog.getBlend( fromId, id ) );
        return true;
    }

    bool CameraDirector::activatePreset( const CameraPresetCatalog& catalog, const hashed_string& id, const CameraBlendSpec& blend )
    {
        const CameraPresetDef* pDef = catalog.findPreset( id );
        if ( pDef == nullptr )
            return false;
        activatePreset( *pDef, blend );
        return true;
    }

    void CameraDirector::activatePreset( const CameraPresetDef& def, const CameraBlendSpec& blend )
    {
        // 낸 포즈가 있으면 켠 프리셋도 있다 — 블렌드는 둘 다 있을 때만 한다.
        const bool bCut = _bHasPose == SW_FALSE || blend._curve == CameraBlendCurve::Cut || blend._duration <= 0.0f;
        if ( bCut )
        {
            _bBlending   = SW_FALSE;
            _blendWeight = 1.0f;
        }
        else
        {
            if ( _bBlending == SW_TRUE )
            {
                // 블렌드 도중 — 지금 내보내는 섞인 포즈를 고정해 출발점으로 둔다(나가는 둘을 다시 섞지 않는다).
                _fromPose  = _outputPose;
                _bFromLive = SW_FALSE;
            }
            else
            {
                // 나가는 프리셋은 대상을 계속 따라간다 — 켠 순간의 값은 지금 내보내는 포즈와 같다.
                _fromDef   = _activeDef;
                _fromPose  = _outputPose;
                _bFromLive = SW_TRUE;
            }
            _blend        = blend;
            _blendElapsed = 0.0f;
            _blendWeight  = 0.0f;
            _bBlending    = SW_TRUE;
        }
        _activeDef        = def;
        _bHasActive       = SW_TRUE;
        _bActivePoseFresh = SW_TRUE;
    }

    const CameraPose& CameraDirector::step( float32 deltaTime, const CameraTarget& target )
    {
        if ( _bHasActive == SW_FALSE )
            return _outputPose;
        const float32    elapsed   = MathUtil::max( 0.0f, deltaTime );
        const CameraPose evaluated = evaluatePreset( _activeDef, target );
        if ( _bActivePoseFresh == SW_TRUE )
        {
            _activePose       = evaluated;
            _bActivePoseFresh = SW_FALSE;
        }
        else
        {
            _activePose = dampPose( _activePose, evaluated, _activeDef._damping, elapsed );
        }

        if ( _bBlending == SW_TRUE )
        {
            if ( _bFromLive == SW_TRUE )
                _fromPose = dampPose( _fromPose, evaluatePreset( _fromDef, target ), _fromDef._damping, elapsed );
            _blendElapsed += elapsed;
            const float32 normalizedTime = _blend._duration > 0.0f ? MathUtil::saturate( _blendElapsed / _blend._duration ) : 1.0f;
            _blendWeight                 = evaluateBlendWeight( _blend, normalizedTime );
            _outputPose                  = blendPoses( _fromPose, _activePose, _blendWeight );
            if ( normalizedTime >= 1.0f )
            {
                _bBlending   = SW_FALSE;
                _bFromLive   = SW_FALSE;
                _blendWeight = 1.0f;
                _outputPose  = _activePose;
            }
        }
        else
        {
            _outputPose = _activePose;
        }
        _bHasPose = SW_TRUE;
        return _outputPose;
    }
} // namespace sw
