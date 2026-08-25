#include "pch.h"

#include "Engine/Scene/SceneDescriptor.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Utility/Resource/AssetFormat.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"
namespace sw
{

	namespace
	{

		constexpr const utf8* kRoot			   = "SceneDescriptor";
		constexpr const utf8* kName			   = "name";
		constexpr const utf8* kEntities		   = "entities";
		constexpr const utf8* kEntity		   = "entity";
		constexpr const utf8* kPrefab		   = "prefab";
		constexpr const utf8* kGameObjectState = "GameObjectState";
		constexpr const utf8* kDefaultEntity   = "Entity";
		constexpr uint32	  kSceneBinMagic   = 0x53434E31u; // 'SCN1'
		constexpr uint32	  kSceneBinVersion = 0;

		static bool readU32Val( const vector<uint8>& listBlob, size_t& offset, uint32& outValue )
		{
			if ( offset + 4 > listBlob.size() )
				return false;
			outValue = static_cast<uint32>( listBlob[offset] ) | ( static_cast<uint32>( listBlob[offset + 1] ) << 8 ) |
					   ( static_cast<uint32>( listBlob[offset + 2] ) << 16 ) | ( static_cast<uint32>( listBlob[offset + 3] ) << 24 );
			offset += 4;
			return true;
		}

		static bool readStrVal( const vector<uint8>& listBlob, size_t& offset, string& outString )
		{
			uint32 len{ 0 };
			if ( readU32Val( listBlob, offset, len ) == false || offset + len > listBlob.size() )
				return false;
			outString.assign( reinterpret_cast<const utf8*>( listBlob.data() + offset ), len );
			offset += len;
			return true;
		}

		static void appendU32Val( vector<uint8>& listBlob, uint32 value )
		{
			listBlob.push_back( static_cast<uint8>( value & 0xFFu ) );
			listBlob.push_back( static_cast<uint8>( ( value >> 8 ) & 0xFFu ) );
			listBlob.push_back( static_cast<uint8>( ( value >> 16 ) & 0xFFu ) );
			listBlob.push_back( static_cast<uint8>( ( value >> 24 ) & 0xFFu ) );
		}

		static void appendStrVal( vector<uint8>& listBlob, string_view text )
		{
			appendU32Val( listBlob, static_cast<uint32>( text.size() ) );
			listBlob.insert( listBlob.end(), text.begin(), text.end() );
		}

		string xmlEscape( string_view text )
		{
			StringBuilder<constant::kMaxBuffer1024> out;
			for ( utf8 ch : text )
			{
				switch ( ch )
				{
					case '&':
						out.append( "&amp;" );
						break;
					case '<':
						out.append( "&lt;" );
						break;
					case '>':
						out.append( "&gt;" );
						break;
					case '"':
						out.append( "&quot;" );
						break;
					case '\'':
						out.append( "&apos;" );
						break;
					default:
						out.append( ch );
						break;
				}
			}
			return string{ out.view() };
		}

		string absoluteWritePath( string_view path )
		{
			string result = ResourceUtil::getResourcePath( path );
			if ( result.empty() )
			{
				result = ResourceUtil::makeAbsolutePath( path );
				if ( result.empty() )
					result = path;
			}
			return result;
		}

		void appendNodeXml( StringBuilder<constant::kMaxBuffer8192>& out, XmlNode node )
		{
			if ( node.isValid() == false )
				return;

			out.append( '<' ).append( node.name() );
			for ( XmlAttribute attr = node.firstAttr(); attr; attr = attr.next() )
			{
				out.append( ' ' ).append( attr.name() ).append( "=\"" ).append( xmlEscape( attr.value() != nullptr ? attr.value() : "" ) ).append( '"' );
			}

			const bool bHasChildren = node.child().isValid();
			const bool bHasValue	= node.text() != nullptr && node.text()[0] != '\0' && bHasChildren == false;
			if ( bHasChildren == false && bHasValue == false )
			{
				out.append( "/>" );
				return;
			}

			out.append( '>' );
			if ( bHasValue )
				out.append( xmlEscape( node.text() ) );

			for ( XmlNode child = node.child(); child; child = child.next() )
			{
				appendNodeXml( out, child );
			}

			out.append( "</" ).append( node.name() ).append( '>' );
		}

	} // namespace

