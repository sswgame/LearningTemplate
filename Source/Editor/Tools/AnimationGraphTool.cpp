/**
 * @file AnimationGraphTool.cpp
 */
#include "Tools/AnimationGraphTool.h"
#include "EditorUtil.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>
#include <sstream>
#include <cstdio>

namespace ed = ax::NodeEditor;

namespace sw
{
	namespace
	{
		int32 pinIn( int32 nodeId ) { return nodeId * 10 + 1; }
		int32 pinOut( int32 nodeId ) { return nodeId * 10 + 2; }

		std::string escapeJson( const std::string& s )
		{
			std::string out;
			out.reserve( s.size() + 8 );
			for ( char c : s )
			{
				if ( c == '\\' || c == '"' )
					out.push_back( '\\' );
				out.push_back( c );
			}
			return out;
		}
	} // namespace

	AnimationGraphTool::AnimationGraphTool()
		: IEditorWindow( false )
	{
	}

	AnimationGraphTool::~AnimationGraphTool()
	{
		destroyEditor();
	}

	void AnimationGraphTool::ensureDefaults()
	{
		if ( _nodes.empty() == false )
			return;
		_nodes.push_back( GraphNode{ 1, "Idle", 40.0f, 40.0f } );
		_nodes.push_back( GraphNode{ 2, "Walk", 280.0f, 80.0f } );
		_links.push_back( GraphLink{ 100, 1, 2 } );
	}

	int32 AnimationGraphTool::nextNodeId() const
	{
		int32 maxId = 0;
		for ( const GraphNode& n : _nodes )
			maxId = std::max( maxId, n.id );
		return maxId + 1;
	}

	int32 AnimationGraphTool::nextLinkId() const
	{
		int32 maxId = 0;
		for ( const GraphLink& l : _links )
			maxId = std::max( maxId, l.id );
		return maxId + 1;
	}

	void AnimationGraphTool::addNamedNode( const char* name )
	{
		GraphNode n{};
		n.id   = nextNodeId();
		n.name = name ? name : "Node";
		n.x	   = 40.0f + static_cast<float32>( _nodes.size() ) * 40.0f;
		n.y	   = 40.0f + static_cast<float32>( _nodes.size() ) * 30.0f;
		_nodes.push_back( std::move( n ) );
	}

	void AnimationGraphTool::loadGraphData()
	{
		const std::filesystem::path path = EditorUtil::resolveEditorConfigFile( "AnimationGraphData.json" );
		if ( path.empty() || FileUtil::isFileExist( path.string() ) == false )
		{
			ensureDefaults();
			_bLoaded = true;
			return;
		}

		std::vector<uint8> data;
		if ( FileUtil::readFile( path.string(), data ) == false || data.empty() )
		{
			ensureDefaults();
			_bLoaded = true;
			return;
		}

		const std::string json( data.begin(), data.end() );
		_nodes.clear();
		_links.clear();

		size_t nodesPos = json.find( "\"nodes\"" );
		if ( nodesPos != std::string::npos )
		{
			size_t arr = json.find( '[', nodesPos );
			size_t end = json.find( ']', arr );
			if ( arr != std::string::npos && end != std::string::npos )
			{
				size_t cursor = arr;
				while ( true )
				{
					const size_t obj = json.find( '{', cursor );
					if ( obj == std::string::npos || obj > end )
						break;
					GraphNode n{};
					const size_t idPos = json.find( "\"id\"", obj );
					if ( idPos != std::string::npos && idPos < end )
					{
						const size_t colon = json.find( ':', idPos );
						n.id			   = std::atoi( json.c_str() + colon + 1 );
					}
					const size_t namePos = json.find( "\"name\"", obj );
					if ( namePos != std::string::npos && namePos < end )
					{
						const size_t q0 = json.find( '"', json.find( ':', namePos ) + 1 );
						const size_t q1 = json.find( '"', q0 + 1 );
						if ( q0 != std::string::npos && q1 != std::string::npos )
							n.name.assign( json, q0 + 1, q1 - q0 - 1 );
					}
					const size_t xPos = json.find( "\"x\"", obj );
					if ( xPos != std::string::npos && xPos < end )
					{
						const size_t colon = json.find( ':', xPos );
						n.x				   = static_cast<float32>( std::atof( json.c_str() + colon + 1 ) );
					}
					const size_t yPos = json.find( "\"y\"", obj );
					if ( yPos != std::string::npos && yPos < end )
					{
						const size_t colon = json.find( ':', yPos );
						n.y				   = static_cast<float32>( std::atof( json.c_str() + colon + 1 ) );
					}
					if ( n.id > 0 )
						_nodes.push_back( n );
					cursor = json.find( '}', obj );
					if ( cursor == std::string::npos )
						break;
					++cursor;
				}
			}
		}

		size_t linksPos = json.find( "\"links\"" );
		if ( linksPos != std::string::npos )
		{
			size_t arr = json.find( '[', linksPos );
			size_t end = json.find( ']', arr );
			if ( arr != std::string::npos && end != std::string::npos )
			{
				size_t cursor = arr;
				while ( true )
				{
					const size_t obj = json.find( '{', cursor );
					if ( obj == std::string::npos || obj > end )
						break;
					GraphLink l{};
					auto parseInt = [&]( const char* key, int32& out )
					{
						const size_t p = json.find( std::string( "\"" ) + key + "\"", obj );
						if ( p == std::string::npos || p > end )
							return;
						const size_t colon = json.find( ':', p );
						out				   = std::atoi( json.c_str() + colon + 1 );
					};
					parseInt( "id", l.id );
					parseInt( "from", l.fromNode );
					parseInt( "to", l.toNode );
					if ( l.id > 0 )
						_links.push_back( l );
					cursor = json.find( '}', obj );
					if ( cursor == std::string::npos )
						break;
					++cursor;
				}
			}
		}

		if ( _nodes.empty() )
			ensureDefaults();
		_bLoaded			 = true;
		_bNavigatedToContent = false;
	}

