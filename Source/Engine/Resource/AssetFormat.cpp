#include "pch.h"

#include "Engine/Resource/AssetFormat.h"

#include "Core/String/StringUtil.h"

#include "sw/config/CookContract.gen.h"

namespace sw
{
    namespace
    {
        struct AssetFormatInternal
        {
            /**
             * @brief 소스 접미사 → 쿠킹본 접미사와 그 에셋 종류. 긴 것이 먼저다(`.scene.xml` 이 `.scene` 보다 먼저 맞아야 한다).
             * @details 줄은 쿠킹 표(`Config/Engine/CookContract.json` → `SW_COOK_SUFFIX_TABLE`)에서 온다 — Python 쿠커가 같은 표로
             *          산출물을 알아본다. 에디터의 씬 · 프리팹 판정도 이 표다(`isCookableSource( path, kind )`).
             */
            struct CookSuffix
            {
                string_view _source;
                string_view _cooked;
                AssetKind   _kind;
                bool        _bSource;
            };
            static constexpr CookSuffix kArrCookSuffix[] = {
#define SW_ASSET_COOK_SUFFIX_ROW( SourceSuffix, CookedSuffix, Kind, bAuthoringSource ) { SourceSuffix, CookedSuffix, AssetKind::Kind, bAuthoringSource },
                SW_COOK_SUFFIX_TABLE( SW_ASSET_COOK_SUFFIX_ROW )
#undef SW_ASSET_COOK_SUFFIX_ROW
            };
        };
    } // namespace

    SW_LOG_CALLER( "AssetFormat" );

    string AssetCookPath::toCookedPath( string_view path )
    {
        for ( const AssetFormatInternal::CookSuffix& suffix : AssetFormatInternal::kArrCookSuffix )
        {
            if ( StringUtil::endsWith( path, suffix._source, true ) == false )
                continue;
            string cooked( path.substr( 0, path.size() - suffix._source.size() ) );
            cooked += suffix._cooked;
            return cooked;
        }
        return {};
    }

    bool AssetCookPath::isCookableSource( string_view path )
    {
        for ( const AssetFormatInternal::CookSuffix& suffix : AssetFormatInternal::kArrCookSuffix )
        {
            if ( suffix._bSource && StringUtil::endsWith( path, suffix._source, true ) )
                return true;
        }
        return false;
    }

    bool AssetCookPath::isCookableSource( string_view path, AssetKind kind )
    {
        for ( const AssetFormatInternal::CookSuffix& suffix : AssetFormatInternal::kArrCookSuffix )
        {
            if ( suffix._bSource && suffix._kind == kind && StringUtil::endsWith( path, suffix._source, true ) )
                return true;
        }
        return false;
    }

    void AssetCookPath::appendSourceSuffixes( AssetKind kind, vector<string_view>& outListSuffix )
    {
        for ( const AssetFormatInternal::CookSuffix& suffix : AssetFormatInternal::kArrCookSuffix )
        {
            if ( suffix._bSource && suffix._kind == kind )
                outListSuffix.push_back( suffix._source );
        }
    }

    string AssetCookPath::toSourcePath( string_view path, AssetKind kind )
    {
        if ( path.empty() || isCookableSource( path, kind ) )
            return string( path );

        // 정본 접미사는 그 종류의 첫 소스 줄이다(`.scene.xml` · `.prefab.xml`).
        string_view canonical;
        for ( const AssetFormatInternal::CookSuffix& suffix : AssetFormatInternal::kArrCookSuffix )
        {
            if ( suffix._bSource && suffix._kind == kind )
            {
                canonical = suffix._source;
                break;
            }
        }
        if ( canonical.empty() )
            return string( path );

        // 같은 종류의 굽지 않는 이름(쿠킹본 · 확장자 없는 이름)은 그 접미사를 정본으로 바꾼다.
        for ( const AssetFormatInternal::CookSuffix& suffix : AssetFormatInternal::kArrCookSuffix )
        {
            if ( suffix._bSource == false && suffix._kind == kind && StringUtil::endsWith( path, suffix._source, true ) )
                return string( path.substr( 0, path.size() - suffix._source.size() ) ) + string( canonical );
        }

        // 끝의 확장자가 정본의 마지막 확장자(`.xml`)와 같으면 그 자리에 넣는다 — `level.xml` → `level.scene.xml`.
        const string_view lastExtension = canonical.substr( canonical.rfind( '.' ) );
        if ( StringUtil::endsWith( path, lastExtension, true ) )
            return string( path.substr( 0, path.size() - lastExtension.size() ) ) + string( canonical );
        return string( path ) + string( canonical );
    }

