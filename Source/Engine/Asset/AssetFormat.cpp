#include "pch.h"

#include "Engine/Asset/AssetFormat.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"

namespace sw
{
    namespace
    {
        struct AssetFormatInternal
        {
            /** @brief @p type 의 첫 저작 소스 접미사(원본 이름 `.scene.xml`)입니다. 쿠킹하지 않는 종류면 빈 글입니다. */
            static string_view findCanonicalSource( const AssetTypeDef& type )
            {
                for ( uint32 index = 0; index < type._suffixCount; ++index )
                {
                    if ( type._pSuffix[index]._role == AssetSuffixRole::CookSource )
                        return type._pSuffix[index]._text;
                }
                return {};
            }

            /** @brief 맞은 접미사가 @p role 이면 true 입니다. */
            static bool hasRole( const AssetSuffixMatch& match, AssetSuffixRole role ) { return match.isMatched() && match._pSuffix->_role == role; }
        };
    } // namespace

    string AssetCookPath::toCookedPath( string_view path )
    {
        const AssetSuffixMatch match     = AssetTypeRegistry::findSuffix( path );
        const bool             bCookName = match.isMatched() && match._pSuffix->_role != AssetSuffixRole::Plain && match._pType->_cookedSuffix.empty() == false;
        if ( bCookName == false )
            return {};
        string cooked( path.substr( 0, path.size() - match._pSuffix->_text.size() ) );
        cooked += match._pType->_cookedSuffix;
        return cooked;
    }

    bool AssetCookPath::isCookableSource( string_view path )
    {
        return AssetFormatInternal::hasRole( AssetTypeRegistry::findSuffix( path ), AssetSuffixRole::CookSource );
    }

    bool AssetCookPath::isCookedOutput( string_view path )
    {
        return AssetFormatInternal::hasRole( AssetTypeRegistry::findSuffix( path ), AssetSuffixRole::Cooked );
    }

    bool AssetCookPath::isCookableSource( string_view path, const AssetTypeDef& type )
    {
        const AssetSuffixMatch match = AssetTypeRegistry::findSuffix( path );
        return AssetFormatInternal::hasRole( match, AssetSuffixRole::CookSource ) && match._pType->_name == type._name;
    }

    void AssetCookPath::appendSourceSuffixes( const AssetTypeDef& type, vector<string_view>& outListSuffix )
    {
        for ( uint32 index = 0; index < type._suffixCount; ++index )
        {
            if ( type._pSuffix[index]._role == AssetSuffixRole::CookSource )
                outListSuffix.push_back( type._pSuffix[index]._text );
        }
    }

    bool AssetCookPath::isInInactiveGamePack( string_view relativePath, string_view activePackRoot )
    {
        if ( activePackRoot.empty() || FileUtil::startsWithPathComponent( relativePath, constant::path::kGamePack ) == false )
            return false;
        return FileUtil::startsWithPathComponent( relativePath, activePackRoot ) == false;
    }

    string AssetCookPath::toSourcePath( string_view path, const AssetTypeDef& type )
    {
        if ( path.empty() || isCookableSource( path, type ) )
            return string( path );

        const string_view canonical = AssetFormatInternal::findCanonicalSource( type );
        if ( canonical.empty() )
            return string( path );

        // 같은 종류의 쿠킹하지 않는 이름(쿠킹본 · 확장자 없는 이름)은 그 접미사를 원본으로 바꾼다.
        const AssetSuffixMatch match       = AssetTypeRegistry::findSuffix( path );
        const bool             bSameType   = match.isMatched() && match._pType->_name == type._name;
        const bool             bCookedName = bSameType && ( match._pSuffix->_role == AssetSuffixRole::Cooked || match._pSuffix->_role == AssetSuffixRole::CookedAlias );
        if ( bCookedName )
            return string( path.substr( 0, path.size() - match._pSuffix->_text.size() ) ) + string( canonical );

        // 끝의 확장자가 원본의 마지막 확장자(`.xml`)와 같으면 그 자리에 넣는다 — `level.xml` → `level.scene.xml`.
        const string_view lastExtension = canonical.substr( canonical.rfind( '.' ) );
        if ( StringUtil::endsWith( path, lastExtension, true ) )
            return string( path.substr( 0, path.size() - lastExtension.size() ) ) + string( canonical );
        return string( path ) + string( canonical );
    }

    AssetFormatVersion AssetXMLFormat::readXMLVersion( XMLNode root )
    {
        if ( root.isValid() == false )
            return kUnversioned;
        const utf8* pAttr = root.findAttribute( kXMLAttrName );
        if ( StringUtil::isNullOrEmpty( pAttr ) == false )
        {
            // 읽지 못했거나 버전 타입에 담기지 않으면 **가장 큰 버전**으로 본다 — "지원하는 것보다 새 형식" 으로 거절된다. 잘라 담으면
            // "4294967296" · "-1" 이 0(현재 버전)으로 읽혀 그 거절을 지나친다.
            uint64 ver{ kUnversioned };
            if ( StringUtil::parseUint64( pAttr, ver, 10 ) == false || ver > static_cast<uint64>( std::numeric_limits<AssetFormatVersion>::max() ) )
                return std::numeric_limits<AssetFormatVersion>::max();
            return static_cast<AssetFormatVersion>( ver );
        }
        return kUnversioned;
    }

    void AssetXMLFormat::writeXMLVersion( XMLNode root, AssetFormatVersion version )
    {
        if ( root.isValid() == false )
            return;

        const string versionStr = sw::to_string( static_cast<uint32>( version ) );

        if ( root.findAttribute( kXMLAttrName ) != nullptr )
        {
            root.setAttribute( kXMLAttrName, versionStr.c_str() );
            return;
        }
        root.appendAttribute( kXMLAttrName, versionStr.c_str() );
    }

    bool AssetXMLFormat::upgradeToCurrentXMLVersion( const AssetTypeDef& type, XMLNode root )
    {
        if ( root.isValid() == false )
            return false;
        const AssetFormatVersion version = readXMLVersion( root );
        if ( version == type._formatVersion )
            return true;
        FormatUpgradePayload payload;
        payload._root = root;
        if ( FormatUpgradeStep::upgrade( FormatUpgradeDomain::AssetXML, type._name, version, type._formatVersion, payload ) == false )
        {
            SW_LOG_ERROR_ID( "AssetFormat.AssetFormatVersionDoesNot", "%# asset formatVersion %# could not be raised to the supported %# - add the missing upgrade step or rewrite the data", type._name, version,
                             type._formatVersion );
            return false;
        }
        writeXMLVersion( root, type._formatVersion ); // 올린 글은 현재 판이다 — 다시 저장하면 새 판으로 남는다
        return true;
    }
} // namespace sw
