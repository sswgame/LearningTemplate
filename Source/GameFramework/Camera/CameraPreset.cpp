#include "pch.h"

#include "GameFramework/Camera/CameraPreset.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "CameraPreset" );

    namespace
    {
        struct CameraPresetInternal
        {
            static constexpr const utf8* kAnyPreset = "*";

            static constexpr const utf8* kArrPresetAttribute[]  = { "id" };
            static constexpr const utf8* kArrViewAttribute[]    = { "mode", "pitch", "yaw", "distance", "offset" };
            static constexpr const utf8* kArrLensAttribute[]    = { "fieldOfViewY", "orthoHeight", "near", "far", "orthographic" };
            static constexpr const utf8* kArrDampingAttribute[] = { "position", "orientation" };
            static constexpr const utf8* kArrBlendAttribute[]   = { "curve", "duration", "exponent", "springFrequency", "springDamping" };
            static constexpr const utf8* kArrRuleAttribute[]    = { "from", "to", "curve", "duration", "exponent", "springFrequency", "springDamping" };
            static constexpr const utf8* kArrKeyAttribute[]     = { "time", "value" };

            struct BlendKeyTimeLess
            {
                bool operator()( const CameraBlendKey& lhs, const CameraBlendKey& rhs ) const { return lhs._time < rhs._time; }
            };

            template <size_t Count>
            static bool isKnownName( const utf8* pName, const utf8* const ( &arrKnown )[Count] )
            {
                for ( const utf8* pKnown : arrKnown )
                {
                    if ( StringUtil::equals( pName, pKnown, true ) )
                        return true;
                }
                return false;
            }

            /** @brief 표에 없는 속성마다 경고합니다 — 이름을 바꾸고 데이터를 빠뜨리면 조용히 기본값이 되는 것을 막는다. */
            template <size_t Count>
            static void warnUnknownAttributes( const XmlNode& node, const utf8* const ( &arrKnown )[Count], string_view sourceName )
            {
                for ( XmlAttribute attribute = node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    if ( isKnownName( attribute.getName(), arrKnown ) == false )
                        SW_LOG_WARNING( "%#: <%#> has unknown attribute '%#'", sourceName, node.getName(), attribute.getName() );
                }
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
            static void readBlend( const XmlNode& node, CameraBlendSpec& inoutBlend, string_view sourceName )
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
                    CameraBlendKey key;
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
                    else
                    {
                        SW_LOG_WARNING( "%#: preset '%#' has unknown section <%#>", sourceName, inoutDef._id.c_str(), pName );
                    }
                }
            }

            static float32 computeDampingAlpha( float32 timeConstant, float32 deltaTime )
            {
                if ( timeConstant <= 0.0f || deltaTime <= 0.0f )
                    return timeConstant <= 0.0f ? 1.0f : 0.0f;
                return 1.0f - ::expf( -deltaTime / timeConstant );
            }

            static float3 rotateByYaw( const float3& value, float32 yaw ) { return float3::transform( value, quaternion::createFromYawPitchRoll( yaw, 0.0f, 0.0f ) ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CameraPose evaluatePreset( const CameraPresetDef& def, const CameraTarget& target )
    {
        const CameraViewDef& view = def._view;
        CameraPose           pose;
        pose._fieldOfViewY  = def._lens._fieldOfViewY;
        pose._orthoHeight   = def._lens._orthoHeight;
        pose._nearPlane     = def._lens._nearPlane;
        pose._farPlane      = def._lens._farPlane;
        pose._bOrthographic = def._lens._bOrthographic ? SW_TRUE : SW_FALSE;

        float32 yaw   = view._yaw;
        float32 pitch = view._pitch;
        float3  pivot = target._focus + view._offset;
        switch ( view._mode )
        {
            case CameraPresetMode::Fixed:
            {
                pose._rotation = quaternion::createFromYawPitchRoll( yaw, pitch, 0.0f );
                pose._position = view._offset;
                return pose;
            }
            case CameraPresetMode::FirstPerson:
            {
                yaw            = target._yaw + view._yaw;
                pitch          = target._pitch + view._pitch;
                pose._rotation = quaternion::createFromYawPitchRoll( yaw, pitch, 0.0f );
                pose._position = target._focus + CameraPresetInternal::rotateByYaw( view._offset, target._yaw );
                return pose;
            }
            case CameraPresetMode::Follow:
            {
                yaw   = target._yaw + view._yaw;
                pivot = target._focus + CameraPresetInternal::rotateByYaw( view._offset, target._yaw );
                break;
            }
            case CameraPresetMode::OrthoTopDown:
            {
                pose._bOrthographic = SW_TRUE;
                break;
            }
            case CameraPresetMode::Orbit:
            {
                break;
            }
        }
        // 피벗을 보는 방향(요 · 피치)의 반대쪽으로 거리만큼 물러난다 — 보는 방향의 요 · 피치가 곧 카메라 회전이다.
        pose._rotation       = quaternion::createFromYawPitchRoll( yaw, pitch, 0.0f );
        const float3 forward = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, pose._rotation );
        pose._position       = pivot - forward * view._distance;
        return pose;
    }

    CameraPose dampPose( const CameraPose& current, const CameraPose& target, const CameraDampingDef& damping, float32 deltaTime )
    {
        CameraPose pose = target;
        pose._position  = float3::lerp( current._position, target._position, CameraPresetInternal::computeDampingAlpha( damping._positionTime, deltaTime ) );
        pose._rotation  = quaternion::slerp( current._rotation, target._rotation, CameraPresetInternal::computeDampingAlpha( damping._orientationTime, deltaTime ) );
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
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        return GameDataXml::loadRoot( doc, path, "CameraPresets", root, sourceName ) && loadRoot( root, sourceName ) > 0;
    }

    bool CameraPresetCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        return GameDataXml::parseRoot( doc, xmlText, sourceName, "CameraPresets", root ) && loadRoot( root, sourceName ) > 0;
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
        _defaultBlend = CameraBlendSpec{};
    }

    const CameraBlendSpec& CameraPresetCatalog::getBlend( const hashed_string& from, const hashed_string& to ) const
    {
        const hashed_string    any( CameraPresetInternal::kAnyPreset );
        const CameraBlendSpec* pFromAny = nullptr;
        const CameraBlendSpec* pToAny   = nullptr;
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
