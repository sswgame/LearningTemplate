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
	namespace
	{
		void parseStringList( rapidxml::xml_node<>* parent, const char* listName, std::vector<std::string>& out )
		{
			out.clear();
			if ( parent == nullptr )
				return;
			rapidxml::xml_node<>* list = parent->first_node( listName );
			if ( list == nullptr )
				return;
			for ( rapidxml::xml_node<>* item = list->first_node( "item" ); item; item = item->next_sibling( "item" ) )
			{
				if ( item->value() != nullptr && item->value()[0] != '\0' )
					out.emplace_back( item->value() );
				else if ( rapidxml::xml_attribute<>* attr = item->first_attribute( "name" ) )
					out.emplace_back( attr->value() );
			}
		}

		void parseGraphPasses( rapidxml::xml_node<>* passesNode, std::vector<RenderGraphPassDesc>& out )
		{
			out.clear();
			if ( passesNode == nullptr )
				return;

			for ( rapidxml::xml_node<>* passNode = passesNode->first_node( "item" ); passNode; passNode = passNode->next_sibling( "item" ) )
			{
				RenderGraphPassDesc pass{};
				if ( rapidxml::xml_node<>* n = passNode->first_node( "_name" ) )
					pass._name = n->value();
				else if ( rapidxml::xml_attribute<>* a = passNode->first_attribute( "name" ) )
					pass._name = a->value();

				if ( rapidxml::xml_node<>* n = passNode->first_node( "_type" ) )
					pass._type = n->value();
				else if ( rapidxml::xml_attribute<>* a = passNode->first_attribute( "type" ) )
					pass._type = a->value();

				parseStringList( passNode, "_inputs", pass._inputs );
				parseStringList( passNode, "_outputs", pass._outputs );
				out.push_back( std::move( pass ) );
			}

			// Also accept <Pass name="..." type="..."> children
			for ( rapidxml::xml_node<>* passNode = passesNode->first_node( "Pass" ); passNode; passNode = passNode->next_sibling( "Pass" ) )
			{
				RenderGraphPassDesc pass{};
				if ( rapidxml::xml_attribute<>* a = passNode->first_attribute( "name" ) )
					pass._name = a->value();
				if ( rapidxml::xml_attribute<>* a = passNode->first_attribute( "type" ) )
					pass._type = a->value();
				parseStringList( passNode, "inputs", pass._inputs );
				parseStringList( passNode, "outputs", pass._outputs );
				if ( pass._inputs.empty() )
					parseStringList( passNode, "_inputs", pass._inputs );
				if ( pass._outputs.empty() )
					parseStringList( passNode, "_outputs", pass._outputs );
				out.push_back( std::move( pass ) );
			}
		}

		void appendStringList( rapidxml::xml_document<>& doc, rapidxml::xml_node<>* parent, const char* listName, const std::vector<std::string>& values )
		{
			rapidxml::xml_node<>* list = doc.allocate_node( rapidxml::node_element, listName );
			parent->append_node( list );
			for ( const std::string& v : values )
				list->append_node( doc.allocate_node( rapidxml::node_element, "item", doc.allocate_string( v.c_str() ) ) );
		}
	} // namespace

	const std::vector<RenderGraphPassDesc>& RenderPassResource::getGraphPasses() const
	{
		if ( _desc._passes.empty() == false )
			return _desc._passes;
		return _desc._graph._passes;
	}

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
			root = doc.first_node( "RenderGraphDesc" );
		if ( root == nullptr )
		{
			SW_LOG_ERROR( "[RenderPassResource] XML missing root <RenderPassDesc>: %#", absPath );
			return false;
		}

		_desc = {};

		if ( rapidxml::xml_node<>* nameNode = root->first_node( "_name" ) )
			_desc._name = nameNode->value();
		else if ( rapidxml::xml_attribute<>* attr = root->first_attribute( "name" ) )
			_desc._name = attr->value();

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
					att._bClear = ( std::string( n->value() ) == "1" || std::string( n->value() ) == "true" );
				if ( rapidxml::xml_node<>* n = attNode->first_node( "_clearColor" ) )
				{
					if ( std::sscanf( n->value(), "%f,%f,%f,%f", &att._clearColor[0], &att._clearColor[1], &att._clearColor[2], &att._clearColor[3] ) < 4 )
						std::sscanf( n->value(), "%f %f %f %f", &att._clearColor[0], &att._clearColor[1], &att._clearColor[2], &att._clearColor[3] );
				}

				_desc._attachments.push_back( std::move( att ) );
			}
		}

		if ( rapidxml::xml_node<>* passesNode = root->first_node( "_passes" ) )
			parseGraphPasses( passesNode, _desc._passes );
		else if ( rapidxml::xml_node<>* passesNode = root->first_node( "passes" ) )
			parseGraphPasses( passesNode, _desc._passes );

		if ( rapidxml::xml_node<>* graphNode = root->first_node( "_graph" ) )
		{
			if ( rapidxml::xml_node<>* n = graphNode->first_node( "_name" ) )
				_desc._graph._name = n->value();
			if ( rapidxml::xml_node<>* passesNode = graphNode->first_node( "_passes" ) )
				parseGraphPasses( passesNode, _desc._graph._passes );
		}
		else if ( std::strcmp( root->name(), "RenderGraphDesc" ) == 0 )
		{
			_desc._graph._name = _desc._name;
			if ( _desc._passes.empty() == false )
				_desc._graph._passes = _desc._passes;
		}

		if ( _desc._graph._name.empty() )
			_desc._graph._name = _desc._name;

		SW_LOG_INFO( "[RenderPassResource] Loaded '%#' (Attachments: %#, Passes: %#)",
					 _desc._name, _desc._attachments.size(), getGraphPasses().size() );
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

		const std::vector<RenderGraphPassDesc>& passes = getGraphPasses();
		if ( passes.empty() == false )
		{
			rapidxml::xml_node<>* passesNode = doc.allocate_node( rapidxml::node_element, "_passes" );
			root->append_node( passesNode );
			for ( const RenderGraphPassDesc& pass : passes )
			{
				rapidxml::xml_node<>* passNode = doc.allocate_node( rapidxml::node_element, "item" );
				passesNode->append_node( passNode );
				passNode->append_node( doc.allocate_node( rapidxml::node_element, "_name", doc.allocate_string( pass._name.c_str() ) ) );
				passNode->append_node( doc.allocate_node( rapidxml::node_element, "_type", doc.allocate_string( pass._type.c_str() ) ) );
				appendStringList( doc, passNode, "_inputs", pass._inputs );
				appendStringList( doc, passNode, "_outputs", pass._outputs );
			}
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
		return sw::core::getTaskManager().emplaceTask( "LoadRenderPassAsync", SW_DELEGATE_LAMBDA( TaskDelegate, [this, assetRelativePath]()
		{
			this->loadFromXmlFile( assetRelativePath );
		} ) );
	}
} // namespace sw
