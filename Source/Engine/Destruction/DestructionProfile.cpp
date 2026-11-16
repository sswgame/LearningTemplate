#include "pch.h"

#include "Engine/Destruction/DestructionProfile.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct DestructionProfileInternal
        {
            static constexpr const utf8* kArrStrainAttribute[] = { "thresholds" };

            /** @brief 같은 원소가 두 번 나오면 오류입니다(뒤 것이 앞 것을 조용히 덮지 않게). */
            static bool claimElement( const XMLNode& node, CharacterDataReader& reader, uint8& inoutSeen )
            {
                if ( inoutSeen != SW_FALSE )
                {
                    reader.addError( "Destruction.ElementRepeated", node, "appears twice" );
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

    bool DestructionProfile::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = DestructionProfile{};
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, "DestructionProfile", root ) )
            readRoot( root, reader );
        if ( reader.hasError() )
            *this = DestructionProfile{};
        return reader.finish();
    }

    bool DestructionProfile::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        *this                  = DestructionProfile{};
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, assetID, "DestructionProfile", root ) )
            readRoot( root, reader );
        if ( reader.hasError() )
            *this = DestructionProfile{};
        return reader.finish();
    }

    void DestructionProfile::readRoot( const XMLNode& root, CharacterDataReader& reader )
    {
        using Internal = DestructionProfileInternal;
        DestructionRootParams rootParams;
        rootParams._density         = _density;
        rootParams._physicsMaterial = _physicsMaterial;
        (void)reader.readProperties( root, rootParams ); // 오류는 읽개의 목록에 쌓인다 — 칸 · 범위는 PROPERTY 가 정본이다
        _density           = rootParams._density;
        _physicsMaterial   = rootParams._physicsMaterial;
        uint8 bStrainSeen  = SW_FALSE;
        uint8 bLinkSeen    = SW_FALSE;
        uint8 bImpactSeen  = SW_FALSE;
        uint8 bDebrisSeen  = SW_FALSE;
        uint8 bNetworkSeen = SW_FALSE;
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Strain" ) )
            {
                if ( Internal::claimElement( child, reader, bStrainSeen ) == false )
                    continue;
                reader.reportUnknownAttributes( child, Internal::kArrStrainAttribute );
                const utf8* pText = child.findAttribute( "thresholds" );
                if ( pText == nullptr )
                {
                    reader.addError( "Destruction.ThresholdsMissing", child, "needs thresholds" );
                    continue;
                }
                _listStrainThreshold.clear();
                for ( const string_view token : StringUtil::split( pText, CharacterDataReader::kTokenSeparators, { ._bDropEmpty = true } ) )
                {
                    float32 value = 0.0f;
                    if ( StringUtil::parseFloat( token, value ) == false || value <= 0.0f )
                        reader.addError( "Destruction.ThresholdInvalid", child, string( "threshold '" ) + string( token ) + "' is not a positive number" );
                    else
                        _listStrainThreshold.push_back( value );
                }
            }
            else if ( StringUtil::equals( child.getName(), "Links" ) )
            {
                if ( Internal::claimElement( child, reader, bLinkSeen ) == false )
                    continue;
                DestructionLinkParams params;
                params._strength        = _linkStrength;
                params._supportStrength = _supportStrength;
                (void)reader.readProperties( child, params );
                _linkStrength    = params._strength;
                _supportStrength = params._supportStrength;
            }
            else if ( StringUtil::equals( child.getName(), "Impact" ) )
            {
                if ( Internal::claimElement( child, reader, bImpactSeen ) == false )
                    continue;
                DestructionImpactParams params;
                params._impulseToStrain = _impulseToStrain;
                params._minImpulse      = _minImpulse;
                params._radius          = _impactRadius;
                (void)reader.readProperties( child, params );
                _impulseToStrain = params._impulseToStrain;
                _minImpulse      = params._minImpulse;
                _impactRadius    = params._radius;
            }
            else if ( StringUtil::equals( child.getName(), "Debris" ) )
            {
                if ( Internal::claimElement( child, reader, bDebrisSeen ) == false )
                    continue;
                DestructionDebrisParams params;
                params._lifetime            = _debrisLifetime;
                params._maxBodies           = _maxDebrisBody;
                params._smallVolume         = _smallDebrisVolume;
                params._fadeTime            = _fadeTime;
                params._sleepRemoveTime     = _sleepRemoveTime;
                params._keepCollisionVolume = _keepCollisionVolume;
                params._hullShrink          = _hullShrink;
                (void)reader.readProperties( child, params );
                _debrisLifetime      = params._lifetime;
                _maxDebrisBody       = params._maxBodies;
                _smallDebrisVolume   = params._smallVolume;
                _fadeTime            = params._fadeTime;
                _sleepRemoveTime     = params._sleepRemoveTime;
                _keepCollisionVolume = params._keepCollisionVolume;
                _hullShrink          = params._hullShrink;
            }
            else if ( StringUtil::equals( child.getName(), "Network" ) )
            {
                if ( Internal::claimElement( child, reader, bNetworkSeen ) == false )
                    continue;
                DestructionNetworkParams params;
                params._poseRate = _networkPoseRate;
                (void)reader.readProperties( child, params );
                _networkPoseRate = params._poseRate;
            }
            else
            {
                reader.reportUnknownElement( child );
            }
        }
    }

    /** @brief 파괴 프로파일 데이터 파일 종류와 그 공유 캐시입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_SHARED_ASSET_CACHE( Destruction, DestructionProfile, DestructionProfile::kAssetType.withLoader( AssetTypeLoader::of<DestructionProfile>( MemoryTag::Physics ) ) );
} // namespace sw
