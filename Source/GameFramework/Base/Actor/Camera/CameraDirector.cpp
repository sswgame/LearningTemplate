#include "pch.h"

#include "GameFramework/Base/Actor/Camera/CameraDirector.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct CameraDirectorInternal
        {
            static constexpr uint32 kStateTag      = FourCcUtil::make( "CDIR" );
            static constexpr uint32 kStateVersion  = 2;  ///< 2: 포즈에 센서 높이(피사계 심도)가 붙었다
            static constexpr uint32 kLayerMinBytes = 64; ///< 층 하나의 최소 바이트(이름 · 블렌드 · 진행 · 포즈 둘 · 상태)

            static bool isCut( const BlendCurveDef& blend ) { return blend._curve == BlendCurve::Cut || blend._duration <= 0.0f; }

            static void writePose( Archive& outArchive, const CameraPose& pose )
            {
                outArchive << pose._position;
                outArchive << float4{ pose._rotation._x, pose._rotation._y, pose._rotation._z, pose._rotation._w };
                outArchive << pose._fieldOfViewY;
                outArchive << pose._orthoHeight;
                outArchive << pose._nearPlane;
                outArchive << pose._farPlane;
                outArchive << pose._depthOfField._focusDistance;
                outArchive << pose._depthOfField._aperture;
                outArchive << pose._depthOfField._sensorHeight;
                outArchive << pose._lensShift;
                outArchive << pose._postProcess._exposureCompensation;
                outArchive << pose._postProcess._bloomScale;
                outArchive << pose._postProcess._vignetteIntensity;
                outArchive << pose._postProcess._saturation;
                outArchive << pose._postProcess._contrast;
                outArchive << pose._postProcess._letterboxAspectRatio;
                outArchive << pose._bOrthographic;
            }

            static void readPose( Archive& archive, CameraPose& outPose )
            {
                float4 rotation{};
                archive >> outPose._position;
                archive >> rotation;
                outPose._rotation = quaternion( rotation );
                archive >> outPose._fieldOfViewY;
                archive >> outPose._orthoHeight;
                archive >> outPose._nearPlane;
                archive >> outPose._farPlane;
                archive >> outPose._depthOfField._focusDistance;
                archive >> outPose._depthOfField._aperture;
                archive >> outPose._depthOfField._sensorHeight;
                archive >> outPose._lensShift;
                archive >> outPose._postProcess._exposureCompensation;
                archive >> outPose._postProcess._bloomScale;
                archive >> outPose._postProcess._vignetteIntensity;
                archive >> outPose._postProcess._saturation;
                archive >> outPose._postProcess._contrast;
                archive >> outPose._postProcess._letterboxAspectRatio;
                archive >> outPose._bOrthographic;
            }

            static void writeBlend( Archive& outArchive, const BlendCurveDef& blend )
            {
                outArchive << static_cast<uint8>( blend._curve );
                outArchive << blend._duration;
                outArchive << blend._exponent;
                outArchive << blend._springFrequency;
                outArchive << blend._springDamping;
                outArchive << static_cast<uint32>( blend._listCustomKey.size() );
                for ( const BlendCurveKey& key : blend._listCustomKey )
                {
                    outArchive << key._time;
                    outArchive << key._value;
                }
            }

            [[nodiscard]] static bool readBlend( Archive& archive, BlendCurveDef& outBlend )
            {
                uint8 curve = 0;
                archive >> curve;
                archive >> outBlend._duration;
                archive >> outBlend._exponent;
                archive >> outBlend._springFrequency;
                archive >> outBlend._springDamping;
                if ( archive.isError() || curve > static_cast<uint8>( BlendCurve::Custom ) )
                    return false;
                outBlend._curve = static_cast<BlendCurve>( curve );
                uint32 keyCount = 0;
                if ( StateArchiveUtil::readCount( archive, 8, keyCount ) == false )
                    return false;
                outBlend._listCustomKey.resize( keyCount );
                for ( BlendCurveKey& key : outBlend._listCustomKey )
                {
                    archive >> key._time;
                    archive >> key._value;
                }
                return archive.isOk();
            }

            /** @brief 입력이 돌린 상태만 씁니다. 암 길이 · 조준 · look-ahead 는 다음 프레임이 다시 구한다. */
            static void writeModeState( Archive& outArchive, const CameraModeState& state )
            {
                outArchive << state._panOffset;
                outArchive << state._yawOffset;
                outArchive << state._pitchOffset;
                outArchive << state._rotateYaw;
                outArchive << state._rotateYawShown;
                outArchive << state._zoom;
                outArchive << state._time;
            }

            static void readModeState( Archive& archive, CameraModeState& outState )
            {
                archive >> outState._panOffset;
                archive >> outState._yawOffset;
                archive >> outState._pitchOffset;
                archive >> outState._rotateYaw;
                archive >> outState._rotateYawShown;
                archive >> outState._zoom;
                archive >> outState._time;
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
        return _outputPose;
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

    void CameraDirector::writeState( Archive& outArchive ) const
    {
        using Internal = CameraDirectorInternal;
        StateArchiveUtil::writeHeader( outArchive, Internal::kStateTag, Internal::kStateVersion );
        outArchive << static_cast<uint8>( _bHasPose );
        Internal::writePose( outArchive, _outputPose );
        outArchive << static_cast<uint32>( _listLayer.size() );
        for ( const Layer& layer : _listLayer )
        {
            StateArchiveUtil::writeName( outArchive, layer._def._id );
            outArchive << layer._bFrozen;
            Internal::writeBlend( outArchive, layer._blend );
            outArchive << layer._elapsed;
            outArchive << layer._weight;
            Internal::writePose( outArchive, layer._dampedPose );
            Internal::writePose( outArchive, layer._pose );
            Internal::writeModeState( outArchive, layer._state );
        }
    }

    bool CameraDirector::readState( Archive& archive, const CameraPresetCatalog& catalog )
    {
        using Internal = CameraDirectorInternal;
        if ( StateArchiveUtil::readHeader( archive, Internal::kStateTag, Internal::kStateVersion ) == false )
            return false;
        uint8      bHasPose = SW_FALSE;
        CameraPose outputPose;
        archive >> bHasPose;
        Internal::readPose( archive, outputPose );
        uint32 layerCount = 0;
        if ( StateArchiveUtil::readCount( archive, Internal::kLayerMinBytes, layerCount ) == false || layerCount > kMaxLayerCount )
            return false;
        // 임시에 다 읽고 끝까지 맞을 때만 바꾼다 — 반쯤 읽은 스택을 남기지 않는다.
        vector<Layer> listLayer( layerCount );
        for ( Layer& layer : listLayer )
        {
            hashed_string id;
            if ( StateArchiveUtil::readName( archive, id ) == false )
                return false;
            archive >> layer._bFrozen;
            if ( Internal::readBlend( archive, layer._blend ) == false )
                return false;
            archive >> layer._elapsed;
            archive >> layer._weight;
            Internal::readPose( archive, layer._dampedPose );
            Internal::readPose( archive, layer._pose );
            Internal::readModeState( archive, layer._state );
            if ( archive.isError() )
                return false;
            layer._bFresh               = SW_FALSE;
            const CameraPresetDef* pDef = layer._bFrozen == SW_TRUE ? nullptr : catalog.find( id );
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
        _listLayer  = std::move( listLayer );
        _outputPose = outputPose;
        _bHasPose   = bHasPose == SW_TRUE ? SW_TRUE : SW_FALSE;
        _bCutQueued = SW_FALSE;
        _impulseListener.clear();
        _fieldOfViewKickListener.clear();
        return true;
    }
} // namespace sw