    void AssetFormatRegistry::ensureBuiltins()
    {
        if ( _bBuiltins )
            return;
        _bBuiltins = true;
        // 내장 migrator 는 없다. 지금 디스크 스키마가 세대 0 이다.
        // 호환이 깨지는 변경이면 AssetFormatVersions::* 를 올리고 registerXmlMigrator(kind, from, …) 로 등록한다.
    }

    void AssetFormatRegistry::registerXmlMigrator( AssetKind kind, AssetFormatVersion fromVersion, XmlAssetMigrator migrator )
    {
        if ( migrator == nullptr )
            return;
        _mapMigrator.insert_or_assign( MigratorKey{ kind, fromVersion }, migrator );
    }

    AssetFormatVersion AssetFormatRegistry::readXmlVersion( XmlNode root ) const
    {
        if ( root.isValid() == false )
            return AssetFormatVersions::kUnversioned;
        const utf8* pAttr = root.findAttribute( kXmlAttrName );
        if ( StringUtil::isNullOrEmpty( pAttr ) == false )
        {
            // 읽지 못했거나 버전 타입에 담기지 않으면 **가장 큰 버전**으로 본다 — "지원하는 것보다 새 형식" 으로 거절된다. 예전에는 결과를
            // 버리고 잘라 담아, "4294967296" · "-1" 이 0(현재 버전)으로 읽혀 그 거절을 지나쳤다.
            uint64 ver{ AssetFormatVersions::kUnversioned };
            if ( StringUtil::parseUint64( pAttr, ver, 10 ) == false || ver > static_cast<uint64>( std::numeric_limits<AssetFormatVersion>::max() ) )
                return std::numeric_limits<AssetFormatVersion>::max();
            return static_cast<AssetFormatVersion>( ver );
        }
        return AssetFormatVersions::kUnversioned;
    }

    void AssetFormatRegistry::writeXmlVersion( XmlNode root, AssetFormatVersion version ) const
    {
        if ( root.isValid() == false )
            return;

        const string versionStr = sw::to_string( static_cast<uint32>( version ) );

        if ( root.findAttribute( kXmlAttrName ) != nullptr )
        {
            root.setAttribute( kXmlAttrName, versionStr.c_str() );
            return;
        }
        root.appendAttribute( kXmlAttrName, versionStr.c_str() );
    }

    AssetFormatVersion AssetFormatRegistry::inferXmlVersion( AssetKind kind, XmlNode root ) const
    {
        const AssetFormatVersion tagged = readXmlVersion( root );
        if ( tagged != AssetFormatVersions::kUnversioned || root.isValid() == false )
            return tagged;

        (void)kind;
        return AssetFormatVersions::kUnversioned;
    }

    bool AssetFormatRegistry::upgradeXml( AssetKind kind, XmlDocument& doc, XmlNode& root,
                                          AssetFormatVersion currentVersion, AssetFormatVersion* pOutSourceVersion )
    {
        ensureBuiltins();
        if ( root.isValid() == false )
            return false;

        AssetFormatVersion version = inferXmlVersion( kind, root );
        if ( pOutSourceVersion != nullptr )
            *pOutSourceVersion = version;

        if ( version > currentVersion )
        {
            SW_LOG_ERROR( "%# asset formatVersion %# is newer than supported %#", static_cast<uint32>( kind ), version, currentVersion );
            return false;
        }

        while ( version < currentVersion )
        {
            const MigratorKey key{ kind, version };
            const auto        it = _mapMigrator.find( key );
            if ( it == _mapMigrator.end() || it->second == nullptr )
            {
                SW_LOG_ERROR( "Missing migrator kind=%# from=%# to %#", static_cast<uint32>( kind ), version, version + 1 );
                return false;
            }
            if ( it->second( doc, root ) == false || root.isValid() == false )
            {
                SW_LOG_ERROR( "Migrator failed kind=%# from=%#", static_cast<uint32>( kind ), version );
                return false;
            }
            ++version;
        }

        writeXmlVersion( root, currentVersion );
        if ( pOutSourceVersion != nullptr && *pOutSourceVersion < currentVersion )
            SW_LOG_INFO( "Upgraded kind=%# formatVersion %# -> %#", static_cast<uint32>( kind ), *pOutSourceVersion, currentVersion );
        return true;
    }
} // namespace sw
