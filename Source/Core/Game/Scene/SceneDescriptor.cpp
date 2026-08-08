/**
 * @file SceneDescriptor.cpp
 */
#include "SceneDescriptor.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Resource/ResourceUtil.h"

#include <rapidxml/rapidxml.hpp>

namespace sw
{
	bool loadSceneDescriptorFromXml( const std::string& path, SceneDescriptor& outDesc )
	{
		outDesc = {};
		outDesc._sourcePath = path;

		std::string absPath = ResourceUtil::getResourcePath( path );
		if ( absPath.empty() )
			absPath = path;

		if ( FileUtil::isFileExist( absPath ) == false )
		{
			SW_LOG_ERROR( "[SceneDescriptor] File not found: %#", absPath );
			return false;
		}

		std::vector<uint8> fileData;
		if ( FileUtil::readFile( absPath, fileData ) == false || fileData.empty() )
		{
			SW_LOG_ERROR( "[SceneDescriptor] Failed to read: %#", absPath );
			return false;
		}

		std::vector<utf8> xmlBuf( fileData.begin(), fileData.end() );
		xmlBuf.push_back( '\0' );

		rapidxml::xml_document<> doc;
		doc.parse<0>( xmlBuf.data() );

		rapidxml::xml_node<>* root = doc.first_node( "SceneDescriptor" );
		if ( root == nullptr )
			root = doc.first_node( "Scene" );
		if ( root == nullptr )
		{
			SW_LOG_ERROR( "[SceneDescriptor] Missing root <SceneDescriptor>: %#", absPath );
			return false;
		}

		rapidxml::xml_node<>* nameNode = root->first_node( "name" );
		if ( nameNode == nullptr )
			nameNode = root->first_node( "_name" );
		if ( nameNode != nullptr )
			outDesc._name = nameNode->value();
		else
		{
			outDesc._name	 = FileUtil::getFileNamePart( absPath );
			const size_t dot = outDesc._name.find_last_of( '.' );
			if ( dot != std::string::npos )
				outDesc._name.resize( dot );
		}

		rapidxml::xml_node<>* entities = root->first_node( "entities" );
		if ( entities == nullptr )
			entities = root->first_node( "_entities" );

		if ( entities != nullptr )
		{
			for ( rapidxml::xml_node<>* ent = entities->first_node( "entity" ); ent; ent = ent->next_sibling( "entity" ) )
			{
				SceneEntityPlaceholder placeholder{};
				if ( rapidxml::xml_attribute<>* attr = ent->first_attribute( "name" ) )
					placeholder._name = attr->value();
				else if ( rapidxml::xml_node<>* n = ent->first_node( "name" ) )
					placeholder._name = n->value();

				if ( rapidxml::xml_attribute<>* attr = ent->first_attribute( "prefab" ) )
					placeholder._prefab = attr->value();
				else if ( rapidxml::xml_node<>* n = ent->first_node( "prefab" ) )
					placeholder._prefab = n->value();

				if ( placeholder._name.empty() )
					placeholder._name = "Entity";
				outDesc._entities.push_back( std::move( placeholder ) );
			}
		}

		outDesc._bValid = true;
		SW_LOG_INFO( "[SceneDescriptor] Loaded '%#' (%# entities) from %#",
					 outDesc._name, outDesc._entities.size(), absPath );
		return true;
	}
} // namespace sw
