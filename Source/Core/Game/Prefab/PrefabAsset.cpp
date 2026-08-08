/**
 * @file PrefabAsset.cpp
 */
#include "PrefabAsset.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/ObjectStateSerializer.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Utility/String/hashed_string.h"

#include <rapidxml/rapidxml.hpp>
#include <rapidxml/rapidxml_print.hpp>

namespace sw
{
	namespace
	{
		bool endsWith( const std::string& path, const char* suffix )
		{
			const size_t suffixLen = std::strlen( suffix );
			return path.size() >= suffixLen && path.compare( path.size() - suffixLen, suffixLen, suffix ) == 0;
		}

		std::string escapeJsonString( const std::string& value )
		{
			std::string out;
			out.reserve( value.size() + 8 );
			for ( char c : value )
			{
				switch ( c )
				{
				case '\\':
					out += "\\\\";
					break;
				case '"':
					out += "\\\"";
					break;
				case '\n':
					out += "\\n";
					break;
				case '\r':
					out += "\\r";
					break;
				case '\t':
					out += "\\t";
					break;
				default:
					out += c;
					break;
				}
			}
			return out;
		}

		std::string unescapeJsonString( const std::string& value )
		{
			std::string out;
			out.reserve( value.size() );
			for ( size_t i = 0; i < value.size(); ++i )
			{
				if ( value[i] == '\\' && i + 1 < value.size() )
				{
					const char next = value[i + 1];
					switch ( next )
					{
					case '\\':
						out += '\\';
						break;
					case '"':
						out += '"';
						break;
					case 'n':
						out += '\n';
						break;
					case 'r':
						out += '\r';
						break;
					case 't':
						out += '\t';
						break;
					default:
						out += next;
						break;
					}
					++i;
					continue;
				}
				out += value[i];
			}
			return out;
		}

		std::string extractJsonStringField( const std::string& json, const char* fieldName )
		{
			const std::string needle = std::string( "\"" ) + fieldName + "\"";
			size_t			  pos	   = json.find( needle );
			if ( pos == std::string::npos )
				return {};

			pos = json.find( ':', pos );
			if ( pos == std::string::npos )
				return {};

			++pos;
			while ( pos < json.size() && ( json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r' ) )
				++pos;

			if ( pos >= json.size() || json[pos] != '"' )
				return {};

			++pos;
			std::string raw;
			while ( pos < json.size() )
			{
				if ( json[pos] == '"' )
					break;
				if ( json[pos] == '\\' && pos + 1 < json.size() )
				{
					raw += json[pos];
					raw += json[pos + 1];
					pos += 2;
					continue;
				}
				raw += json[pos];
				++pos;
			}
			return unescapeJsonString( raw );
		}
	} // namespace
	PrefabAsset::PrefabAsset()
		: _bValid{ 0 }
		, _reserved{ 0 }
	{
	}

	void PrefabAsset::setFromGameObject( const GameObject* gameObject )
	{
		if ( gameObject == nullptr )
		{
			_bValid = 0;
			return;
		}
		_name	 = gameObject->getName().c_str();
		_xmlBody = ObjectStateSerializer::saveToXmlString( gameObject );
		_bValid	 = _xmlBody.empty() == false ? 1 : 0;
	}

	bool PrefabAsset::loadFromXmlFile( const std::string& assetRelativePath )
	{
		_bValid = 0;
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		if ( FileUtil::isFileExist( absPath ) == false )
		{
			SW_LOG_ERROR( "[PrefabAsset] Not found: %#", absPath );
			return false;
		}

		std::vector<uint8> fileData;
		if ( FileUtil::readFile( absPath, fileData ) == false || fileData.empty() )
			return false;

		std::vector<utf8> xmlBuf( fileData.begin(), fileData.end() );
		xmlBuf.push_back( '\0' );

		rapidxml::xml_document<> doc;
		doc.parse<0>( xmlBuf.data() );

		rapidxml::xml_node<>* root = doc.first_node( "Prefab" );
		if ( root == nullptr )
		{
			SW_LOG_ERROR( "[PrefabAsset] Missing <Prefab>: %#", absPath );
			return false;
		}

		if ( rapidxml::xml_node<>* n = root->first_node( "name" ) )
			_name = n->value();
		else if ( rapidxml::xml_attribute<>* a = root->first_attribute( "name" ) )
			_name = a->value();

		if ( rapidxml::xml_node<>* body = root->first_node( "GameObjectState" ) )
		{
			std::string bodyStr;
			rapidxml::print( std::back_inserter( bodyStr ), *body, 0 );
			_xmlBody = std::move( bodyStr );
		}
		else if ( rapidxml::xml_node<>* objectStateNode = root->first_node( "ObjectState" ) )
		{
			// Nested text may already be a GameObjectState document
			_xmlBody = objectStateNode->value() != nullptr ? objectStateNode->value() : "";
			if ( _xmlBody.empty() )
			{
				std::string bodyStr;
				rapidxml::print( std::back_inserter( bodyStr ), *objectStateNode, 0 );
				_xmlBody = std::move( bodyStr );
			}
		}
		else
		{
			_xmlBody.assign( fileData.begin(), fileData.end() );
		}

		_bValid = 1;
		SW_LOG_INFO( "[PrefabAsset] Loaded '%#' from %#", _name, absPath );
		return true;
	}

	bool PrefabAsset::saveToXmlFile( const std::string& assetRelativePath ) const
	{
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		rapidxml::xml_document<> doc;
		rapidxml::xml_node<>*	 decl = doc.allocate_node( rapidxml::node_declaration );
		decl->append_attribute( doc.allocate_attribute( "version", "1.0" ) );
		decl->append_attribute( doc.allocate_attribute( "encoding", "utf-8" ) );
		doc.append_node( decl );

		rapidxml::xml_node<>* root = doc.allocate_node( rapidxml::node_element, "Prefab" );
		doc.append_node( root );
		root->append_node( doc.allocate_node( rapidxml::node_element, "name", doc.allocate_string( _name.c_str() ) ) );

		// Embed raw object XML as child text blob under ObjectState
		rapidxml::xml_node<>* state = doc.allocate_node( rapidxml::node_element, "ObjectState", doc.allocate_string( _xmlBody.c_str() ) );
		root->append_node( state );

		std::string xmlStr;
		rapidxml::print( std::back_inserter( xmlStr ), doc, 0 );
		const bool ok = FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( xmlStr.data() ), static_cast<uint64>( xmlStr.size() ) );
		if ( ok )
			SW_LOG_INFO( "[PrefabAsset] Saved '%#' → %#", _name, absPath );
		return ok;
	}

