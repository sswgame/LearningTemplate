#include "pch.h"

#include "Editor/Tools/AnimationGraphTool.h"

#include "Editor/Config/EditorConfig.h"
#include "Editor/EditorUtil.h"

#include "Engine/Serialization/Format/JsonSerializer.h"

#include "RuntimeAPI/EditorUIContext.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw
{

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

	AnimationGraphTool::AnimationGraphTool()
		: BaseNodeGraphEditor{ false }
		, _bLoaded{ false }
		, _listNodes{}
		, _listLinks{}
	{
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
		if ( ImGui::Button( "Link Selected" ) && _listNodes.size() >= 2 )
		{
			// 선택돼 보이는 앞 두 노드를 연결합니다: 목록의 마지막 두 개를 간단한 작성 보조로 씁니다.
			GraphLink l{};
			l._id		= nextLinkId();
			l._fromNode = _listNodes[_listNodes.size() - 2]._id;
			l._toNode	= _listNodes[_listNodes.size() - 1]._id;
			_listLinks.push_back( l );
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

		ImGui::TextDisabled( "Nodes: %zu  Links: %zu  (%s)", _listNodes.size(), _listLinks.size(),
							 EditorConfig::getActive()._animationGraphDataFile.c_str() );

		ensureEditorContext( EditorConfig::getActive()._animationGraphSettingsFile.c_str() );
		if ( _pEditor == nullptr )
		{
			ImGui::TextUnformatted( "Failed to create Animation Graph editor context." );
			ImGui::End();
			return;
		}

		ed::SetCurrentEditor( _pEditor );
		ed::Begin( "AnimationGraphCanvas" );

		for ( GraphNode& node : _listNodes )
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

			if ( _bNavigatedToContent == false )
				ed::SetNodePosition( nodeId, ImVec2( node._x, node._y ) );
		}

		for ( const GraphLink& link : _listLinks )
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
					_listLinks.push_back( link );
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
					_listLinks.erase( std::remove_if( _listLinks.begin(), _listLinks.end(),
													  [id]( const GraphLink& link )
					{ return link._id == id; } ),
									  _listLinks.end() );
				}
			}
			ed::NodeId nodeId;
			while ( ed::QueryDeletedNode( &nodeId ) )
			{
				if ( ed::AcceptDeletedItem() )
				{
					const int32 id = static_cast<int32>( nodeId.Get() );
					_listNodes.erase( std::remove_if( _listNodes.begin(), _listNodes.end(),
													  [id]( const GraphNode& node )
					{ return node._id == id; } ),
									  _listNodes.end() );
					_listLinks.erase( std::remove_if( _listLinks.begin(), _listLinks.end(),
													  [id]( const GraphLink& link )
					{ return link._fromNode == id || link._toNode == id; } ),
									  _listLinks.end() );
				}
			}
			ed::EndDelete();
		}

		if ( _bNavigatedToContent == false )
		{
			ed::NavigateToContent( 0.1f );
			_bNavigatedToContent = true;
		}

		// 저장용 위치를 캐시합니다.
		for ( GraphNode& node : _listNodes )
		{
			const ImVec2 pos = ed::GetNodePosition( toNodeId( node._id ) );
			node._x			 = pos.x;
			node._y			 = pos.y;
		}

		ed::End();
		ed::SetCurrentEditor( nullptr );
		ImGui::End();
	}

	void AnimationGraphTool::ensureDefaults()
	{
		if ( _listNodes.empty() == false )
			return;
		_listNodes.push_back( GraphNode{ 1, "Idle", 40.0f, 40.0f } );
		_listNodes.push_back( GraphNode{ 2, "Walk", 280.0f, 80.0f } );
		_listLinks.push_back( GraphLink{ 100, 1, 2 } );
	}

	void AnimationGraphTool::loadGraphData()
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
		_listNodes.clear();
		_listLinks.clear();

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
						_listNodes.push_back( n );
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
						_listLinks.push_back( l );
					cursor = json.find( '}', obj );
					if ( cursor == string::npos )
						break;
					++cursor;
				}
			}
		}

		if ( _listNodes.empty() )
			ensureDefaults();
		_bLoaded			 = true;
		_bNavigatedToContent = false;
	}

	void AnimationGraphTool::saveGraphData() const
	{
		const string path = EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._animationGraphDataFile.c_str() );
		if ( path.empty() )
			return;

		// 가능하면 에디터에서 라이브 노드 위치를 가져옵니다.
		StringBuilder<2048> sb;
		sb.append( "{\n  \"nodes\": [\n" );
		for ( size_t nodeIndex = 0; nodeIndex < _listNodes.size(); ++nodeIndex )
		{
			const GraphNode& n = _listNodes[nodeIndex];
			float32			 x = n._x;
			float32			 y = n._y;
			if ( _pEditor != nullptr )
			{
				ed::SetCurrentEditor( _pEditor );
				const ImVec2 pos = ed::GetNodePosition( toNodeId( n._id ) );
				x				 = pos.x;
				y				 = pos.y;
				ed::SetCurrentEditor( nullptr );
			}
			sb.append( "    { \"id\": " ).append( n._id ).append( ", \"name\": \"" ).append( JsonSerializer::escapeString( n._name ).c_str() ).append( "\", \"x\": " ).append( x ).append( ", \"y\": " ).append( y ).append( " }" );
			if ( nodeIndex + 1 < _listNodes.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ],\n  \"links\": [\n" );
		for ( size_t linkIndex = 0; linkIndex < _listLinks.size(); ++linkIndex )
		{
			const GraphLink& l = _listLinks[linkIndex];
			sb.append( "    { \"id\": " ).append( l._id ).append( ", \"from\": " ).append( l._fromNode ).append( ", \"to\": " ).append( l._toNode ).append( " }" );
			if ( linkIndex + 1 < _listLinks.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ]\n}\n" );

		const string text( sb.c_str() );
		if ( FileUtil::writeFile( path, reinterpret_cast<const uint8*>( text.data() ),
								  text.size() ) )
			SW_LOG_INFO( "[AnimationGraph] Saved %#", path.c_str() );
	}

	int32 AnimationGraphTool::nextNodeId() const
	{
		int32 maxId{ 0 };
		for ( const GraphNode& node : _listNodes )
		{
			maxId = MathUtil::max( maxId, node._id );
		}
		return maxId + 1;
	}

	int32 AnimationGraphTool::nextLinkId() const
	{
		int32 maxId{ 0 };
		for ( const GraphLink& link : _listLinks )
		{
			maxId = MathUtil::max( maxId, link._id );
		}
		return maxId + 1;
	}

	void AnimationGraphTool::addNamedNode( const utf8* pName )
	{
		GraphNode n{};
		n._id	= nextNodeId();
		n._name = ( pName != nullptr ) ? pName : "Node";
		n._x	= 40.0f + static_cast<float32>( _listNodes.size() ) * 40.0f;
		n._y	= 40.0f + static_cast<float32>( _listNodes.size() ) * 30.0f;
		_listNodes.push_back( std::move( n ) );
	}
} // namespace sw