	void AnimationGraphTool::saveGraphData() const
	{
		const std::filesystem::path path = EditorUtil::resolveEditorConfigFile( "AnimationGraphData.json" );
		if ( path.empty() )
			return;

		// Pull live node positions from editor if available
		std::ostringstream oss;
		oss << "{\n  \"nodes\": [\n";
		for ( size_t i = 0; i < _nodes.size(); ++i )
		{
			const GraphNode& n = _nodes[i];
			float32			 x = n.x;
			float32			 y = n.y;
			if ( _editor != nullptr )
			{
				ed::SetCurrentEditor( _editor );
				const ImVec2 pos = ed::GetNodePosition( ed::NodeId( n.id ) );
				x				 = pos.x;
				y				 = pos.y;
				ed::SetCurrentEditor( nullptr );
			}
			oss << "    { \"id\": " << n.id << ", \"name\": \"" << escapeJson( n.name ) << "\", \"x\": " << x
				<< ", \"y\": " << y << " }";
			if ( i + 1 < _nodes.size() )
				oss << ",";
			oss << "\n";
		}
		oss << "  ],\n  \"links\": [\n";
		for ( size_t i = 0; i < _links.size(); ++i )
		{
			const GraphLink& l = _links[i];
			oss << "    { \"id\": " << l.id << ", \"from\": " << l.fromNode << ", \"to\": " << l.toNode << " }";
			if ( i + 1 < _links.size() )
				oss << ",";
			oss << "\n";
		}
		oss << "  ]\n}\n";

		const std::string text = oss.str();
		if ( FileUtil::writeFile( path.string(), reinterpret_cast<const uint8*>( text.data() ),
								  static_cast<uint64>( text.size() ) ) )
			SW_LOG_INFO( "[AnimationGraph] Saved %#", path.string().c_str() );
	}

	void AnimationGraphTool::ensureEditor()
	{
		if ( _editor != nullptr )
			return;

		ed::Config config{};
		const std::filesystem::path settingsPath =
			EditorUtil::resolveEditorConfigFile( "AnimationGraph.json" );
		if ( settingsPath.empty() == false )
		{
			_settingsPath		= settingsPath.string();
			config.SettingsFile = _settingsPath.c_str();
		}
		_editor = ed::CreateEditor( &config );
	}

	void AnimationGraphTool::destroyEditor()
	{
		if ( _editor == nullptr )
			return;
		ed::DestroyEditor( _editor );
		_editor				 = nullptr;
		_bNavigatedToContent = false;
	}

	void AnimationGraphTool::shutdown( IRHIDevice* /*rhiDevice*/ )
	{
		destroyEditor();
	}

	void AnimationGraphTool::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		if ( _bLoaded == false )
			loadGraphData();