	bool PrefabAsset::loadFromJsonFile( const std::string& assetRelativePath )
	{
		_bValid = 0;
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		if ( FileUtil::isFileExist( absPath ) == false )
		{
			SW_LOG_ERROR( "[PrefabAsset] Not found: %#", absPath );
			return false;
		}

		std::vector<uint8> fileData;
		if ( FileUtil::readFile( absPath, fileData ) == false || fileData.empty() )
			return false;

		const std::string json( fileData.begin(), fileData.end() );
		_name	 = extractJsonStringField( json, "name" );
		_xmlBody = extractJsonStringField( json, "xmlBody" );

		if ( _xmlBody.empty() )
		{
			SW_LOG_ERROR( "[PrefabAsset] Missing xmlBody in JSON: %#", absPath );
			return false;
		}

		_bValid = 1;
		SW_LOG_INFO( "[PrefabAsset] Loaded '%#' from JSON %#", _name, absPath );
		return true;
	}

	bool PrefabAsset::saveToJsonFile( const std::string& assetRelativePath ) const
	{
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		std::string json;
		json.reserve( _name.size() + _xmlBody.size() + 64 );
		json += "{\n";
		json += "\t\"name\": \"" + escapeJsonString( _name ) + "\",\n";
		json += "\t\"xmlBody\": \"" + escapeJsonString( _xmlBody ) + "\"\n";
		json += "}\n";

		const bool ok = FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( json.data() ), static_cast<uint64>( json.size() ) );
		if ( ok )
			SW_LOG_INFO( "[PrefabAsset] Saved '%#' → JSON %#", _name, absPath );
		return ok;
	}

	namespace
	{
		constexpr uint32 kPrefabBinMagic = 0x50464231u; // 'PFB1'
	}

	bool PrefabAsset::saveToBinaryFile( const std::string& assetRelativePath ) const
	{
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		std::vector<uint8> blob;
		auto appendU32 = [&blob]( uint32 v ) {
			blob.push_back( static_cast<uint8>( v & 0xFFu ) );
			blob.push_back( static_cast<uint8>( ( v >> 8 ) & 0xFFu ) );
			blob.push_back( static_cast<uint8>( ( v >> 16 ) & 0xFFu ) );
			blob.push_back( static_cast<uint8>( ( v >> 24 ) & 0xFFu ) );
		};
		auto appendStr = [&]( const std::string& s ) {
			appendU32( static_cast<uint32>( s.size() ) );
			blob.insert( blob.end(), s.begin(), s.end() );
		};

		appendU32( kPrefabBinMagic );
		appendStr( _name );
		appendStr( _xmlBody );
		return FileUtil::writeFile( absPath, blob.data(), static_cast<uint64>( blob.size() ) );
	}

