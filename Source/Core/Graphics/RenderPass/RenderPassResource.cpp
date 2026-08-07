/**
 * @file RenderPassResource.cpp
 * @brief RenderPassResource 구현
 */
#include "Core/CoreMinimal.h"

#include "RenderPassResource.h"

#include "Core/Common/CommonDefines.h"

#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Utility/Task/TaskManager.h"
#include "Core/Utility/String/StringBuilder.h"

#include <rapidxml/rapidxml.hpp>
#include <rapidxml/rapidxml_print.hpp>

namespace sw
{

	bool RenderPassResource::loadFromXmlFile( const std::string& assetRelativePath )
	{
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
		{
			absPath = assetRelativePath;
		}

		if ( FileUtil::isFileExist( absPath ) == false )
		{
			SW_LOG_ERROR( "[RenderPassResource] XML file not found: %#", absPath );
			return false;
		}

		std::vector<uint8> fileData;
		if ( FileUtil::readFile( absPath, fileData ) == false )
		{
			SW_LOG_ERROR( "[RenderPassResource] Failed to open XML file: %#", absPath );
			return false;
		}

		if ( fileData.empty() )
		{
			SW_LOG_ERROR( "[RenderPassResource] XML file is empty: %#", absPath );
			return false;
		}

		std::vector<utf8> xmlBuf( fileData.begin(), fileData.end() );
		xmlBuf.push_back( '\0' );

		rapidxml::xml_document<> doc;
		doc.parse<0>( xmlBuf.data() );

		rapidxml::xml_node<>* root = doc.first_node( "RenderPassDesc" );
		if ( root == nullptr )
		{
			SW_LOG_ERROR( "[RenderPassResource] XML missing root <RenderPassDesc> node: %#", absPath );
			return false;
		}

		if ( rapidxml::xml_node<>* nameNode = root->first_node( "_name" ) )
		{
			_desc._name = nameNode->value();
		}

		_desc._attachments.clear();
		if ( rapidxml::xml_node<>* attachsNode = root->first_node( "_attachments" ) )
		{
			for ( rapidxml::xml_node<>* attNode = attachsNode->first_node( "item" ); attNode; attNode = attNode->next_sibling( "item" ) )
			{
				RenderPassAttachment att{};

				if ( rapidxml::xml_node<>* n = attNode->first_node( "_name" ) )
					att._name = n->value();
				if ( rapidxml::xml_node<>* n = attNode->first_node( "_format" ) )
					att._format = n->value();
				if ( rapidxml::xml_node<>* n = attNode->first_node( "_bClear" ) )
					att._bClear = ( std::string( n->value() ) == "1" );
				if ( rapidxml::xml_node<>* n = attNode->first_node( "_clearColor" ) )
				{

					std::sscanf( n->value(), "%f,%f,%f,%f", &att._clearColor[0], &att._clearColor[1], &att._clearColor[2], &att._clearColor[3] );
				}

				_desc._attachments.push_back( std::move( att ) );
			}
		}

		SW_LOG_INFO( "[RenderPassResource] Loaded RenderPass '%#' (Attachments: %#)", _desc._name, _desc._attachments.size() );
		return true;
	}

	bool RenderPassResource::saveToXmlFile( const std::string& assetRelativePath ) const
	{
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
		{
			absPath = assetRelativePath;
		}

		rapidxml::xml_document<> doc;

		rapidxml::xml_node<>* decl = doc.allocate_node( rapidxml::node_declaration );
		decl->append_attribute( doc.allocate_attribute( "version", "1.0" ) );
		decl->append_attribute( doc.allocate_attribute( "encoding", "utf-8" ) );
		doc.append_node( decl );

		rapidxml::xml_node<>* root = doc.allocate_node( rapidxml::node_element, "RenderPassDesc" );
		doc.append_node( root );

		rapidxml::xml_node<>* nameNode = doc.allocate_node( rapidxml::node_element, "_name", doc.allocate_string( _desc._name.c_str() ) );
		root->append_node( nameNode );

		rapidxml::xml_node<>* attachsNode = doc.allocate_node( rapidxml::node_element, "_attachments" );
		root->append_node( attachsNode );

		for ( const RenderPassAttachment& att : _desc._attachments )
		{
			rapidxml::xml_node<>* attNode = doc.allocate_node( rapidxml::node_element, "item" );
			attachsNode->append_node( attNode );

			attNode->append_node( doc.allocate_node( rapidxml::node_element, "_name", doc.allocate_string( att._name.c_str() ) ) );
			attNode->append_node( doc.allocate_node( rapidxml::node_element, "_format", doc.allocate_string( att._format.c_str() ) ) );
			attNode->append_node( doc.allocate_node( rapidxml::node_element, "_bClear", doc.allocate_string( att._bClear ? "1" : "0" ) ) );

			sw::StringBuilder<constant::kMaxBuffer128> colorSS;
			const Format							   colorFmt( 4 );
			colorSS.appendFormat( "%#,%#,%#,%#",
								  Fmt( att._clearColor[0], colorFmt ),
								  Fmt( att._clearColor[1], colorFmt ),
								  Fmt( att._clearColor[2], colorFmt ),
								  Fmt( att._clearColor[3], colorFmt ) );
			attNode->append_node( doc.allocate_node( rapidxml::node_element, "_clearColor", doc.allocate_string( colorSS.c_str() ) ) );
		}

		std::string xmlStr;
		rapidxml::print( std::back_inserter( xmlStr ), doc, 0 );

		if ( FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( xmlStr.data() ), static_cast<uint64>( xmlStr.size() ) ) == false )
		{
			SW_LOG_ERROR( "[RenderPassResource] Failed to write XML file: %#", absPath );
			return false;
		}

		SW_LOG_INFO( "[RenderPassResource] Saved RenderPass '%#' to: %#", _desc._name, absPath );
		return true;
	}

	TaskHandle RenderPassResource::loadFromXmlFileAsync( const std::string& assetRelativePath )
	{
		return sw::getTaskManager().emplaceTask( "LoadRenderPassAsync", SW_DELEGATE_LAMBDA( TaskDelegate, [this, assetRelativePath]()
		{
			this->loadFromXmlFile( assetRelativePath );
		} ) );
	}
} // namespace sw