	bool loadSceneDescriptorFromXml( string_view path, SceneDescriptor& outDesc )
	{
		outDesc				= {};
		outDesc._sourcePath = path;

		XmlDocument doc;
		string		absPath;
		if ( doc.loadResource( path, &absPath ) == false )
		{
			// Absolute fallback
			if ( doc.loadFile( path ) == false )
			{
				SW_LOG_ERROR( "[SceneDescriptor] File not found: %#", path );
				return false;
			}
			absPath = path;
		}

		XmlNode root = doc.root( kRoot );
		if ( root.isValid() == false )
		{
			SW_LOG_ERROR( "[SceneDescriptor] Missing root <SceneDescriptor>: %#", absPath );
			return false;
		}

		if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::Scene, doc, root, AssetFormatVersions::kScene ) ==
			 false )
		{
			SW_LOG_ERROR( "[SceneDescriptor] formatVersion upgrade failed: %#", absPath );
			return false;
		}

		const utf8* pSceneName = root.attr( "name" );
		if ( pSceneName == nullptr )
		{
			pSceneName = root.childText( kName );
		}

		if ( pSceneName != nullptr )
		{
			outDesc._name = pSceneName;
		}
		else
		{
			outDesc._name	 = FileUtil::getFileNamePart( absPath );
			const size_t dot = outDesc._name.find_last_of( '.' );
			if ( dot != string::npos )
			{
				outDesc._name.resize( dot );
			}
		}

		XmlNode entities = root.child( kEntities );

		if ( entities.isValid() )
		{
			for ( XmlNode entityNode = entities.child( kEntity ); entityNode.isValid();
				  entityNode		 = entityNode.next( kEntity ) )
			{
				SceneEntityPlaceholder placeholder{};
				const utf8*			   pName = entityNode.attr( kName );
				if ( pName == nullptr )
				{
					pName = entityNode.childText( kName );
				}
				if ( pName != nullptr )
				{
					placeholder._name = pName;
				}

				const utf8* pPrefab = entityNode.attr( kPrefab );
				if ( pPrefab == nullptr )
				{
					pPrefab = entityNode.childText( kPrefab );
				}
				if ( pPrefab != nullptr )
				{
					placeholder._prefab = pPrefab;
				}

				XmlNode stateNode = entityNode.child( kGameObjectState );
				if ( stateNode.isValid() )
				{
					StringBuilder<constant::kMaxBuffer8192> stateSb;
					appendNodeXml( stateSb, stateNode );
					placeholder._embeddedXml = stateSb.view();
				}

				if ( placeholder._name.empty() )
				{
					placeholder._name = kDefaultEntity;
				}
				outDesc._listEntities.push_back( std::move( placeholder ) );
			}
		}

		outDesc._bValid = true;
		SW_LOG_INFO( "[SceneDescriptor] Loaded '%#' (%# entities) from %#",
					 outDesc._name, static_cast<uint32>( outDesc._listEntities.size() ), absPath );
		return true;
	}

	bool saveSceneDescriptorToXml( string_view path, const SceneDescriptor& desc )
	{
		StringBuilder<constant::kMaxBuffer8192> sb;
		sb.appendFormat( "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<%# formatVersion=\"%#\">\n\t<%#>%#</%#>\n\t<%#>\n",
						 kRoot,
						 static_cast<uint32>( AssetFormatVersions::kScene ),
						 kName,
						 xmlEscape( desc._name ),
						 kName,
						 kEntities );

		for ( const SceneEntityPlaceholder& entity : desc._listEntities )
		{
			sb.appendFormat( "\t\t<%# name=\"%#\"", kEntity, xmlEscape( entity._name ) );
			if ( entity._prefab.empty() == false )
			{
				sb.appendFormat( " prefab=\"%#\"", xmlEscape( entity._prefab ) );
			}
			if ( entity._embeddedXml.empty() )
			{
				sb.append( "/>\n" );
			}
			else
			{
				sb.appendFormat( ">\n\t\t\t%#\n\t\t</%#>\n", entity._embeddedXml, kEntity );
			}
		}

		sb.appendFormat( "\t</%#>\n</%#>\n", kEntities, kRoot );

		const string absPath = absoluteWritePath( path );
		if ( absPath.empty() )
		{
			SW_LOG_ERROR( "[SceneDescriptor] Cannot resolve save path: %#", path );
			return false;
		}
		FileUtil::createDirectory( absPath );
		if ( FileUtil::writeTextFile( absPath, sb.view() ) == false )
		{
			SW_LOG_ERROR( "[SceneDescriptor] Failed to write: %#", absPath );
			return false;
		}
		SW_LOG_INFO( "[SceneDescriptor] Saved '%#' (%# entities) -> %#",
					 desc._name, static_cast<uint32>( desc._listEntities.size() ), absPath );
		return true;
	}

	bool loadSceneDescriptorFromBinary( string_view path, SceneDescriptor& outDesc )
	{
		outDesc				= {};
		outDesc._sourcePath = path;

		string absPath = ResourceUtil::getResourcePath( path );
		if ( absPath.empty() )
			absPath = path;

		vector<uint8> listBlob;
		if ( FileUtil::readFile( absPath, listBlob ) == false || listBlob.size() < 12 )
		{
			SW_LOG_ERROR( "[SceneDescriptor] Binary read failed or too small: %#", absPath );
			return false;
		}

		size_t offset{ 0 };
		uint32 magic{ 0 };
		if ( readU32Val( listBlob, offset, magic ) == false || magic != kSceneBinMagic )
		{
			SW_LOG_ERROR( "[SceneDescriptor] Bad binary magic: %#", absPath );
			return false;
		}

		uint32 version{ 0 };
		if ( readU32Val( listBlob, offset, version ) == false || version > kSceneBinVersion )
		{
			SW_LOG_ERROR( "[SceneDescriptor] Unsupported binary version %# in %#", version, absPath );
			return false;
		}

		if ( readStrVal( listBlob, offset, outDesc._name ) == false )
		{
			SW_LOG_ERROR( "[SceneDescriptor] Binary name truncated: %#", absPath );
			return false;
		}

		uint32 entityCount{ 0 };
		if ( readU32Val( listBlob, offset, entityCount ) == false )
		{
			SW_LOG_ERROR( "[SceneDescriptor] Binary entity count truncated: %#", absPath );
			return false;
		}

		outDesc._listEntities.reserve( entityCount );
		for ( uint32 entityIndex = 0; entityIndex < entityCount; ++entityIndex )
		{
			SceneEntityPlaceholder placeholder{};
			if ( readStrVal( listBlob, offset, placeholder._name ) == false ||
				 readStrVal( listBlob, offset, placeholder._prefab ) == false ||
				 readStrVal( listBlob, offset, placeholder._embeddedXml ) == false )
			{
				SW_LOG_ERROR( "[SceneDescriptor] Binary entity truncated at index %#: %#", entityIndex, absPath );
				return false;
			}
			outDesc._listEntities.push_back( std::move( placeholder ) );
		}

		outDesc._bValid = true;
		SW_LOG_INFO( "[SceneDescriptor] Loaded '%#' (%# entities) from binary %#",
					 outDesc._name, static_cast<uint32>( outDesc._listEntities.size() ), absPath );
		return true;
	}

	bool saveSceneDescriptorToBinary( string_view path, const SceneDescriptor& desc )
	{
		vector<uint8> listBlob;
		appendU32Val( listBlob, kSceneBinMagic );
		appendU32Val( listBlob, kSceneBinVersion );
		appendStrVal( listBlob, desc._name );
		appendU32Val( listBlob, static_cast<uint32>( desc._listEntities.size() ) );

		for ( const SceneEntityPlaceholder& entity : desc._listEntities )
		{
			appendStrVal( listBlob, entity._name );
			appendStrVal( listBlob, entity._prefab );
			appendStrVal( listBlob, entity._embeddedXml );
		}

		const string absPath = absoluteWritePath( path );
		if ( absPath.empty() )
		{
			SW_LOG_ERROR( "[SceneDescriptor] Cannot resolve save path: %#", path );
			return false;
		}
		FileUtil::createDirectory( absPath );
		const bool bOk = FileUtil::writeFile( absPath, listBlob.data(), listBlob.size() );
		if ( bOk )
			SW_LOG_INFO( "[SceneDescriptor] Saved binary '%#' (%# entities) -> %#",
						 desc._name, static_cast<uint32>( desc._listEntities.size() ), absPath );
		return bOk;
	}

	bool loadSceneDescriptor( string_view path, SceneDescriptor& outDesc )
	{
		string	   binPath( path );
		const bool bXml = FileUtil::hasExtension( binPath, ".xml" );
		if ( bXml )
			binPath.replace( binPath.size() - 4, 4, ".bin" );
		else if ( binPath.find( ".scene" ) != string::npos && FileUtil::hasExtension( binPath, ".bin" ) == false )
			binPath += ".bin";

#if defined( SW_SHIPPING )
		if ( loadSceneDescriptorFromBinary( binPath, outDesc ) )
			return true;
		SW_LOG_ERROR( "[SceneDescriptor] Shipping requires cooked binary scene: %#", binPath );
		return false;
#else
		if ( FileUtil::hasExtension( path, ".bin" ) )
			return loadSceneDescriptorFromBinary( path, outDesc );

		string absBinPath = ResourceUtil::getResourcePath( binPath );
		if ( absBinPath.empty() )
			absBinPath = binPath;

		if ( FileUtil::fileExists( absBinPath ) && loadSceneDescriptorFromBinary( binPath, outDesc ) )
			return true;

		return loadSceneDescriptorFromXml( path, outDesc );
#endif
	}
} // namespace sw
