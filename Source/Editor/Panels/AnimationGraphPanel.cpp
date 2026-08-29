#include "pch.h"

#include "Editor/Panels/AnimationGraphPanel.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
	namespace
	{
		struct AnimationGraphPanelInternal
		{
			static int32 pinIn( int32 nodeId )
			{
				return nodeId * 10 + 1;
			}
			static int32 pinOut( int32 nodeId )
			{
				return nodeId * 10 + 2;
			}

			static ed::NodeId toNodeId( int32 id )
			{
				return ed::NodeId( static_cast<uintptr_t>( id ) );
			}
			static ed::PinId toPinId( int32 id )
			{
				return ed::PinId( static_cast<uintptr_t>( id ) );
			}
			static ed::LinkId toLinkId( int32 id )
			{
				return ed::LinkId( static_cast<uintptr_t>( id ) );
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	SW_LOG_CALLER( "AnimationGraph" );

	AnimationGraphPanel::AnimationGraphPanel()
		: EditorDocumentPanel{ EditorAssetKind::AnimationGraph, true }
		, _nodeGraph{}
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
		if ( hasNewFocusedDocument() )
			acceptFocusedDocument();
		if ( isDocumentLoaded() == false )
			loadGraphData();

		if ( EditorChrome::beginToolbar( "##AnimGraphToolbar" ) )
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
				loadGraphData();
			ImGui::SameLine();
			if ( ImGui::Button( "Save" ) )
				saveGraphData();

			ImGui::SameLine();
			ImGui::TextDisabled( "Nodes: %zu  Links: %zu  (%s)", _listNode.size(), _listLink.size(),
								 EditorConfig::getActive()._animationGraphDataFile.c_str() );
		}
		EditorChrome::endToolbar();

		if ( _nodeGraph.beginCanvas( "AnimationGraphCanvas",
									 EditorConfig::getActive()._animationGraphSettingsFile.c_str() ) == false )
		{
			ImGui::TextUnformatted( "Failed to create Animation Graph editor context." );
			return;
		}

		for ( GraphNode& node : _listNode )
		{
			const ed::NodeId nodeId = AnimationGraphPanelInternal::toNodeId( node._id );
			ed::BeginNode( nodeId );
			ImGui::TextUnformatted( node._name.c_str() );
			ed::BeginPin( AnimationGraphPanelInternal::toPinId( AnimationGraphPanelInternal::pinIn( node._id ) ), ed::PinKind::Input );
			ImGui::TextUnformatted( "-> In" );
			ed::EndPin();
			ImGui::SameLine();
			ed::BeginPin( AnimationGraphPanelInternal::toPinId( AnimationGraphPanelInternal::pinOut( node._id ) ), ed::PinKind::Output );
			ImGui::TextUnformatted( "Out ->" );
			ed::EndPin();
			ed::EndNode();

			if ( _nodeGraph.needsContentFit() )
				ed::SetNodePosition( nodeId, ImVec2( node._x, node._y ) );
		}

		for ( const GraphLink& link : _listLink )
		{
			ed::Link( AnimationGraphPanelInternal::toLinkId( link._id ), AnimationGraphPanelInternal::toPinId( AnimationGraphPanelInternal::pinOut( link._fromNode ) ), AnimationGraphPanelInternal::toPinId( AnimationGraphPanelInternal::pinIn( link._toNode ) ) );
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
			const ImVec2 pos = ed::GetNodePosition( AnimationGraphPanelInternal::toNodeId( node._id ) );
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
		EditorAnimGraphData data;
		if ( EditorToolAssetCommands::loadAnimationGraph( data, getLoadedAssetPath() ) )
		{
			_listNode = std::move( data._listNode );
			_listLink = std::move( data._listLink );
		}
		if ( _listNode.empty() )
			ensureDefaults();
		markDocumentLoaded();
		_nodeGraph.requestContentFit();
	}

	void AnimationGraphPanel::saveGraphData()
	{
		EditorAnimGraphData previous;
		const bool			bHadFile   = EditorToolAssetCommands::loadAnimationGraph( previous, getLoadedAssetPath() );
		const string		beforeJson = bHadFile ? EditorToolAssetCommands::serializeAnimationGraph( previous ) : string{};

		EditorAnimGraphData data;
		data._listNode.reserve( _listNode.size() );
		data._listLink = _listLink;
		for ( const GraphNode& node : _listNode )
		{
			GraphNode saved = node;
			if ( _nodeGraph.bind() )
			{
				const ImVec2 pos = ed::GetNodePosition( AnimationGraphPanelInternal::toNodeId( node._id ) );
				saved._x		 = pos.x;
				saved._y		 = pos.y;
				_nodeGraph.unbind();
			}
			data._listNode.push_back( std::move( saved ) );
		}
		EditorToolAssetCommands::saveAnimationGraph( data, getLoadedAssetPath() );
		const string afterJson = EditorToolAssetCommands::serializeAnimationGraph( data );
		const string path	   = getLoadedAssetPath();
		EditorTransaction::recordDocumentText(
			beforeJson, afterJson, "Save Animation Graph",
			SW_DELEGATE_LAMBDA( EditorDocumentRestoreDelegate, [this, path]( string_view snapshot )
		{
			EditorAnimGraphData restored;
			if ( snapshot.empty() == false )
				EditorToolAssetCommands::parseAnimationGraph( snapshot, restored );
			EditorToolAssetCommands::saveAnimationGraph( restored, path );
			if ( getLoadedAssetPath() != path )
				return;
			_listNode = std::move( restored._listNode );
			_listLink = std::move( restored._listLink );
			if ( _listNode.empty() )
				ensureDefaults();
			_nodeGraph.requestContentFit();
		} ) );
	}

	int32 AnimationGraphPanel::nextNodeId() const
	{
		return nextItemId( _listNode );
	}

	int32 AnimationGraphPanel::nextLinkId() const
	{
		return nextItemId( _listLink );
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
