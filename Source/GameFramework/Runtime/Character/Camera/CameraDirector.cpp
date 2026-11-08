#include "pch.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirector.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    namespace
    {
        struct CameraDirectorInternal
        {
            static constexpr uint32 kStateTag      = FourCCUtil::make( "CDIR" );
            static constexpr uint32 kStateVersion  = 3;  ///< 3: 포즈의 후처리에 자동 노출 범위 · 속도가 붙었다
            static constexpr uint32 kLayerMinBytes = 64; ///< 층 하나의 최소 바이트(이름 · 블렌드 · 진행 · 포즈 둘 · 상태)

            static bool isCut( const BlendCurveDef& blend ) { return blend._curve == BlendCurve::Cut || blend._duration <= 0.0f; }

            static void serializePose( StateIO& io, CameraPose& pose )
            {
                float4 rotation{ pose._rotation._x, pose._rotation._y, pose._rotation._z, pose._rotation._w };
                io.value( pose._position );
                io.value( rotation );
                if ( io.isReading() )
                {
                    pose._rotation = quaternion( rotation );
                }
                io.value( pose._fieldOfViewY );
                io.value( pose._orthoHeight );
                io.value( pose._nearPlane );
                io.value( pose._farPlane );
                io.value( pose._depthOfField._focusDistance );
                io.value( pose._depthOfField._aperture );
                io.value( pose._depthOfField._sensorHeight );
                io.value( pose._lensShift );
                io.value( pose._postProcess._exposureCompensation );
                io.value( pose._postProcess._bloomScale );
                io.value( pose._postProcess._vignetteIntensity );
                io.value( pose._postProcess._saturation );
                io.value( pose._postProcess._contrast );
                io.value( pose._postProcess._letterboxAspectRatio );
                io.value( pose._postProcess._autoExposureMinEv );
                io.value( pose._postProcess._autoExposureMaxEv );
                io.value( pose._postProcess._autoExposureSpeedUp );
                io.value( pose._postProcess._autoExposureSpeedDown );
                io.value( pose._bOrthographic );
            }

            static void serializeBlend( StateIO& io, BlendCurveDef& blend )
            {
                io.enumValue( blend._curve, BlendCurve::Custom );
                io.value( blend._duration );
                io.value( blend._exponent );
                io.value( blend._springFrequency );
                io.value( blend._springDamping );
                io.list( blend._listCustomKey, 8, [&]( BlendCurveKey& key )
                {
                    io.value( key._time );
                    io.value( key._value );
                } );
            }

            static void serializeModeState( StateIO& io, CameraModeState& state )
            {
                io.value( state._panOffset );
                io.value( state._yawOffset );
                io.value( state._pitchOffset );
                io.value( state._rotateYaw );
                io.value( state._rotateYawShown );
                io.value( state._zoom );
                io.value( state._time );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CameraDirector::CameraDirector()
        : _listLayer{}
        , _impulseListener{}
        , _fieldOfViewKickListener{}
        , _listPostProcessOverlay{}
        , _emptyState{}
        , _outputPose{}
        , _userSettings{}
        , _pProbe{ nullptr }
        , _bHasPose{ SW_FALSE }
        , _bCutQueued{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool CameraDirector::activatePreset( const CameraPresetCatalog& catalog, const hashed_string& id )
    {
        const CameraPresetDef* pDef = catalog.find( id );
        if ( pDef == nullptr )
            return false;
        const hashed_string fromID = hasActivePreset() ? getActivePresetID() : hashed_string{};
        activatePreset( *pDef, catalog.getBlend( fromID, id ) );
        return true;
    }

    bool CameraDirector::activatePreset( const CameraPresetCatalog& catalog, const hashed_string& id, const BlendCurveDef& blend )
    {
        const CameraPresetDef* pDef = catalog.find( id );
        if ( pDef == nullptr )
            return false;
        activatePreset( *pDef, blend );
        return true;
    }

    void CameraDirector::activatePreset( const CameraPresetDef& def, const BlendCurveDef& blend )
    {
        Layer layer;
        layer._def    = def;
        layer._blend  = blend;
        layer._bFresh = SW_TRUE;
        // 낸 포즈가 없으면(처음) 섞을 화면이 없다 — 컷과 같이 바로 붙는다. 컷은 스택을 비운다.
        const bool bCut = _bHasPose == SW_FALSE || CameraDirectorInternal::isCut( blend );
        if ( bCut )
        {
            if ( _bHasPose == SW_TRUE )
                _bCutQueued = SW_TRUE;
            _listLayer.clear();
            layer._weight = 1.0f;
            _listLayer.push_back( layer );
            return;
        }
        // 스택이 비었는데 화면은 있다(복원 실패 뒤) — 지금 화면을 고정 층으로 깔고 거기서 섞는다.
        if ( _listLayer.empty() )
            _listLayer.push_back( makeFrozenLayer( _outputPose ) );
        layer._weight = 0.0f;
        _listLayer.push_back( layer );
        collapseOverflow();
    }

    void CameraDirector::refreshActivePreset( const CameraPresetDef& def )
    {
        if ( _listLayer.empty() == false )
            _listLayer.back()._def = def;
    }

    void CameraDirector::blendFromPose( const CameraPose& from, const BlendCurveDef& blend )
    {
        if ( hasActivePreset() == false )
            return;
        Layer top = _listLayer.back();
        _listLayer.clear();
        top._elapsed = 0.0f;
        if ( CameraDirectorInternal::isCut( blend ) )
        {
            top._weight = 1.0f;
            _listLayer.push_back( top );
            return;
        }
        top._blend  = blend;
        top._weight = 0.0f;
        _listLayer.push_back( makeFrozenLayer( from ) );
        _listLayer.push_back( top );
        // 화면은 이미 @p from 이다 — 다음 프레임부터 거기서 섞는다(처음 켠 프리셋처럼 바로 붙지 않게).
        _outputPose = from;
        _bHasPose   = SW_TRUE;
    }

    void CameraDirector::applyInput( const CameraModeInput& input, float32 deltaTime )
    {
        if ( hasActivePreset() )
            applyCameraInput( _listLayer.back()._def, input, deltaTime, _listLayer.back()._state );
    }

    CameraDirector::Layer CameraDirector::makeFrozenLayer( const CameraPose& pose )
    {
        Layer layer;
        layer._def._id    = hashed_string{};
        layer._pose       = pose;
        layer._dampedPose = pose;
        layer._bFrozen    = SW_TRUE;
        layer._weight     = 1.0f;
        return layer;
    }

    void CameraDirector::collapseOverflow()
    {
        while ( _listLayer.size() > kMaxLayerCount )
        {
            const Layer& bottom = _listLayer[0];
            const Layer& above  = _listLayer[1];
            // 아직 한 번도 풀지 않은 층(같은 프레임에 켰다 다시 바꾼 것)은 가중치 0 이라 섞인 화면에 없다 — 그냥 뺀다.
            if ( above._bFresh == SW_TRUE )
            {
                _listLayer.erase( _listLayer.begin() + 1 );
                continue;
            }
            const Layer merged = makeFrozenLayer( blendPoses( bottom._pose, above._pose, above._weight ) );
            _listLayer.erase( _listLayer.begin(), _listLayer.begin() + 2 );
            _listLayer.insert( _listLayer.begin(), merged );
        }
    }

    void CameraDirector::evaluateLayer( Layer& inoutLayer, float32 deltaTime, const CameraTarget& target ) const
    {
        if ( inoutLayer._bFrozen == SW_TRUE )
            return;
        const CameraPresetDef& def       = inoutLayer._def;
        const CameraPose       evaluated = _userSettings.applyFieldOfView( def._view._mode, evaluateCameraMode( def, target, deltaTime, inoutLayer._state, _pProbe ) );
        inoutLayer._dampedPose           = inoutLayer._bFresh == SW_TRUE ? evaluated : dampPose( inoutLayer._dampedPose, evaluated, def._damping, deltaTime );
        inoutLayer._bFresh               = SW_FALSE;
        CameraShakeOffset shake          = computeCameraNoise( def._noise, inoutLayer._state._time ) * _userSettings._shakeScale;
        if ( _userSettings._bHeadBob )
            shake += computeCameraHeadBob( def._headBob, target._focus, deltaTime, inoutLayer._state );
        inoutLayer._pose = applyCameraShake( inoutLayer._dampedPose, shake );
    }

    const CameraPose& CameraDirector::step( float32 deltaTime, const CameraTarget& target )
    {
        if ( _listLayer.empty() )
            return _outputPose;
        const float32 elapsed = MathUtil::max( 0.0f, deltaTime );
        for ( Layer& layer : _listLayer )
        {
            evaluateLayer( layer, elapsed, target );
        }

        // 아래에서 위로 섞는다. 다 섞인 층(가중치 1)이 있으면 그 아래는 화면에 없으므로 버린다.
        CameraPose blended      = _listLayer[0]._pose;
        size_t     settledIndex = 0;
        for ( size_t index = 1; index < _listLayer.size(); ++index )
        {
            Layer& layer = _listLayer[index];
            layer._elapsed += elapsed;
            const float32 normalizedTime = layer._blend._duration > 0.0f ? MathUtil::saturate( layer._elapsed / layer._blend._duration ) : 1.0f;
            layer._weight                = normalizedTime >= 1.0f ? 1.0f : evaluateBlendWeight( layer._blend, normalizedTime );
            blended                      = blendPoses( blended, layer._pose, layer._weight );
            if ( normalizedTime >= 1.0f )
                settledIndex = index;
        }
        if ( settledIndex > 0 )
            _listLayer.erase( _listLayer.begin(), _listLayer.begin() + static_cast<ptrdiff_t>( settledIndex ) );
        _bHasPose = SW_TRUE;

        // 충격은 섞인 포즈 위에 마지막으로 — 블렌드가 흔들림을 고정해 출발점에 싣지 않게.
        _impulseListener.step( elapsed );
        _outputPose = _impulseListener.isActive() ? applyCameraShake( blended, _impulseListener.computeOffset( blended._position ) * _userSettings._shakeScale ) : blended;
        // 시야각 킥도 섞인 포즈 위에 — 블렌드 출발점에 킥이 고정되지 않게.
        _fieldOfViewKickListener.step( elapsed );
        if ( _fieldOfViewKickListener.isActive() )
            _outputPose = _fieldOfViewKickListener.applyToPose( _outputPose );
        // 후처리 덧칠도 섞인 포즈 위에 — 프리셋 블렌드가 덧칠을 출발점에 싣지 않게.
        for ( const PostProcessOverlay& overlay : _listPostProcessOverlay )
            _outputPose._postProcess = blendPostProcess( _outputPose._postProcess, overlay._values, overlay._weight );
        return _outputPose;
    }

    void CameraDirector::setPostProcessOverlay( const GameObjectHandle& source, const CameraPostProcessValues& values, float32 weight )
    {
        for ( size_t index = 0; index < _listPostProcessOverlay.size(); ++index )
        {
            PostProcessOverlay& overlay = _listPostProcessOverlay[index];
            if ( overlay._source != source )
                continue;
            if ( weight <= 0.0f )
                _listPostProcessOverlay.erase( _listPostProcessOverlay.begin() + static_cast<ptrdiff_t>( index ) );
            else
            {
                overlay._values = values;
                overlay._weight = MathUtil::min( weight, 1.0f );
            }
            return;
        }
        if ( weight > 0.0f )
            _listPostProcessOverlay.push_back( PostProcessOverlay{ source, values, MathUtil::min( weight, 1.0f ) } );
    }

    const CameraPresetDef& CameraDirector::getActivePreset() const
    {
        static const CameraPresetDef s_emptyPreset{};
        return _listLayer.empty() ? s_emptyPreset : _listLayer.back()._def;
    }

    float32 CameraDirector::getBlendWeight() const
    {
        return _listLayer.size() > 1 ? _listLayer.back()._weight : 1.0f;
    }

    CameraModeState& CameraDirector::getModeState()
    {
        return _listLayer.empty() ? _emptyState : _listLayer.back()._state;
    }

    const CameraModeState& CameraDirector::getModeState() const
    {
        return _listLayer.empty() ? _emptyState : _listLayer.back()._state;
    }

    bool CameraDirector::consumeCut()
    {
        const bool bCut = _bCutQueued == SW_TRUE;
        _bCutQueued     = SW_FALSE;
        return bCut;
    }

    void CameraDirector::serializeState( StateIO& io, const CameraPresetCatalog* pCatalog )
    {
        using Internal = CameraDirectorInternal;
        io.header( Internal::kStateTag, Internal::kStateVersion );
        if ( io.isOk() == false )
            return;
        uint8      bHasPose   = _bHasPose;
        CameraPose outputPose = _outputPose;
        io.value( bHasPose );
        Internal::serializePose( io, outputPose );
        uint32 layerCount = static_cast<uint32>( _listLayer.size() );
        io.count( Internal::kLayerMinBytes, layerCount );
        if ( io.check( layerCount <= kMaxLayerCount ) == false )
            return;
        // 읽기는 임시에 다 받고 끝까지 맞을 때만 바꾼다 — 반쯤 읽은 스택을 남기지 않는다.
        vector<Layer> restoredLayer;
        if ( io.isReading() )
        {
            restoredLayer.resize( layerCount );
        }
        for ( Layer& layer : io.isWriting() ? _listLayer : restoredLayer )
        {
            hashed_string id = layer._def._id;
            io.value( id );
            io.value( layer._bFrozen );
            Internal::serializeBlend( io, layer._blend );
            io.value( layer._elapsed );
            io.value( layer._weight );
            Internal::serializePose( io, layer._dampedPose );
            Internal::serializePose( io, layer._pose );
            Internal::serializeModeState( io, layer._state );
            if ( io.isOk() == false )
                return;
            if ( io.isWriting() )
                continue;
            layer._bFresh               = SW_FALSE;
            const CameraPresetDef* pDef = layer._bFrozen == SW_TRUE ? nullptr : pCatalog->find( id );
            if ( pDef != nullptr )
            {
                layer._def = *pDef;
            }
            else
            {
                // 카탈로그 밖의 프리셋(코드로 만든 것)은 다시 풀 수 없다 — 마지막 포즈로 고정해 화면을 지킨다.
                layer._def._id = id;
                layer._bFrozen = SW_TRUE;
            }
        }
        if ( io.isWriting() )
            return;
        _listLayer  = std::move( restoredLayer );
        _outputPose = outputPose;
        _bHasPose   = bHasPose == SW_TRUE ? SW_TRUE : SW_FALSE;
        _bCutQueued = SW_FALSE;
        _impulseListener.clear();
        _fieldOfViewKickListener.clear();
    }

} // namespace sw
