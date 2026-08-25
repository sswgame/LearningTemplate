#include "pch.h"

#include "Engine/Utility/Resource/AssetFormat.h"

namespace sw
{
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
		_mapMigrators.insert_or_assign( MigratorKey{ kind, fromVersion }, migrator );
	}

	AssetFormatVersion AssetFormatRegistry::readXmlVersion( XmlNode root ) const
	{
		if ( root.isValid() == false )
			return AssetFormatVersions::kUnversioned;
		const utf8* pAttr = root.attr( kXmlAttrName );
		if ( pAttr != nullptr && pAttr[0] != '\0' )
			return static_cast<AssetFormatVersion>( StringUtil::strtoull( pAttr, nullptr, 10 ) );
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
			SW_LOG_ERROR( "[AssetFormat] %# asset formatVersion %# is newer than supported %#", static_cast<uint32>( kind ), version, currentVersion );
			return false;
		}

		while ( version < currentVersion )
		{
			const MigratorKey key{ kind, version };
			const auto		  it = _mapMigrators.find( key );
			if ( it == _mapMigrators.end() || it->second == nullptr )
			{
				SW_LOG_ERROR( "[AssetFormat] Missing migrator kind=%# from=%# to %#", static_cast<uint32>( kind ), version, version + 1 );
				return false;
			}
			if ( it->second( doc, root ) == false || root.isValid() == false )
			{
				SW_LOG_ERROR( "[AssetFormat] Migrator failed kind=%# from=%#", static_cast<uint32>( kind ), version );
				return false;
			}
			++version;
		}

		writeXmlVersion( root, currentVersion );
		if ( pOutSourceVersion != nullptr && *pOutSourceVersion < currentVersion )
		{
			SW_LOG_INFO( "[AssetFormat] Upgraded kind=%# formatVersion %# -> %#", static_cast<uint32>( kind ), *pOutSourceVersion, currentVersion );
		}
		return true;
	}
} // namespace sw
