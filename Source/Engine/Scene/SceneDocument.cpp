#include "pch.h"

#include "Engine/Scene/SceneDocument.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Utility/Resource/AssetFormat.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	SW_LOG_CALLER( "SceneDocument" );

	namespace
	{

		constexpr const utf8* kRoot			   = "Scene";
		constexpr const utf8* kName			   = "name";
		constexpr const utf8* kEntities		   = "entities";
		constexpr const utf8* kEntity		   = "entity";
		constexpr const utf8* kPrefab		   = "prefab";
		constexpr const utf8* kGameObject	   = "GameObject";
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

			const utf8* pNodeName = node.name();
			if ( pNodeName == nullptr || pNodeName[0] == '\0' )
				return;

			out.append( '<' ).append( pNodeName );
			for ( XmlAttribute attr = node.firstAttr(); attr; attr = attr.next() )
			{
				out.append( ' ' ).append( attr.name() ).append( "=\"" ).append( xmlEscape( attr.value() != nullptr ? attr.value() : "" ) ).append( '"' );
			}

			bool bHasElementChild = false;
			for ( XmlNode child = node.child(); child; child = child.next() )
			{
				const utf8* pChildName = child.name();
				if ( pChildName != nullptr && pChildName[0] != '\0' )
				{
					bHasElementChild = true;
					break;
				}
			}

			const bool bHasValue = node.text() != nullptr && node.text()[0] != '\0';
			if ( bHasElementChild == false && bHasValue == false )
			{
				out.append( "/>" );
				return;
			}

			out.append( '>' );
			if ( bHasValue )
				out.append( xmlEscape( node.text() ) );

			for ( XmlNode child = node.child(); child; child = child.next() )
			{
				const utf8* pChildName = child.name();
				if ( pChildName != nullptr && pChildName[0] != '\0' )
					appendNodeXml( out, child );
			}

			out.append( "</" ).append( pNodeName ).append( '>' );
		}

	} // namespace

	bool loadSceneDocumentFromXml( string_view path, SceneDocument& outDoc )
	{
		outDoc			   = {};
		outDoc._sourcePath = path;

		XmlDocument doc;
		string		absPath;
		if ( doc.loadResource( path, &absPath ) == false )
		{
			// Absolute fallback
			if ( doc.loadFile( path ) == false )
			{
				SW_LOG_ERROR( "File not found: %#", path );
				return false;
			}
			absPath = path;
		}

		XmlNode root = doc.root( kRoot );
		if ( root.isValid() == false )
		{
			SW_LOG_ERROR( "Missing root <Scene>: %#", absPath );
			return false;
		}

		if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::Scene, doc, root, AssetFormatVersions::kScene ) ==
			 false )
		{
			SW_LOG_ERROR( "formatVersion upgrade failed: %#", absPath );
			return false;
		}

		const utf8* pSceneName = root.attr( "name" );
		if ( pSceneName == nullptr )
		{
			pSceneName = root.childText( kName );
		}

		if ( pSceneName != nullptr )
		{
			outDoc._name = pSceneName;
		}
		else
		{
			outDoc._name	 = FileUtil::getFileNamePart( absPath );
			const size_t dot = outDoc._name.find_last_of( '.' );
			if ( dot != string::npos )
			{
				outDoc._name.resize( dot );
			}
		}

		XmlNode entities = root.child( kEntities );

		if ( entities.isValid() )
		{
			for ( XmlNode entityNode = entities.child( kEntity ); entityNode.isValid();
				  entityNode		 = entityNode.next( kEntity ) )
			{
				SceneEntityNode node{};
				const utf8*		pName = entityNode.attr( kName );
				if ( pName == nullptr )
				{
					pName = entityNode.childText( kName );
				}
				if ( pName != nullptr )
				{
					node._name = pName;
				}

				const utf8* pPrefab = entityNode.attr( kPrefab );
				if ( pPrefab == nullptr )
				{
					pPrefab = entityNode.childText( kPrefab );
				}
				if ( pPrefab != nullptr )
				{
					node._prefab = pPrefab;
				}

				XmlNode stateNode = entityNode.child( kGameObject );
				if ( stateNode.isValid() )
				{
					StringBuilder<constant::kMaxBuffer8192> stateSb;
					appendNodeXml( stateSb, stateNode );
					node._embeddedXml = stateSb.view();
				}

				if ( node._name.empty() )
				{
					node._name = kDefaultEntity;
				}
				outDoc._listEntityNode.push_back( std::move( node ) );
			}
		}

		outDoc._bValid = true;
		SW_LOG_INFO( "Loaded '%#' (%# entities) from %#",
					 outDoc._name, static_cast<uint32>( outDoc._listEntityNode.size() ), absPath );
		return true;
	}

	bool saveSceneDocumentToXml( string_view path, const SceneDocument& doc )
	{
		StringBuilder<constant::kMaxBuffer8192> sb;
		sb.appendFormat( "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<%# formatVersion=\"%#\" name=\"%#\">\n\t<%#>\n",
						 kRoot,
						 static_cast<uint32>( AssetFormatVersions::kScene ),
						 xmlEscape( doc._name ),
						 kEntities );

		for ( const SceneEntityNode& entity : doc._listEntityNode )
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
			SW_LOG_ERROR( "Cannot resolve save path: %#", path );
			return false;
		}
		FileUtil::createDirectory( absPath );
		if ( FileUtil::writeTextFile( absPath, sb.view() ) == false )
		{
			SW_LOG_ERROR( "Failed to write: %#", absPath );
			return false;
		}
		SW_LOG_INFO( "Saved '%#' (%# entities) -> %#",
					 doc._name, static_cast<uint32>( doc._listEntityNode.size() ), absPath );
		return true;
	}

	bool loadSceneDocumentFromBinary( string_view path, SceneDocument& outDoc )
	{
		outDoc			   = {};
		outDoc._sourcePath = path;

		string absPath = ResourceUtil::getResourcePath( path );
		if ( absPath.empty() )
			absPath = path;

		vector<uint8> listBlob;
		if ( FileUtil::readFile( absPath, listBlob ) == false || listBlob.size() < 12 )
		{
			SW_LOG_ERROR( "Binary read failed or too small: %#", absPath );
			return false;
		}

		size_t offset{ 0 };
		uint32 magic{ 0 };
		if ( readU32Val( listBlob, offset, magic ) == false || magic != kSceneBinMagic )
		{
			SW_LOG_ERROR( "Bad binary magic: %#", absPath );
			return false;
		}

		uint32 version{ 0 };
		if ( readU32Val( listBlob, offset, version ) == false || version > kSceneBinVersion )
		{
			SW_LOG_ERROR( "Unsupported binary version %# in %#", version, absPath );
			return false;
		}

		if ( readStrVal( listBlob, offset, outDoc._name ) == false )
		{
			SW_LOG_ERROR( "Binary name truncated: %#", absPath );
			return false;
		}

		uint32 entityCount{ 0 };
		if ( readU32Val( listBlob, offset, entityCount ) == false )
		{
			SW_LOG_ERROR( "Binary entity count truncated: %#", absPath );
			return false;
		}

		outDoc._listEntityNode.reserve( entityCount );
		for ( uint32 entityIndex = 0; entityIndex < entityCount; ++entityIndex )
		{
			SceneEntityNode node{};
			if ( readStrVal( listBlob, offset, node._name ) == false ||
				 readStrVal( listBlob, offset, node._prefab ) == false ||
				 readStrVal( listBlob, offset, node._embeddedXml ) == false )
			{
				SW_LOG_ERROR( "Binary entity truncated at index %#: %#", entityIndex, absPath );
				return false;
			}
			outDoc._listEntityNode.push_back( std::move( node ) );
		}

		outDoc._bValid = true;
		SW_LOG_INFO( "Loaded '%#' (%# entities) from binary %#",
					 outDoc._name, static_cast<uint32>( outDoc._listEntityNode.size() ), absPath );
		return true;
	}

	bool saveSceneDocumentToBinary( string_view path, const SceneDocument& doc )
	{
		vector<uint8> listBlob;
		appendU32Val( listBlob, kSceneBinMagic );
		appendU32Val( listBlob, kSceneBinVersion );
		appendStrVal( listBlob, doc._name );
		appendU32Val( listBlob, static_cast<uint32>( doc._listEntityNode.size() ) );

		for ( const SceneEntityNode& entity : doc._listEntityNode )
		{
			appendStrVal( listBlob, entity._name );
			appendStrVal( listBlob, entity._prefab );
			appendStrVal( listBlob, entity._embeddedXml );
		}

		const string absPath = absoluteWritePath( path );
		if ( absPath.empty() )
		{
			SW_LOG_ERROR( "Cannot resolve save path: %#", path );
			return false;
		}
		FileUtil::createDirectory( absPath );
		const bool bOk = FileUtil::writeFile( absPath, listBlob.data(), listBlob.size() );
		if ( bOk )
			SW_LOG_INFO( "Saved binary '%#' (%# entities) -> %#",
						 doc._name, static_cast<uint32>( doc._listEntityNode.size() ), absPath );
		return bOk;
	}

	bool loadSceneDocument( string_view path, SceneDocument& outDoc )
	{
		string	   binPath( path );
		const bool bXml = FileUtil::hasExtension( binPath, ".xml" );
		if ( bXml )
			binPath.replace( binPath.size() - 4, 4, ".bin" );
		else if ( binPath.find( ".scene" ) != string::npos && FileUtil::hasExtension( binPath, ".bin" ) == false )
			binPath += ".bin";

#if defined( SW_SHIPPING )
		if ( loadSceneDocumentFromBinary( binPath, outDoc ) )
			return true;
		SW_LOG_ERROR( "Shipping requires cooked binary scene: %#", binPath );
		return false;
#else
		if ( FileUtil::hasExtension( path, ".bin" ) )
			return loadSceneDocumentFromBinary( path, outDoc );

		string absBinPath = ResourceUtil::getResourcePath( binPath );
		if ( absBinPath.empty() )
			absBinPath = binPath;

		if ( FileUtil::fileExists( absBinPath ) && loadSceneDocumentFromBinary( binPath, outDoc ) )
			return true;

		return loadSceneDocumentFromXml( path, outDoc );
#endif
	}
} // namespace sw
