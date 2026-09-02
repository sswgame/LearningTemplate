#include "pch.h"

#include "Engine/Resource/AssetFormat.h"

#include "Core/String/StringUtil.h"

namespace sw
{
    SW_LOG_CALLER( "AssetFormat" );

    void AssetFormatRegistry::ensureBuiltins()
    {
        if ( _bBuiltins )
            return;
        _bBuiltins = true;
        // No built-in migrators: current disk schema is generation 0.
        // On a breaking change, bump AssetFormatVersions::* and registerXmlMigrator(kind, from, …).
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
        const utf8* pAttr = root.attr( kXmlAttrName );
        if ( StringUtil::isNullOrEmpty( pAttr ) == false )
        {
            uint64 ver{ AssetFormatVersions::kUnversioned };
            StringUtil::parseUInt64( pAttr, ver, 10 );
            return static_cast<AssetFormatVersion>( ver );
        }
        return AssetFormatVersions::kUnversioned;
    }

    void AssetFormatRegistry::writeXmlVersion( XmlNode root, AssetFormatVersion version ) const
    {
        if ( root.isValid() == false )
            return;

        const string versionStr = sw::to_string( static_cast<uint32>( version ) );

        if ( root.attr( kXmlAttrName ) != nullptr )
        {
            root.setAttr( kXmlAttrName, versionStr.c_str() );
            return;
        }
        root.appendAttr( kXmlAttrName, versionStr.c_str() );
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
        {
            SW_LOG_INFO( "Upgraded kind=%# formatVersion %# -> %#", static_cast<uint32>( kind ), *pOutSourceVersion, currentVersion );
        }
        return true;
    }
} // namespace sw