	bool PrefabAsset::loadFromBinaryFile( const std::string& assetRelativePath )
	{
		_bValid					= 0;
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		std::vector<uint8> blob;
		if ( FileUtil::readFile( absPath, blob ) == false || blob.size() < 12 )
			return false;

		size_t offset = 0;
		auto   readU32 = [&]( uint32& out ) -> bool {
			if ( offset + 4 > blob.size() )
				return false;
			out = static_cast<uint32>( blob[offset] ) | ( static_cast<uint32>( blob[offset + 1] ) << 8 ) |
				  ( static_cast<uint32>( blob[offset + 2] ) << 16 ) | ( static_cast<uint32>( blob[offset + 3] ) << 24 );
			offset += 4;
			return true;
		};
		auto readStr = [&]( std::string& out ) -> bool {
			uint32 len = 0;
			if ( readU32( len ) == false || offset + len > blob.size() )
				return false;
			out.assign( reinterpret_cast<const char*>( blob.data() + offset ), len );
			offset += len;
			return true;
		};

		uint32 magic = 0;
		if ( readU32( magic ) == false || magic != kPrefabBinMagic )
			return false;
		if ( readStr( _name ) == false || readStr( _xmlBody ) == false )
			return false;

		_bValid = 1;
		return true;
	}

	PrefabManager::PrefabManager() = default;

	PrefabManager& PrefabManager::get()
	{
		static PrefabManager s_instance;
		return s_instance;
	}

	PrefabAsset* PrefabManager::loadPrefab( const std::string& assetRelativePath )
	{
		auto it = _cache.find( assetRelativePath );
		if ( it != _cache.end() )
			return it->second.get();

		auto asset = std::make_unique<PrefabAsset>();

		std::string binPath = assetRelativePath;
		const bool	bJson	= endsWith( binPath, ".json" );
		const bool	bXml	= endsWith( binPath, ".xml" );
		if ( bXml )
			binPath.replace( binPath.size() - 4, 4, ".bin" );
		else if ( bJson )
			binPath.replace( binPath.size() - 5, 5, ".bin" );
		else if ( binPath.find( ".prefab" ) != std::string::npos )
			binPath += ".bin";

		auto tryDevSource = [&]() -> bool {
			if ( bJson )
				return asset->loadFromJsonFile( assetRelativePath );
			return asset->loadFromXmlFile( assetRelativePath );
		};

#if defined( SW_SHIPPING )
		if ( asset->loadFromBinaryFile( binPath ) == false && tryDevSource() == false )
			return nullptr;
#else
		if ( tryDevSource() == false && asset->loadFromBinaryFile( binPath ) == false )
			return nullptr;
#endif

		PrefabAsset* ptr = asset.get();
		_cache.emplace( assetRelativePath, std::move( asset ) );
		return ptr;
	}

	bool PrefabManager::cookPrefabToBinary( const std::string& sourceRelativePath, const std::string& binRelativePath )
	{
		PrefabAsset asset;
		const bool	bLoaded = endsWith( sourceRelativePath, ".json" )
									? asset.loadFromJsonFile( sourceRelativePath )
									: asset.loadFromXmlFile( sourceRelativePath );
		if ( bLoaded == false )
			return false;
		return asset.saveToBinaryFile( binRelativePath );
	}

	GameObject* PrefabManager::spawn( GameObjectManager* objects, const std::string& assetRelativePath, const char* instanceName )
	{
		if ( objects == nullptr )
			return nullptr;

		PrefabAsset* asset = loadPrefab( assetRelativePath );
		if ( asset == nullptr || asset->isValid() == false )
			return nullptr;

		const char* name = instanceName != nullptr ? instanceName : asset->getName().c_str();
		if ( name == nullptr || name[0] == '\0' )
			name = "PrefabInstance";

		GameObject* go = objects->createGameObject( hashed_string( name ) );
		if ( go == nullptr )
			return nullptr;

		if ( asset->getXmlBody().empty() == false )
		{
			if ( ObjectStateSerializer::loadFromXmlString( go, asset->getXmlBody() ) == false )
				SW_LOG_WARNING( "[PrefabManager] Spawned '%#' but ObjectState apply failed — placeholder only", name );
		}

		SW_LOG_INFO( "[PrefabManager] Spawned '%#' from %#", name, assetRelativePath );
		return go;
	}
} // namespace sw
