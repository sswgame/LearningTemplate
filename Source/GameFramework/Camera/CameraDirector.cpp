#include "pch.h"

#include "GameFramework/Camera/CameraDirector.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    CameraDirector::CameraDirector()
        : _activeDef{}
        , _fromDef{}
        , _activeState{}
        , _fromState{}
        , _blender{}
        , _impulseListener{}
        , _activePose{}
        , _fromPose{}
        , _outputPose{}
        , _pProbe{ nullptr }
        , _bHasActive{ SW_FALSE }
        , _bActivePoseFresh{ SW_FALSE }
        , _reserved{ 0 }
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
        // 나가는 쪽을 살려 두는 블렌드면 지금 프리셋과 그 모드 상태를 그대로 넘겨 계속 굴린다.
        if ( _blender.start( blend ) )
        {
            _fromDef   = _activeDef;
            _fromState = _activeState;
            _fromPose  = _activePose;
        }
        _activeDef = def;
        _activeState.reset();
        _bHasActive       = SW_TRUE;
        _bActivePoseFresh = SW_TRUE;
    }

    void CameraDirector::applyInput( const CameraModeInput& input, float32 deltaTime )
    {
        if ( _bHasActive == SW_TRUE )
            applyCameraInput( _activeDef, input, deltaTime, _activeState );
    }

    CameraPose CameraDirector::evaluateLayer( const CameraPresetDef& def, CameraModeState& inoutState, CameraPose& inoutDampedPose, bool bFresh,
                                              float32 deltaTime, const CameraTarget& target ) const
    {
        const CameraPose evaluated = evaluateCameraMode( def, target, deltaTime, inoutState, _pProbe );
        inoutDampedPose            = bFresh ? evaluated : dampPose( inoutDampedPose, evaluated, def._damping, deltaTime );
        return applyCameraShake( inoutDampedPose, computeCameraNoise( def._noise, inoutState._time ) );
    }

    const CameraPose& CameraDirector::step( float32 deltaTime, const CameraTarget& target )
    {
        if ( _bHasActive == SW_FALSE )
            return _outputPose;
        const float32    elapsed = MathUtil::max( 0.0f, deltaTime );
        const CameraPose active  = evaluateLayer( _activeDef, _activeState, _activePose, _bActivePoseFresh == SW_TRUE, elapsed, target );
        _bActivePoseFresh        = SW_FALSE;

        CameraPose        liveFrom{};
        const CameraPose* pLiveFrom = nullptr;
        if ( _blender.isFromLive() )
        {
            liveFrom  = evaluateLayer( _fromDef, _fromState, _fromPose, false, elapsed, target );
            pLiveFrom = &liveFrom;
        }
        const CameraPose& blended = _blender.step( elapsed, active, pLiveFrom );

        // 충격은 섞인 포즈 위에 마지막으로 — 블렌드가 흔들림을 고정해 출발점에 싣지 않게.
        _impulseListener.step( elapsed );
        _outputPose = _impulseListener.isActive() ? applyCameraShake( blended, _impulseListener.computeOffset( blended._position ) ) : blended;
        return _outputPose;
    }
} // namespace sw
