#include "pch.h"

#include "GameFramework/Camera/CameraPreset.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Utility/Xml/XmlDocument.h"
#include "Engine/Utility/Xml/XmlNameCheck.h"

#include "GameFramework/Data/GameDataXml.h"

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
            static constexpr const utf8* kArrLensAttribute[]      = { "fieldOfViewY", "orthoHeight", "near", "far", "orthographic" };
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
            static void warnUnknownAttributes( const XmlNode& node, const utf8* const ( &arrKnown )[Count], string_view sourceName )
            {
                (void)XmlNameCheck::reportUnknownAttributes( node, arrKnown, sourceName, LogLevel::Warning ); // 경고만 하고 읽기를 잇는다
            }

            static float32 readDegrees( const XmlNode& node, const utf8* pName, float32 fallbackRadians )
            {
                return node.getAttributeFloat( pName, fallbackRadians * MathUtil::RadianToDegree ) * MathUtil::DegreeToRadian;
            }

            template <typename TEnum>
            static void readEnum( const XmlNode& node, const utf8* pName, TEnum& inoutValue, string_view sourceName )
            {
                const utf8* pText = node.findAttribute( pName );
                if ( pText == nullptr )
                    return;
                TEnum parsed{};
                if ( engine::getTypeRegistry().enumFromString( string_view( pText ), parsed ) )
                    inoutValue = parsed;
                else
                    SW_LOG_WARNING( "%#: <%#> has unknown %# '%#'", sourceName, node.getName(), pName, pText );
            }

            /** @brief 블렌드 속성과 `<Key>` 자식을 읽습니다. 빠진 칸은 @p inoutBlend 의 것이 남습니다. */
            static void readBlend( const XmlNode& node, BlendCurveSpec& inoutBlend, string_view sourceName )
            {
                readEnum( node, "curve", inoutBlend._curve, sourceName );
                inoutBlend._duration        = MathUtil::max( 0.0f, node.getAttributeFloat( "duration", inoutBlend._duration ) );
                inoutBlend._exponent        = MathUtil::max( 0.01f, node.getAttributeFloat( "exponent", inoutBlend._exponent ) );
                inoutBlend._springFrequency = MathUtil::max( 0.01f, node.getAttributeFloat( "springFrequency", inoutBlend._springFrequency ) );
                inoutBlend._springDamping   = MathUtil::max( 1.0f, node.getAttributeFloat( "springDamping", inoutBlend._springDamping ) );
                XmlNode keyNode             = node.findChild( "Key" );
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

            static void readView( const XmlNode& node, CameraViewDef& outView, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrViewAttribute, sourceName );
                readEnum( node, "mode", outView._mode, sourceName );
                outView._pitch    = readDegrees( node, "pitch", outView._pitch );
                outView._yaw      = readDegrees( node, "yaw", outView._yaw );
                outView._distance = MathUtil::max( 0.0f, node.getAttributeFloat( "distance", outView._distance ) );
                outView._offset   = GameDataXml::parseFloat3( node.getAttributeText( "offset" ), outView._offset );
                readEnum( node, "aim", outView._aim, sourceName );
                if ( node.findAttribute( "lookAt" ) != nullptr )
                {
                    outView._lookAt = GameDataXml::parseFloat3( node.getAttributeText( "lookAt" ), outView._lookAt );
                    // 점을 적었으면 그것을 본다 — `aim` 을 따로 적지 않아도 된다.
                    if ( node.findAttribute( "aim" ) == nullptr )
                        outView._aim = CameraAimMode::Point;
                }
            }

            static void readInput( const XmlNode& node, CameraInputDef& outInput, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrInputAttribute, sourceName );
                outInput._lookSensitivity = MathUtil::max( 0.0f, node.getAttributeFloat( "sensitivity", outInput._lookSensitivity ) );
                outInput._zoomStep        = MathUtil::clamp( node.getAttributeFloat( "zoomStep", outInput._zoomStep ), 0.0f, 0.99f );
                outInput._panSpeed        = MathUtil::max( 0.0f, node.getAttributeFloat( "panSpeed", outInput._panSpeed ) );
                outInput._rotateStep      = readDegrees( node, "rotateStep", outInput._rotateStep );
                outInput._rotateTime      = MathUtil::max( 0.0f, node.getAttributeFloat( "rotateTime", outInput._rotateTime ) );
                outInput._bLookWhileHeld  = node.getAttributeBool( "lookWhileHeld", outInput._bLookWhileHeld );
            }

            static void readConfiner( const XmlNode& node, CameraConfinerDef& outConfiner, string_view sourceName )
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
                if ( bHasMin && bHasMax )
                {
                    outConfiner._boundsMin = GameDataXml::parseFloat3( node.getAttributeText( "boundsMin" ), outConfiner._boundsMin );
                    outConfiner._boundsMax = GameDataXml::parseFloat3( node.getAttributeText( "boundsMax" ), outConfiner._boundsMax );
                    outConfiner._bBounds   = true;
                }
            }

            static void readFraming( const XmlNode& node, CameraFramingDef& outFraming, string_view sourceName )
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

            static void readCollision( const XmlNode& node, CameraCollisionDef& outCollision, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrCollisionAttribute, sourceName );
                outCollision._bEnabled    = true;
                outCollision._radius      = MathUtil::max( 0.0f, node.getAttributeFloat( "radius", outCollision._radius ) );
                outCollision._minDistance = MathUtil::max( 0.0f, node.getAttributeFloat( "minDistance", outCollision._minDistance ) );
                outCollision._recoverTime = MathUtil::max( 0.0f, node.getAttributeFloat( "recoverTime", outCollision._recoverTime ) );
            }

            static void readNoise( const XmlNode& node, CameraNoiseDef& outNoise, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrNoiseAttribute, sourceName );
                outNoise._positionAmplitude  = GameDataXml::parseFloat3( node.getAttributeText( "position" ), outNoise._positionAmplitude );
                const float3 rotationDegrees = GameDataXml::parseFloat3( node.getAttributeText( "rotation" ), outNoise._rotationAmplitude * MathUtil::RadianToDegree );
                outNoise._rotationAmplitude  = rotationDegrees * MathUtil::DegreeToRadian;
                outNoise._frequency          = MathUtil::max( 0.0f, node.getAttributeFloat( "frequency", outNoise._frequency ) );
                outNoise._seed               = static_cast<uint32>( MathUtil::max( 0, node.getAttributeInt( "seed", static_cast<int32>( outNoise._seed ) ) ) );
            }

            static void readSweep( const XmlNode& node, CameraSweepDef& outSweep, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrSweepAttribute, sourceName );
                outSweep._yawAmplitude = MathUtil::max( 0.0f, readDegrees( node, "yaw", outSweep._yawAmplitude ) );
                outSweep._period       = MathUtil::max( 0.01f, node.getAttributeFloat( "period", outSweep._period ) );
                outSweep._phase        = MathUtil::saturate( node.getAttributeFloat( "phase", outSweep._phase ) );
            }

            static void readLens( const XmlNode& node, CameraLensDef& outLens, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrLensAttribute, sourceName );
                outLens._fieldOfViewY  = MathUtil::clamp( readDegrees( node, "fieldOfViewY", outLens._fieldOfViewY ), 0.01f, 3.13f );
                outLens._orthoHeight   = MathUtil::max( 0.01f, node.getAttributeFloat( "orthoHeight", outLens._orthoHeight ) );
                outLens._nearPlane     = MathUtil::max( 0.001f, node.getAttributeFloat( "near", outLens._nearPlane ) );
                outLens._farPlane      = MathUtil::max( outLens._nearPlane + 0.01f, node.getAttributeFloat( "far", outLens._farPlane ) );
                outLens._bOrthographic = node.getAttributeBool( "orthographic", outLens._bOrthographic );
            }

            static void readDamping( const XmlNode& node, CameraDampingDef& outDamping, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrDampingAttribute, sourceName );
                outDamping._positionTime    = MathUtil::max( 0.0f, node.getAttributeFloat( "position", outDamping._positionTime ) );
                outDamping._orientationTime = MathUtil::max( 0.0f, node.getAttributeFloat( "orientation", outDamping._orientationTime ) );
            }

            static void readPreset( const XmlNode& node, CameraPresetDef& inoutDef, string_view sourceName )
            {
                warnUnknownAttributes( node, kArrPresetAttribute, sourceName );
                for ( XmlNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    const utf8* pName = child.getName();
                    if ( StringUtil::equals( pName, "View", true ) )
                    {
                        readView( child, inoutDef._view, sourceName );
                    }
                    else if ( StringUtil::equals( pName, "Lens", true ) )
                    {
                        readLens( child, inoutDef._lens, sourceName );
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
                        readConfiner( child, inoutDef._confiner, sourceName );
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
                        readNoise( child, inoutDef._noise, sourceName );
                    }
                    else if ( StringUtil::equals( pName, "Sweep", true ) )
                    {
                        readSweep( child, inoutDef._sweep, sourceName );
                    }
                    else
                    {
                        SW_LOG_WARNING( "%#: preset '%#' has unknown section <%#>", sourceName, inoutDef._id.c_str(), pName );
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
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
        : _catalog{}
        , _listBlendRule{}
        , _defaultBlend{}
    {
    }

    bool CameraPresetCatalog::loadFromResource( string_view path )
    {
        return GameDataXml::loadFile( *this, &CameraPresetCatalog::loadRoot, path, "CameraPresets" );
    }

    bool CameraPresetCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        return GameDataXml::loadText( *this, &CameraPresetCatalog::loadRoot, xmlText, sourceName, "CameraPresets" );
    }

    void CameraPresetCatalog::addPreset( const CameraPresetDef& def )
    {
        (void)_catalog.add( def ); // 빈 id 는 카탈로그가 거른다
    }

    void CameraPresetCatalog::addBlendRule( const CameraBlendRule& rule )
    {
        _listBlendRule.push_back( rule );
    }

    void CameraPresetCatalog::clear()
    {
        _catalog.clear();
        _listBlendRule.clear();
        _defaultBlend = BlendCurveSpec{};
    }

    const BlendCurveSpec& CameraPresetCatalog::getBlend( const hashed_string& from, const hashed_string& to ) const
    {
        const hashed_string   any( CameraPresetInternal::kAnyPreset );
        const BlendCurveSpec* pFromAny = nullptr;
        const BlendCurveSpec* pToAny   = nullptr;
        for ( const CameraBlendRule& rule : _listBlendRule )
        {
            const bool bFromMatches = rule._from == from;
            const bool bToMatches   = rule._to == to;
            if ( bFromMatches && bToMatches )
                return rule._blend;
            if ( pFromAny == nullptr && rule._from == any && bToMatches )
                pFromAny = &rule._blend;
            if ( pToAny == nullptr && bFromMatches && rule._to == any )
                pToAny = &rule._blend;
        }
        if ( pFromAny != nullptr )
            return *pFromAny;
        if ( pToAny != nullptr )
            return *pToAny;
        const CameraPresetDef* pTarget = findPreset( to );
        return pTarget != nullptr ? pTarget->_blendIn : _defaultBlend;
    }

    uint32 CameraPresetCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        // 기본 블렌드를 먼저 — `<BlendIn>` 이 없는 프리셋이 이 값으로 시작한다.
        const XmlNode defaultNode = root.findChild( "DefaultBlend" );
        if ( defaultNode.isValid() )
        {
            CameraPresetInternal::warnUnknownAttributes( defaultNode, CameraPresetInternal::kArrBlendAttribute, sourceName );
            CameraPresetInternal::readBlend( defaultNode, _defaultBlend, sourceName );
        }

        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const utf8* pName = node.getName();
            if ( StringUtil::equals( pName, "Preset", true ) )
            {
                const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
                if ( pId == nullptr )
                    continue;
                CameraPresetDef def;
                def._id      = hashed_string( pId );
                def._blendIn = _defaultBlend;
                CameraPresetInternal::readPreset( node, def, sourceName );
                addPreset( def );
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
