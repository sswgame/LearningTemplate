#include "pch.h"

#include "GameFramework/Base/Actor/Camera/CameraPreset.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "CameraPreset" );

    namespace
    {
        struct CameraPresetInternal
        {
            static constexpr const utf8* kAnyPreset = "*";

            static constexpr const utf8* kArrPresetAttribute[]    = { "id" };
            static constexpr const utf8* kArrViewAttribute[]      = { "mode", "pitch", "yaw", "distance", "offset", "aim", "lookAt" };
            static constexpr const utf8* kArrLensAttribute[]      = { "fieldOfViewY", "orthoHeight", "near", "far", "orthographic", "focalLength",
                                                                      "sensor", "focusDistance", "aperture", "shift", "dutch" };
            static constexpr const utf8* kArrDollyAttribute[]     = { "type", "closed", "pathOffset" };
            static constexpr const utf8* kArrPointAttribute[]     = { "position" };
            static constexpr const utf8* kArrPostAttribute[]      = { "exposure", "bloom", "vignette", "saturation", "contrast", "letterbox" };
            static constexpr const utf8* kArrDampingAttribute[]   = { "position", "orientation" };
            static constexpr const utf8* kArrBlendAttribute[]     = { "curve", "duration", "exponent", "springFrequency", "springDamping" };
            static constexpr const utf8* kArrRuleAttribute[]      = { "from", "to", "curve", "duration", "exponent", "springFrequency", "springDamping" };
            static constexpr const utf8* kArrKeyAttribute[]       = { "time", "value" };
            static constexpr const utf8* kArrInputAttribute[]     = { "sensitivity", "zoomStep", "panSpeed", "rotateStep", "rotateTime", "lookWhileHeld" };
            static constexpr const utf8* kArrConfinerAttribute[]  = { "pitchMin", "pitchMax", "zoomMin", "zoomMax", "boundsMin", "boundsMax" };
            static constexpr const utf8* kArrFramingAttribute[]   = { "compose", "screenX", "screenY", "deadZoneWidth", "deadZoneHeight", "softZoneWidth",
                                                                      "softZoneHeight", "damping", "lookAhead", "lookAheadSmoothing", "groupPadding" };
            static constexpr const utf8* kArrCollisionAttribute[] = { "radius", "minDistance", "recoverTime" };
            static constexpr const utf8* kArrNoiseAttribute[]     = { "position", "rotation", "frequency", "seed" };
            static constexpr const utf8* kArrSweepAttribute[]     = { "yaw", "period", "phase" };
            struct BlendKeyTimeLess
            {
                bool operator()( const BlendCurveKey& lhs, const BlendCurveKey& rhs ) const { return lhs._time < rhs._time; }
            };

            /** @brief 표에 없는 속성마다 경고합니다 — 이름을 바꾸고 데이터를 빠뜨리면 조용히 기본값이 되는 것을 막는다. */
            template <size_t Count>
            static void warnUnknownAttributes( const XMLNode& node, const utf8* const ( &arrKnown )[Count], string_view sourceName )
            {
                (void)XMLNameCheck::reportUnknownAttributes( node, arrKnown, sourceName, LogLevel::Warning ); // 경고만 하고 읽기를 잇는다
            }

            static float32 readDegrees( const XMLNode& node, const utf8* pName, float32 fallbackRadians )
            {
                return node.getAttributeFloat( pName, fallbackRadians * MathUtil::kRadianToDegree ) * MathUtil::kDegreeToRadian;
            }

            template <typename TEnum>
            static void readEnum( const XMLNode& node, const utf8* pName, TEnum& inoutValue, string_view sourceName )
            {
                const utf8* pText = node.findAttribute( pName );
                if ( pText == nullptr )
                    return;
                TEnum parsed{};
                if ( game::getSubsystem<TypeRegistry>().enumFromString( string_view( pText ), parsed ) )
                    inoutValue = parsed;
                else
                    SW_LOG_WARNING( "%#: <%#> has unknown %# '%#'", sourceName, node.getName(), pName, pText );
            }

            /** @brief 블렌드 속성과 `<Key>` 자식을 읽습니다. 빠진 칸은 @p inoutBlend 의 것이 남습니다. */
            static void readBlend( const XMLNode& node, BlendCurveDef& inoutBlend, string_view sourceName )
            {
                readEnum( node, "curve", inoutBlend._curve, sourceName );
                inoutBlend._duration        = MathUtil::max( 0.0f, node.getAttributeFloat( "duration", inoutBlend._duration ) );
                inoutBlend._exponent        = MathUtil::max( 0.01f, node.getAttributeFloat( "exponent", inoutBlend._exponent ) );
                inoutBlend._springFrequency = MathUtil::max( 0.01f, node.getAttributeFloat( "springFrequency", inoutBlend._springFrequency ) );
                inoutBlend._springDamping   = MathUtil::max( 1.0f, node.getAttributeFloat( "springDamping", inoutBlend._springDamping ) );
                XMLNode keyNode             = node.findChild( "Key" );
                if ( keyNode.isValid() )
                    inoutBlend._listCustomKey.clear();
                bool bSorted = true;
                for ( ; keyNode; keyNode = keyNode.findNextSibling( "Key" ) )
                {
                    warnUnknownAttributes( keyNode, kArrKeyAttribute, sourceName );
                    BlendCurveKey key;
                    key._time  = MathUtil::saturate( keyNode.getAttributeFloat( "time", 0.0f ) );
                    key._value = keyNode.getAttributeFloat( "value", key._time );
                    if ( inoutBlend._listCustomKey.empty() == false && key._time < inoutBlend._listCustomKey.back()._time )
                        bSorted = false;
                    inoutBlend._listCustomKey.push_back( key );
                }
                if ( bSorted == false )
                {
                    SW_LOG_WARNING( "%#: <%#> keys are not in time order - sorted", sourceName, node.getName() );
                    std::stable_sort( inoutBlend._listCustomKey.begin(), inoutBlend._listCustomKey.end(), BlendKeyTimeLess{} );
                }
            }

            /** @brief 시점 속성을 읽습니다. 벡터 속성이 틀리면 false 입니다(오류는 `GameDataXML` 이 속성 이름과 함께 알린다). */
            [[nodiscard]] static bool readView( const XMLNode& node, CameraViewDef& outView, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrViewAttribute, sourceName );
                readEnum( node, "mode", outView._mode, sourceName );
                outView._pitch    = readDegrees( node, "pitch", outView._pitch );
                outView._yaw      = readDegrees( node, "yaw", outView._yaw );
                outView._distance = MathUtil::max( 0.0f, node.getAttributeFloat( "distance", outView._distance ) );
                bool bValid       = GameDataXML::readOptionalFloat3( node, "offset", sourceName, outView._offset );
                readEnum( node, "aim", outView._aim, sourceName );
                if ( node.findAttribute( "lookAt" ) != nullptr )
                {
                    bValid = GameDataXML::readOptionalFloat3( node, "lookAt", sourceName, outView._lookAt ) && bValid;
                    // 점을 적었으면 그것을 본다 — `aim` 을 따로 적지 않아도 된다.
                    if ( node.findAttribute( "aim" ) == nullptr )
                        outView._aim = CameraAimMode::Point;
                }
                return bValid;
            }

            static void readInput( const XMLNode& node, CameraInputDef& outInput, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrInputAttribute, sourceName );
                outInput._lookSensitivity = MathUtil::max( 0.0f, node.getAttributeFloat( "sensitivity", outInput._lookSensitivity ) );
                outInput._zoomStep        = MathUtil::clamp( node.getAttributeFloat( "zoomStep", outInput._zoomStep ), 0.0f, 0.99f );
                outInput._panSpeed        = MathUtil::max( 0.0f, node.getAttributeFloat( "panSpeed", outInput._panSpeed ) );
                outInput._rotateStep      = readDegrees( node, "rotateStep", outInput._rotateStep );
                outInput._rotateTime      = MathUtil::max( 0.0f, node.getAttributeFloat( "rotateTime", outInput._rotateTime ) );
                outInput._bLookWhileHeld  = node.getAttributeBool( "lookWhileHeld", outInput._bLookWhileHeld );
            }

            /** @brief 제한 상자를 읽습니다. 벡터 속성이 틀리면 false 입니다. */
            [[nodiscard]] static bool readConfiner( const XMLNode& node, CameraConfinerDef& outConfiner, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrConfinerAttribute, sourceName );
                outConfiner._pitchMin = readDegrees( node, "pitchMin", outConfiner._pitchMin );
                outConfiner._pitchMax = MathUtil::max( outConfiner._pitchMin, readDegrees( node, "pitchMax", outConfiner._pitchMax ) );
                outConfiner._zoomMin  = MathUtil::max( 0.0f, node.getAttributeFloat( "zoomMin", outConfiner._zoomMin ) );
                outConfiner._zoomMax  = MathUtil::max( 0.0f, node.getAttributeFloat( "zoomMax", outConfiner._zoomMax ) );
                const bool bHasMin    = node.findAttribute( "boundsMin" ) != nullptr;
                const bool bHasMax    = node.findAttribute( "boundsMax" ) != nullptr;
                if ( bHasMin != bHasMax )
                    SW_LOG_WARNING( "%#: <Confiner> needs both boundsMin and boundsMax - the box is ignored", sourceName );
                bool bValid = true;
                if ( bHasMin && bHasMax )
                {
                    bValid               = GameDataXML::readOptionalFloat3( node, "boundsMin", sourceName, outConfiner._boundsMin );
                    bValid               = GameDataXML::readOptionalFloat3( node, "boundsMax", sourceName, outConfiner._boundsMax ) && bValid;
                    outConfiner._bBounds = true;
                }
                return bValid;
            }

            static void readFraming( const XMLNode& node, CameraFramingDef& outFraming, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrFramingAttribute, sourceName );
                outFraming._bCompose          = node.getAttributeBool( "compose", outFraming._bCompose );
                outFraming._screenPosition._x = MathUtil::saturate( node.getAttributeFloat( "screenX", outFraming._screenPosition._x ) );
                outFraming._screenPosition._y = MathUtil::saturate( node.getAttributeFloat( "screenY", outFraming._screenPosition._y ) );
                outFraming._deadZone._x       = MathUtil::saturate( node.getAttributeFloat( "deadZoneWidth", outFraming._deadZone._x ) );
                outFraming._deadZone._y       = MathUtil::saturate( node.getAttributeFloat( "deadZoneHeight", outFraming._deadZone._y ) );
                outFraming._softZone._x =
                    MathUtil::max( outFraming._deadZone._x, MathUtil::saturate( node.getAttributeFloat( "softZoneWidth", outFraming._softZone._x ) ) );
                outFraming._softZone._y =
                    MathUtil::max( outFraming._deadZone._y, MathUtil::saturate( node.getAttributeFloat( "softZoneHeight", outFraming._softZone._y ) ) );
                outFraming._damping            = MathUtil::max( 0.0f, node.getAttributeFloat( "damping", outFraming._damping ) );
                outFraming._lookAheadTime      = MathUtil::max( 0.0f, node.getAttributeFloat( "lookAhead", outFraming._lookAheadTime ) );
                outFraming._lookAheadSmoothing = MathUtil::max( 0.0f, node.getAttributeFloat( "lookAheadSmoothing", outFraming._lookAheadSmoothing ) );
                outFraming._groupPadding       = MathUtil::max( 0.0f, node.getAttributeFloat( "groupPadding", outFraming._groupPadding ) );
            }

            static void readCollision( const XMLNode& node, CameraCollisionDef& outCollision, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrCollisionAttribute, sourceName );
                outCollision._bEnabled    = true;
                outCollision._radius      = MathUtil::max( 0.0f, node.getAttributeFloat( "radius", outCollision._radius ) );
                outCollision._minDistance = MathUtil::max( 0.0f, node.getAttributeFloat( "minDistance", outCollision._minDistance ) );
                outCollision._recoverTime = MathUtil::max( 0.0f, node.getAttributeFloat( "recoverTime", outCollision._recoverTime ) );
            }

            /** @brief 흔들림을 읽습니다. 벡터 속성이 틀리면 false 입니다. */
            [[nodiscard]] static bool readNoise( const XMLNode& node, CameraNoiseDef& outNoise, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrNoiseAttribute, sourceName );
                bool   bValid          = GameDataXML::readOptionalFloat3( node, "position", sourceName, outNoise._positionAmplitude );
                float3 rotationDegrees = outNoise._rotationAmplitude * MathUtil::kRadianToDegree;
                bValid                 = GameDataXML::readOptionalFloat3( node, "rotation", sourceName, rotationDegrees ) && bValid;

                const float3 rotationRadians = rotationDegrees * MathUtil::kDegreeToRadian;
                outNoise._rotationAmplitude  = rotationRadians;
                outNoise._frequency          = MathUtil::max( 0.0f, node.getAttributeFloat( "frequency", outNoise._frequency ) );
                outNoise._seed               = static_cast<uint32>( MathUtil::max( 0, node.getAttributeInt( "seed", static_cast<int32>( outNoise._seed ) ) ) );
                return bValid;
            }

            static void readSweep( const XMLNode& node, CameraSweepDef& outSweep, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrSweepAttribute, sourceName );
                outSweep._yawAmplitude = MathUtil::max( 0.0f, readDegrees( node, "yaw", outSweep._yawAmplitude ) );
                outSweep._period       = MathUtil::max( 0.01f, node.getAttributeFloat( "period", outSweep._period ) );
                outSweep._phase        = MathUtil::saturate( node.getAttributeFloat( "phase", outSweep._phase ) );
            }

            /** @brief 렌즈를 읽습니다. 벡터 속성(`sensor` · `shift`)이 틀리면 false 입니다. */
            [[nodiscard]] static bool readLens( const XMLNode& node, CameraLensDef& outLens, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrLensAttribute, sourceName );
                outLens._fieldOfViewY  = MathUtil::clamp( readDegrees( node, "fieldOfViewY", outLens._fieldOfViewY ), 0.01f, 3.13f );
                outLens._orthoHeight   = MathUtil::max( 0.01f, node.getAttributeFloat( "orthoHeight", outLens._orthoHeight ) );
                outLens._nearPlane     = MathUtil::max( 0.001f, node.getAttributeFloat( "near", outLens._nearPlane ) );
                outLens._farPlane      = MathUtil::max( outLens._nearPlane + 0.01f, node.getAttributeFloat( "far", outLens._farPlane ) );
                outLens._bOrthographic = node.getAttributeBool( "orthographic", outLens._bOrthographic );
                outLens._focalLength   = MathUtil::max( 0.0f, node.getAttributeFloat( "focalLength", outLens._focalLength ) );
                bool bValid            = GameDataXML::readOptionalFloat2( node, "sensor", sourceName, outLens._sensorSize );
                outLens._sensorSize    = float2::max( outLens._sensorSize, float2{ 0.01f, 0.01f } );
                outLens._focusDistance = MathUtil::max( 0.0f, node.getAttributeFloat( "focusDistance", outLens._focusDistance ) );
                outLens._aperture      = MathUtil::max( 0.5f, node.getAttributeFloat( "aperture", outLens._aperture ) );
                bValid                 = GameDataXML::readOptionalFloat2( node, "shift", sourceName, outLens._lensShift ) && bValid;
                outLens._dutch         = readDegrees( node, "dutch", outLens._dutch );
                return bValid;
            }

            /** @brief 돌리 경로를 읽습니다. `<Point>` 의 `position` 은 필수이고, 없거나 틀리면 false 입니다. */
            [[nodiscard]] static bool readDolly( const XMLNode& node, CameraDollyDef& outDolly, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrDollyAttribute, sourceName );
                readEnum( node, "type", outDolly._type, sourceName );
                outDolly._bClosed    = node.getAttributeBool( "closed", outDolly._bClosed );
                outDolly._pathOffset = node.getAttributeFloat( "pathOffset", outDolly._pathOffset );
                XMLNode pointNode    = node.findChild( "Point" );
                if ( pointNode.isValid() )
                    outDolly._listPoint.clear();
                bool bValid = true;
                for ( ; pointNode; pointNode = pointNode.findNextSibling( "Point" ) )
                {
                    warnUnknownAttributes( pointNode, kArrPointAttribute, sourceName );
                    float3 position{};
                    if ( GameDataXML::readRequiredFloat3( pointNode, "position", sourceName, position ) )
                        outDolly._listPoint.push_back( position );
                    else
                        bValid = false;
                }
                if ( outDolly._listPoint.size() < 2 )
                    SW_LOG_WARNING( "%#: <Dolly> needs at least two <Point> - the camera stays at the view offset", sourceName );
                return bValid;
            }

            static void readPostProcess( const XMLNode& node, CameraPostProcessValues& outValues, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrPostAttribute, sourceName );
                outValues._exposureCompensation = node.getAttributeFloat( "exposure", outValues._exposureCompensation );
                outValues._bloomScale           = MathUtil::max( 0.0f, node.getAttributeFloat( "bloom", outValues._bloomScale ) );
                outValues._vignetteIntensity    = MathUtil::saturate( node.getAttributeFloat( "vignette", outValues._vignetteIntensity ) );
                outValues._saturation           = MathUtil::max( 0.0f, node.getAttributeFloat( "saturation", outValues._saturation ) );
                outValues._contrast             = MathUtil::max( 0.0f, node.getAttributeFloat( "contrast", outValues._contrast ) );
                outValues._letterboxAspectRatio = MathUtil::max( 0.0f, node.getAttributeFloat( "letterbox", outValues._letterboxAspectRatio ) );
            }

            static void readDamping( const XMLNode& node, CameraDampingDef& outDamping, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrDampingAttribute, sourceName );
                outDamping._positionTime    = MathUtil::max( 0.0f, node.getAttributeFloat( "position", outDamping._positionTime ) );
                outDamping._orientationTime = MathUtil::max( 0.0f, node.getAttributeFloat( "orientation", outDamping._orientationTime ) );
            }

            /** @brief 프리셋 하나를 읽습니다. 스키마로 엄격하게 읽는 섹션(`<HeadBob>`)에 오류가 있으면 false 입니다(오류는 `readFields` 가 이름과 함께 알린다). */
            [[nodiscard]] static bool readPreset( const XMLNode& node, CameraPresetDef& inoutDef, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrPresetAttribute, sourceName );
                bool bValid = true;
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    const utf8* pName = child.getName();
                    if ( StringUtil::equals( pName, "View", true ) )
                    {
                        bValid = readView( child, inoutDef._view, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "Lens", true ) )
                    {
                        bValid = readLens( child, inoutDef._lens, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "Damping", true ) )
                    {
                        readDamping( child, inoutDef._damping, sourceName );
                    }
                    else if ( StringUtil::equals( pName, "BlendIn", true ) )
                    {
                        warnUnknownAttributes( child, kArrBlendAttribute, sourceName );
                        readBlend( child, inoutDef._blendIn, sourceName );
                    }
                    else if ( StringUtil::equals( pName, "Input", true ) )
                    {
                        readInput( child, inoutDef._input, sourceName );
                    }
                    else if ( StringUtil::equals( pName, "Confiner", true ) )
                    {
                        bValid = readConfiner( child, inoutDef._confiner, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "Framing", true ) )
                    {
                        readFraming( child, inoutDef._framing, sourceName );
                    }
                    else if ( StringUtil::equals( pName, "Collision", true ) )
                    {
                        readCollision( child, inoutDef._collision, sourceName );
                    }
                    else if ( StringUtil::equals( pName, "Noise", true ) )
                    {
                        bValid = readNoise( child, inoutDef._noise, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "HeadBob", true ) )
                    {
                        // 스키마(Def 의 초기값 · Min)가 기본값과 범위를 정한다. 모르는 속성 · 범위 밖 값은 오류다.
                        bValid = GameDataXML::readFields( child, inoutDef._headBob, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "Sweep", true ) )
                    {
                        readSweep( child, inoutDef._sweep, sourceName );
                    }
                    else if ( StringUtil::equals( pName, "Dolly", true ) )
                    {
                        bValid = readDolly( child, inoutDef._dolly, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "PostProcess", true ) )
                    {
                        readPostProcess( child, inoutDef._postProcess, sourceName );
                    }
                    else
                    {
                        SW_LOG_WARNING( "%#: preset '%#' has unknown section <%#>", sourceName, inoutDef._id.c_str(), pName );
                    }
                }
                return bValid;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 CameraLensDef::computeFieldOfViewY() const
    {
        if ( _focalLength <= 0.0f )
            return _fieldOfViewY;
        // 핀홀 렌즈 — 센서 높이의 절반을 초점 길이로 나눈 각의 두 배다.
        return 2.0f * MathUtil::atan2( _sensorSize._y * 0.5f, _focalLength );
    }

    float32 computeDampingAlpha( float32 timeConstant, float32 deltaTime )
    {
        if ( timeConstant <= 0.0f || deltaTime <= 0.0f )
            return timeConstant <= 0.0f ? 1.0f : 0.0f;
        return 1.0f - ::expf( -deltaTime / timeConstant );
    }

    CameraPose dampPose( const CameraPose& current, const CameraPose& target, const CameraDampingDef& damping, float32 deltaTime )
    {
        CameraPose pose = target;
        pose._position  = float3::lerp( current._position, target._position, computeDampingAlpha( damping._positionTime, deltaTime ) );
        pose._rotation  = quaternion::slerp( current._rotation, target._rotation, computeDampingAlpha( damping._orientationTime, deltaTime ) );
        return pose;
    }

    CameraPresetCatalog::CameraPresetCatalog()
        : _listBlendRule{}
        , _defaultBlend{}
    {
    }

    void CameraPresetCatalog::addBlendRule( const CameraBlendRule& rule )
    {
        _listBlendRule.push_back( rule );
    }

    void CameraPresetCatalog::clear()
    {
        GameCatalog<CameraPresetDef>::clear();
        _listBlendRule.clear();
        _defaultBlend = BlendCurveDef{};
    }

    const BlendCurveDef& CameraPresetCatalog::getBlend( const hashed_string& from, const hashed_string& to ) const
    {
        const hashed_string  any( CameraPresetInternal::kAnyPreset );
        const BlendCurveDef* pFromAny = nullptr;
        const BlendCurveDef* pToAny   = nullptr;
        for ( const CameraBlendRule& rule : _listBlendRule )
        {
            const bool bFromMatches = rule._from == from;
            const bool bToMatches   = rule._to == to;
            if ( bFromMatches && bToMatches )
                return rule._blend;
            const bool bFromAnyMatch = pFromAny == nullptr && rule._from == any && bToMatches;
            if ( bFromAnyMatch )
                pFromAny = &rule._blend;
            const bool bToAnyMatch = pToAny == nullptr && bFromMatches && rule._to == any;
            if ( bToAnyMatch )
                pToAny = &rule._blend;
        }
        if ( pFromAny != nullptr )
            return *pFromAny;
        if ( pToAny != nullptr )
            return *pToAny;
        const CameraPresetDef* pTarget = find( to );
        return pTarget != nullptr ? pTarget->_blendIn : _defaultBlend;
    }

    uint32 CameraPresetCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 기본 블렌드를 먼저 — `<BlendIn>` 이 없는 프리셋이 이 값으로 시작한다.
        const XMLNode defaultNode = root.findChild( "DefaultBlend" );
        if ( defaultNode.isValid() )
        {
            CameraPresetInternal::warnUnknownAttributes( defaultNode, CameraPresetInternal::kArrBlendAttribute, sourceName );
            CameraPresetInternal::readBlend( defaultNode, _defaultBlend, sourceName );
        }

        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const utf8* pName = node.getName();
            if ( StringUtil::equals( pName, "Preset", true ) )
            {
                const utf8* pID = GameDataXML::findRequiredID( node, sourceName );
                if ( pID == nullptr )
                    continue;
                CameraPresetDef def;
                def._id      = hashed_string( pID );
                def._blendIn = _defaultBlend;
                if ( CameraPresetInternal::readPreset( node, def, sourceName ) == false )
                {
                    // 틀린 값으로 반쯤 읽은 프리셋을 쓰지 않는다 — 켜려는 쪽이 없는 id 로 알게 된다.
                    SW_LOG_ERROR( "%#: preset '%#' has invalid fields - skipped", sourceName, pID );
                    continue;
                }
                add( def );
                ++loadedCount;
            }
            else if ( StringUtil::equals( pName, "Blend", true ) )
            {
                CameraPresetInternal::warnUnknownAttributes( node, CameraPresetInternal::kArrRuleAttribute, sourceName );
                const utf8* pFrom = node.findAttribute( "from" );
                const utf8* pTo   = node.findAttribute( "to" );
                if ( StringUtil::isNullOrEmpty( pFrom ) || StringUtil::isNullOrEmpty( pTo ) )
                {
                    SW_LOG_WARNING( "%#: <Blend> needs both from and to - skipped", sourceName );
                    continue;
                }
                CameraBlendRule rule;
                rule._from  = hashed_string( pFrom );
                rule._to    = hashed_string( pTo );
                rule._blend = _defaultBlend;
                CameraPresetInternal::readBlend( node, rule._blend, sourceName );
                addBlendRule( rule );
            }
            else if ( StringUtil::equals( pName, "DefaultBlend", true ) == false )
            {
                SW_LOG_WARNING( "%#: unknown element <%#>", sourceName, pName );
            }
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Preset> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