		if ( ImGui::Button( "Add Idle" ) )
			addNamedNode( "Idle" );
		ImGui::SameLine();
		if ( ImGui::Button( "Add Walk" ) )
			addNamedNode( "Walk" );
		ImGui::SameLine();
		if ( ImGui::Button( "Add Attack" ) )
			addNamedNode( "Attack" );
		ImGui::SameLine();
		if ( ImGui::Button( "Link Selected" ) && _nodes.size() >= 2 )
		{
			// Link first two selected-looking nodes: last two in list as a simple authoring aid
			GraphLink l{};
			l.id	   = nextLinkId();
			l.fromNode = _nodes[_nodes.size() - 2].id;
			l.toNode   = _nodes[_nodes.size() - 1].id;
			_links.push_back( l );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Load" ) )
		{
			_bLoaded = false;
			loadGraphData();
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Save" ) )
			saveGraphData();

		ImGui::TextDisabled( "Nodes: %zu  Links: %zu  (AnimationGraphData.json)", _nodes.size(), _links.size() );

		ensureEditor();
		if ( _editor == nullptr )
		{
			ImGui::TextUnformatted( "Failed to create Animation Graph editor context." );
			ImGui::End();
			return;
		}

		ed::SetCurrentEditor( _editor );
		ed::Begin( "AnimationGraphCanvas" );

		for ( GraphNode& n : _nodes )
		{
			const ed::NodeId nodeId( n.id );
			ed::BeginNode( nodeId );
			ImGui::TextUnformatted( n.name.c_str() );
			ed::BeginPin( ed::PinId( pinIn( n.id ) ), ed::PinKind::Input );
			ImGui::TextUnformatted( "-> In" );
			ed::EndPin();
			ImGui::SameLine();
			ed::BeginPin( ed::PinId( pinOut( n.id ) ), ed::PinKind::Output );
			ImGui::TextUnformatted( "Out ->" );
			ed::EndPin();
			ed::EndNode();

			if ( _bNavigatedToContent == false )
				ed::SetNodePosition( nodeId, ImVec2( n.x, n.y ) );
		}

		for ( const GraphLink& l : _links )
			ed::Link( ed::LinkId( l.id ), ed::PinId( pinOut( l.fromNode ) ), ed::PinId( pinIn( l.toNode ) ) );

		if ( ed::BeginCreate() )
		{
			ed::PinId a;
			ed::PinId b;
			if ( ed::QueryNewLink( &a, &b ) )
			{
				if ( a && b && ed::AcceptNewItem() )
				{
					GraphLink link{};
					link.id		  = nextLinkId();
					const int32 ap = static_cast<int32>( a.Get() );
					const int32 bp = static_cast<int32>( b.Get() );
					// Resolve node ids from pin ids
					const int32 aNode = ap / 10;
					const int32 bNode = bp / 10;
					if ( ( ap % 10 ) == 2 )
					{
						link.fromNode = aNode;
						link.toNode	  = bNode;
					}
					else
					{
						link.fromNode = bNode;
						link.toNode	  = aNode;
					}
					_links.push_back( link );
				}
			}
			ed::EndCreate();
		}

		if ( ed::BeginDelete() )
		{
			ed::LinkId linkId;
			while ( ed::QueryDeletedLink( &linkId ) )
			{
				if ( ed::AcceptDeletedItem() )
				{
					const int32 id = static_cast<int32>( linkId.Get() );
					_links.erase( std::remove_if( _links.begin(), _links.end(),
												  [id]( const GraphLink& l )
												  { return l.id == id; } ),
								  _links.end() );
				}
			}
			ed::NodeId nodeId;
			while ( ed::QueryDeletedNode( &nodeId ) )
			{
				if ( ed::AcceptDeletedItem() )
				{
					const int32 id = static_cast<int32>( nodeId.Get() );
					_nodes.erase( std::remove_if( _nodes.begin(), _nodes.end(),
												  [id]( const GraphNode& n )
												  { return n.id == id; } ),
								  _nodes.end() );
					_links.erase( std::remove_if( _links.begin(), _links.end(),
												  [id]( const GraphLink& l )
												  { return l.fromNode == id || l.toNode == id; } ),
								  _links.end() );
				}
			}
			ed::EndDelete();
		}

		if ( _bNavigatedToContent == false )
		{
			ed::NavigateToContent( 0.1f );
			_bNavigatedToContent = true;
		}

		// Cache positions for save
		for ( GraphNode& n : _nodes )
		{
			const ImVec2 pos = ed::GetNodePosition( ed::NodeId( n.id ) );
			n.x				 = pos.x;
			n.y				 = pos.y;
		}

		ed::End();
		ed::SetCurrentEditor( nullptr );
		ImGui::End();
	}
} // namespace sw
