#include "pch.h"

#include "Engine/Destruction/DestructionProfile.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct DestructionProfileInternal
        {
            static constexpr const utf8* kArrRootAttribute[]    = { "density", "physicsMaterial" };
            static constexpr const utf8* kArrStrainAttribute[]  = { "thresholds" };
            static constexpr const utf8* kArrLinkAttribute[]    = { "strength", "supportStrength" };
            static constexpr const utf8* kArrImpactAttribute[]  = { "impulseToStrain", "minImpulse", "radius" };
            static constexpr const utf8* kArrDebrisAttribute[]  = { "lifetime", "maxBodies", "smallVolume", "fadeTime", "sleepRemoveTime", "keepCollisionVolume", "hullShrink" };
            static constexpr const utf8* kArrNetworkAttribute[] = { "poseRate" };

            /** @brief 같은 원소가 두 번 나오면 오류입니다(뒤 것이 앞 것을 조용히 덮지 않게). */
            static bool claimElement( const XmlNode& node, CharacterDataReader& reader, uint8& inoutSeen )
            {
                if ( inoutSeen != SW_FALSE )
                {
                    reader.addError( node, "appears twice" );
                    return false;
                }
                inoutSeen = SW_TRUE;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    DestructionProfile::DestructionProfile()
        : _listStrainThreshold{ 60.0f, 90.0f, 40.0f }
        , _physicsMaterial{}
        , _density{ 2000.0f }
        , _linkStrength{ 400.0f }
        , _supportStrength{ 30000.0f }
        , _impulseToStrain{ 0.4f }
        , _minImpulse{ 40.0f }
        , _impactRadius{ 0.35f }
        , _debrisLifetime{ 10.0f }
        , _smallDebrisVolume{ 0.002f }
        , _fadeTime{ 0.5f }
        , _sleepRemoveTime{ 1.5f }
        , _keepCollisionVolume{ 0.05f }
        , _hullShrink{ 0.01f }
        , _maxDebrisBody{ 96 }
        , _networkPoseRate{ 10.0f }
    {
    }

    float32 DestructionProfile::getStrainThreshold( uint32 depth ) const
    {
        if ( _listStrainThreshold.empty() )
            return MathUtil::kMaxFloat;
        return _listStrainThreshold[MathUtil::min( static_cast<size_t>( depth ), _listStrainThreshold.size() - 1 )];
    }

    bool DestructionProfile::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        *this = DestructionProfile{};
        CharacterDataReader reader( sourceName );
        XmlDocument         document;
        XmlNode             root;
        if ( reader.parseRoot( document, xmlText, "DestructionProfile", root ) )
            readRoot( root, reader );
        if ( reader.hasError() )
            *this = DestructionProfile{};
        return reader.finish();
    }

    bool DestructionProfile::loadFromResource( string_view path )
    {
        *this = DestructionProfile{};
        CharacterDataReader reader( path );
        XmlDocument         document;
        XmlNode             root;
        if ( reader.loadRoot( document, path, "DestructionProfile", root ) )
            readRoot( root, reader );
        if ( reader.hasError() )
            *this = DestructionProfile{};
        return reader.finish();
    }

    void DestructionProfile::readRoot( const XmlNode& root, CharacterDataReader& reader )
    {
        using Internal = DestructionProfileInternal;
        reader.reportUnknownAttributes( root, Internal::kArrRootAttribute );
        _density           = reader.readFloat( root, "density", _density );
        _physicsMaterial   = reader.readName( root, "physicsMaterial", false );
        uint8 bStrainSeen  = SW_FALSE;
        uint8 bLinkSeen    = SW_FALSE;
        uint8 bImpactSeen  = SW_FALSE;
        uint8 bDebrisSeen  = SW_FALSE;
        uint8 bNetworkSeen = SW_FALSE;
        for ( XmlNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Strain", true ) )
            {
                if ( Internal::claimElement( child, reader, bStrainSeen ) == false )
                    continue;
                reader.reportUnknownAttributes( child, Internal::kArrStrainAttribute );
                const utf8* pText = child.findAttribute( "thresholds" );
                if ( pText == nullptr )
                {
                    reader.addError( child, "needs thresholds" );
                    continue;
                }
                vector<string_view> listToken;
                CharacterDataReader::splitTokens( pText, listToken );
                _listStrainThreshold.clear();
                for ( const string_view token : listToken )
                {
                    float32 value = 0.0f;
                    if ( StringUtil::parseFloat( token, value ) == false || value <= 0.0f )
                        reader.addError( child, string( "threshold '" ) + string( token ) + "' is not a positive number" );
                    else
                        _listStrainThreshold.push_back( value );
                }
            }
            else if ( StringUtil::equals( child.getName(), "Links", true ) )
            {
                if ( Internal::claimElement( child, reader, bLinkSeen ) == false )
                    continue;
                reader.reportUnknownAttributes( child, Internal::kArrLinkAttribute );
                _linkStrength    = reader.readFloat( child, "strength", _linkStrength );
                _supportStrength = reader.readFloat( child, "supportStrength", _supportStrength );
            }
            else if ( StringUtil::equals( child.getName(), "Impact", true ) )
            {
                if ( Internal::claimElement( child, reader, bImpactSeen ) == false )
                    continue;
                reader.reportUnknownAttributes( child, Internal::kArrImpactAttribute );
                _impulseToStrain = reader.readFloat( child, "impulseToStrain", _impulseToStrain );
                _minImpulse      = reader.readFloat( child, "minImpulse", _minImpulse );
                _impactRadius    = reader.readFloat( child, "radius", _impactRadius );
            }
            else if ( StringUtil::equals( child.getName(), "Debris", true ) )
            {
                if ( Internal::claimElement( child, reader, bDebrisSeen ) == false )
                    continue;
                reader.reportUnknownAttributes( child, Internal::kArrDebrisAttribute );
                _debrisLifetime      = reader.readFloat( child, "lifetime", _debrisLifetime );
                const int32 maxBody  = reader.readInt( child, "maxBodies", static_cast<int32>( _maxDebrisBody ) );
                _smallDebrisVolume   = reader.readFloat( child, "smallVolume", _smallDebrisVolume );
                _fadeTime            = reader.readFloat( child, "fadeTime", _fadeTime );
                _sleepRemoveTime     = reader.readFloat( child, "sleepRemoveTime", _sleepRemoveTime );
                _keepCollisionVolume = reader.readFloat( child, "keepCollisionVolume", _keepCollisionVolume );
                _hullShrink          = reader.readFloat( child, "hullShrink", _hullShrink );
                if ( maxBody < 0 )
                    reader.addError( child, "maxBodies must not be negative" );
                else
                    _maxDebrisBody = static_cast<uint32>( maxBody );
            }
            else if ( StringUtil::equals( child.getName(), "Network", true ) )
            {
                if ( Internal::claimElement( child, reader, bNetworkSeen ) == false )
                    continue;
                reader.reportUnknownAttributes( child, Internal::kArrNetworkAttribute );
                _networkPoseRate = reader.readFloat( child, "poseRate", _networkPoseRate );
            }
            else
            {
                reader.reportUnknownElement( child );
            }
        }
        const bool bPositive = _density > 0.0f && _linkStrength > 0.0f && _supportStrength > 0.0f && _impulseToStrain >= 0.0f && _minImpulse >= 0.0f &&
                               _impactRadius >= 0.0f && _debrisLifetime > 0.0f && _smallDebrisVolume >= 0.0f && _fadeTime >= 0.0f && _sleepRemoveTime >= 0.0f &&
                               _keepCollisionVolume >= 0.0f && _hullShrink >= 0.0f && _networkPoseRate > 0.0f;
        if ( bPositive == false )
            reader.addError( root, "density, strengths, lifetime and pose rate must be positive; the other numbers must not be negative" );
    }
} // namespace sw
