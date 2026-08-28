#include "pch.h"

#include "Editor/Panels/AnimationGraphPanel.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Engine/Serialization/Format/JsonSerializer.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
	SW_LOG_CALLER( "AnimationGraph" );

	namespace
	{
		int32 pinIn( int32 nodeId )
		{
			return nodeId * 10 + 1;
		}
		int32 pinOut( int32 nodeId )
		{
			return nodeId * 10 + 2;
		}

		ed::NodeId toNodeId( int32 id )
		{
			return ed::NodeId( static_cast<uintptr_t>( id ) );
		}
		ed::PinId toPinId( int32 id )
		{
			return ed::PinId( static_cast<uintptr_t>( id ) );
		}
		ed::LinkId toLinkId( int32 id )
		{
			return ed::LinkId( static_cast<uintptr_t>( id ) );
		}

	} // namespace

	AnimationGraphPanel::AnimationGraphPanel()
		: IEditorPanel{ false }
		, _nodeGraph{}
		, _bLoaded{ false }
		, _listNode{}
		, _listLink{}
	{
	}

	void AnimationGraphPanel::shutdown( IRHIDevice* /*pRhiDevice*/ )
	{
		_nodeGraph.shutdown();
	}

	void AnimationGraphPanel::drawContent()
	{
		if ( _bLoaded == false )
			loadGraphData();

		if ( editor::beginToolbar( "##AnimGraphToolbar" ) )
		{
			if ( ImGui::Button( "Add Idle" ) )
				addNamedNode( "Idle" );
			ImGui::SameLine();
			if ( ImGui::Button( "Add Walk" ) )
				addNamedNode( "Walk" );
			ImGui::SameLine();
			if ( ImGui::Button( "Add Attack" ) )
				addNamedNode( "Attack" );
			ImGui::SameLine();
			if ( ImGui::Button( "Link Selected" ) && _listNode.size() >= 2 )
			{
				GraphLink l{};
				l._id		= nextLinkId();
				l._fromNode = _listNode[_listNode.size() - 2]._id;
				l._toNode	= _listNode[_listNode.size() - 1]._id;
				_listLink.push_back( l );
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

			ImGui::SameLine();
			ImGui::TextDisabled( "Nodes: %zu  Links: %zu  (%s)", _listNode.size(), _listLink.size(),
								 EditorConfig::getActive()._animationGraphDataFile.c_str() );
		}
		editor::endToolbar();

		if ( _nodeGraph.beginCanvas( "AnimationGraphCanvas",
									 EditorConfig::getActive()._animationGraphSettingsFile.c_str() ) == false )
		{
			ImGui::TextUnformatted( "Failed to create Animation Graph editor context." );
			return;
		}

		for ( GraphNode& node : _listNode )
		{
			const ed::NodeId nodeId = toNodeId( node._id );
			ed::BeginNode( nodeId );
			ImGui::TextUnformatted( node._name.c_str() );
			ed::BeginPin( toPinId( pinIn( node._id ) ), ed::PinKind::Input );
			ImGui::TextUnformatted( "-> In" );
			ed::EndPin();
			ImGui::SameLine();
			ed::BeginPin( toPinId( pinOut( node._id ) ), ed::PinKind::Output );
			ImGui::TextUnformatted( "Out ->" );
			ed::EndPin();
			ed::EndNode();

			if ( _nodeGraph.needsContentFit() )
				ed::SetNodePosition( nodeId, ImVec2( node._x, node._y ) );
		}

		for ( const GraphLink& link : _listLink )
		{
			ed::Link( toLinkId( link._id ), toPinId( pinOut( link._fromNode ) ), toPinId( pinIn( link._toNode ) ) );
		}

		if ( ed::BeginCreate() )
		{
			ed::PinId a;
			ed::PinId b;
			if ( ed::QueryNewLink( &a, &b ) )
			{
				if ( a.Get() != 0 && b.Get() != 0 && ed::AcceptNewItem() )
				{
					GraphLink link{};
					link._id	   = nextLinkId();
					const int32 ap = static_cast<int32>( a.Get() );
					const int32 bp = static_cast<int32>( b.Get() );
					// 핀 ID에서 노드 ID를 해석합니다.
					const int32 aNode = ap / 10;
					const int32 bNode = bp / 10;
					if ( ( ap % 10 ) == 2 )
					{
						link._fromNode = aNode;
						link._toNode   = bNode;
					}
					else
					{
						link._fromNode = bNode;
						link._toNode   = aNode;
					}
					_listLink.push_back( link );
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
					_listLink.erase( std::remove_if( _listLink.begin(), _listLink.end(),
													 [id]( const GraphLink& link )
					{ return link._id == id; } ),
									 _listLink.end() );
				}
			}
			ed::NodeId nodeId;
			while ( ed::QueryDeletedNode( &nodeId ) )
			{
				if ( ed::AcceptDeletedItem() )
				{
					const int32 id = static_cast<int32>( nodeId.Get() );
					_listNode.erase( std::remove_if( _listNode.begin(), _listNode.end(),
													 [id]( const GraphNode& node )
					{ return node._id == id; } ),
									 _listNode.end() );
					_listLink.erase( std::remove_if( _listLink.begin(), _listLink.end(),
													 [id]( const GraphLink& link )
					{ return link._fromNode == id || link._toNode == id; } ),
									 _listLink.end() );
				}
			}
			ed::EndDelete();
		}

		_nodeGraph.applyContentFitIfNeeded();

		// 저장용 위치를 캐시합니다.
		for ( GraphNode& node : _listNode )
		{
			const ImVec2 pos = ed::GetNodePosition( toNodeId( node._id ) );
			node._x			 = pos.x;
			node._y			 = pos.y;
		}

		_nodeGraph.endCanvas();
	}

	void AnimationGraphPanel::ensureDefaults()
	{
		if ( _listNode.empty() == false )
			return;
		_listNode.push_back( GraphNode{ 1, "Idle", 40.0f, 40.0f } );
		_listNode.push_back( GraphNode{ 2, "Walk", 280.0f, 80.0f } );
		_listLink.push_back( GraphLink{ 100, 1, 2 } );
	}

	void AnimationGraphPanel::loadGraphData()
	{
		const string path = EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._animationGraphDataFile.c_str() );
		if ( path.empty() || FileUtil::fileExists( path ) == false )
		{
			ensureDefaults();
			_bLoaded = true;
			return;
		}

		vector<uint8> listData;
		if ( FileUtil::readFile( path, listData ) == false || listData.empty() )
		{
			ensureDefaults();
			_bLoaded = true;
			return;
		}

		const string json( listData.begin(), listData.end() );
		_listNode.clear();
		_listLink.clear();

		size_t nodesPos = json.find( "\"nodes\"" );
		if ( nodesPos != string::npos )
		{
			size_t arr = json.find( '[', nodesPos );
			size_t end = json.find( ']', arr );
			if ( arr != string::npos && end != string::npos )
			{
				size_t cursor = arr;
				while ( true )
				{
					const size_t obj = json.find( '{', cursor );
					if ( obj == string::npos || obj > end )
						break;
					GraphNode	 n{};
					const size_t idPos = json.find( "\"id\"", obj );
					if ( idPos != string::npos && idPos < end )
					{
						const size_t colon = json.find( ':', idPos );
						utf8*		 pEndPtr{ nullptr };
						n._id = static_cast<int32>( StringUtil::strtoll( json.c_str() + colon + 1, &pEndPtr, 10 ) );
					}
					const size_t namePos = json.find( "\"name\"", obj );
					if ( namePos != string::npos && namePos < end )
					{
						const size_t q0 = json.find( '"', json.find( ':', namePos ) + 1 );
						const size_t q1 = json.find( '"', q0 + 1 );
						if ( q0 != string::npos && q1 != string::npos )
							n._name.assign( json, q0 + 1, q1 - q0 - 1 );
					}
					const size_t xPos = json.find( "\"x\"", obj );
					if ( xPos != string::npos && xPos < end )
					{
						const size_t colon = json.find( ':', xPos );
						n._x			   = static_cast<float32>( StringUtil::atof( json.c_str() + colon + 1 ) );
					}
					const size_t yPos = json.find( "\"y\"", obj );
					if ( yPos != string::npos && yPos < end )
					{
						const size_t colon = json.find( ':', yPos );
						n._y			   = static_cast<float32>( StringUtil::atof( json.c_str() + colon + 1 ) );
					}
					if ( n._id > 0 )
						_listNode.push_back( n );
					cursor = json.find( '}', obj );
					if ( cursor == string::npos )
						break;
					++cursor;
				}
			}
		}

		size_t linksPos = json.find( "\"links\"" );
		if ( linksPos != string::npos )
		{
			size_t arr = json.find( '[', linksPos );
			size_t end = json.find( ']', arr );
			if ( arr != string::npos && end != string::npos )
			{
				size_t cursor = arr;
				while ( true )
				{
					const size_t obj = json.find( '{', cursor );
					if ( obj == string::npos || obj > end )
						break;
					GraphLink l{};
					auto	  parseInt = [&]( const utf8* pKey, int32& out )
					{
						const size_t p = json.find( string( "\"" ) + pKey + "\"", obj );
						if ( p == string::npos || p > end )
							return;
						const size_t colon = json.find( ':', p );
						utf8*		 pEndPtr{ nullptr };
						out = static_cast<int32>( StringUtil::strtoll( json.c_str() + colon + 1, &pEndPtr, 10 ) );
					};
					parseInt( "id", l._id );
					parseInt( "from", l._fromNode );
					parseInt( "to", l._toNode );
					if ( l._id > 0 )
						_listLink.push_back( l );
					cursor = json.find( '}', obj );
					if ( cursor == string::npos )
						break;
					++cursor;
				}
			}
		}

		if ( _listNode.empty() )
			ensureDefaults();
		_bLoaded = true;
		_nodeGraph.requestContentFit();
	}

	void AnimationGraphPanel::saveGraphData() const
	{
		const string path = EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._animationGraphDataFile.c_str() );
		if ( path.empty() )
			return;

		// 가능하면 에디터에서 라이브 노드 위치를 가져옵니다.
		StringBuilder<2048> sb;
		sb.append( "{\n  \"nodes\": [\n" );
		for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
		{
			const GraphNode& n = _listNode[nodeIndex];
			float32			 x = n._x;
			float32			 y = n._y;
			if ( _nodeGraph.bind() )
			{
				const ImVec2 pos = ed::GetNodePosition( toNodeId( n._id ) );
				x				 = pos.x;
				y				 = pos.y;
				_nodeGraph.unbind();
			}
			sb.append( "    { \"id\": " ).append( n._id ).append( ", \"name\": \"" ).append( JsonSerializer::escapeString( n._name ).c_str() ).append( "\", \"x\": " ).append( x ).append( ", \"y\": " ).append( y ).append( " }" );
			if ( nodeIndex + 1 < _listNode.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ],\n  \"links\": [\n" );
		for ( size_t linkIndex = 0; linkIndex < _listLink.size(); ++linkIndex )
		{
			const GraphLink& l = _listLink[linkIndex];
			sb.append( "    { \"id\": " ).append( l._id ).append( ", \"from\": " ).append( l._fromNode ).append( ", \"to\": " ).append( l._toNode ).append( " }" );
			if ( linkIndex + 1 < _listLink.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ]\n}\n" );

		const string text( sb.c_str() );
		if ( FileUtil::writeFile( path, reinterpret_cast<const uint8*>( text.data() ),
								  text.size() ) )
			SW_LOG_INFO( "Saved %#", path.c_str() );
	}

	int32 AnimationGraphPanel::nextNodeId() const
	{
		int32 maxId{ 0 };
		for ( const GraphNode& node : _listNode )
		{
			maxId = MathUtil::max( maxId, node._id );
		}
		return maxId + 1;
	}

	int32 AnimationGraphPanel::nextLinkId() const
	{
		int32 maxId{ 0 };
		for ( const GraphLink& link : _listLink )
		{
			maxId = MathUtil::max( maxId, link._id );
		}
		return maxId + 1;
	}

	void AnimationGraphPanel::addNamedNode( const utf8* pName )
	{
		GraphNode n{};
		n._id	= nextNodeId();
		n._name = ( pName != nullptr ) ? pName : "Node";
		n._x	= 40.0f + static_cast<float32>( _listNode.size() ) * 40.0f;
		n._y	= 40.0f + static_cast<float32>( _listNode.size() ) * 30.0f;
		_listNode.push_back( std::move( n ) );
	}
} // namespace sw::editor
