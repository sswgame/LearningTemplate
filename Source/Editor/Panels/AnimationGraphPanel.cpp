#include "pch.h"

#include "Editor/Panels/AnimationGraphPanel.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/EditorViewportPreview.h"
#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/EditorSessionPolicy.h"
#include "Editor/Common/Gui/EditorChrome.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/AnimationGraphAsset.h"

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
		, _previewPlayer{}
		, _listPreviewClip{}
		, _previewHoldSeconds{ 0.0f }
		, _bGraphLayoutReady{ SW_FALSE }
		, _bPreviewPlaying{ SW_FALSE }
		, _reservedGraph{ 0 }
	{
	}

	void AnimationGraphPanel::shutdown( IRHIDevice* /*pRhiDevice*/ )
	{
		_nodeGraph.shutdown();
	}

	void AnimationGraphPanel::drawContent()
	{
		updateFocusedDocument();
		if ( isDocumentLoaded() == false )
			loadGraphData();

		tickPreview( ImGui::GetIO().DeltaTime );

		if ( EditorChrome::beginToolbar( "##AnimGraphToolbar" ) )
		{
			if ( ImGui::Button( "Add Idle" ) )
			{
				addNamedNode( "Idle" );
				notifyDocumentEdited( "Add Animation Graph Node" );
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Add Walk" ) )
			{
				addNamedNode( "Walk" );
				notifyDocumentEdited( "Add Animation Graph Node" );
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Add Attack" ) )
			{
				addNamedNode( "Attack" );
				notifyDocumentEdited( "Add Animation Graph Node" );
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Link Selected" ) && _listNode.size() >= 2 )
			{
				GraphLink l{};
				l._id		= nextLinkId();
				l._fromNode = _listNode[_listNode.size() - 2]._id;
				l._toNode	= _listNode[_listNode.size() - 1]._id;
				_listLink.push_back( l );
				notifyDocumentEdited( "Link Animation Graph Nodes" );
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Load" ) )
				loadGraphData();
			ImGui::SameLine();
			if ( ImGui::Button( "Save" ) )
				saveGraphData();
			ImGui::SameLine();
			if ( ImGui::Button( "Play" ) )
			{
				syncPreviewGraph();
				_previewPlayer.play();
				_bPreviewPlaying	= SW_TRUE;
				_previewHoldSeconds = 0.0f;
				EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentNodeName(), getLoadedAssetPath() );
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Advance" ) )
			{
				syncPreviewGraph();
				if ( _previewPlayer.advance() == false )
					_bPreviewPlaying = SW_FALSE;
				else
					EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentNodeName(), getLoadedAssetPath() );
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Stop" ) )
			{
				_previewPlayer.stop();
				_bPreviewPlaying = SW_FALSE;
			}

			ImGui::SameLine();
			if ( _previewPlayer.getCurrentNodeName().empty() == false )
				ImGui::TextDisabled( "Preview: %s  Nodes: %zu  Links: %zu", _previewPlayer.getCurrentNodeName().c_str(),
									 _listNode.size(), _listLink.size() );
			else
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
			if ( _previewPlayer.getCurrentNodeName() == node._name && _previewPlayer.getCurrentNodeName().empty() == false )
				ImGui::TextColored( ImVec4( 0.4f, 0.9f, 0.5f, 1.0f ), "%s", node._name.c_str() );
			else
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
					link._id		  = nextLinkId();
					const int32 ap	  = static_cast<int32>( a.Get() );
					const int32 bp	  = static_cast<int32>( b.Get() );
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
					notifyDocumentEdited( "Link Animation Graph Nodes" );
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
					notifyDocumentEdited( "Delete Animation Graph Link" );
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
					notifyDocumentEdited( "Delete Animation Graph Node" );
				}
			}
			ed::EndDelete();
		}

		_nodeGraph.applyContentFitIfNeeded();
		cacheNodeLayout();
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
		AnimationGraphAsset data;
		if ( EditorToolAssetCommands::loadAnimationGraph( data, getLoadedAssetPath() ) )
		{
			_listNode = std::move( data._listNode );
			_listLink = std::move( data._listLink );
		}
		if ( _listNode.empty() )
			ensureDefaults();
		_bGraphLayoutReady = SW_FALSE;
		_previewPlayer.stop();
		_bPreviewPlaying = SW_FALSE;
		markDocumentLoaded();
		_nodeGraph.requestContentFit();
	}

	void AnimationGraphPanel::saveGraphData()
	{
		AnimationGraphAsset data = captureGraphData();
		if ( _nodeGraph.bind() )
		{
			for ( GraphNode& node : data._listNode )
			{
				const ImVec2 pos = ed::GetNodePosition( AnimationGraphPanelInternal::toNodeId( node._id ) );
				node._x			 = pos.x;
				node._y			 = pos.y;
			}
			_nodeGraph.unbind();
			_listNode = data._listNode;
		}
		EditorToolAssetCommands::saveAnimationGraph( data, getLoadedAssetPath() );
		clearDocumentDirty();
		syncDocumentUndoBaseline();
	}

	bool AnimationGraphPanel::saveDocument()
	{
		saveGraphData();
		return true;
	}

	string AnimationGraphPanel::captureDocumentText() const
	{
		return captureGraphData().toJson();
	}

	void AnimationGraphPanel::applyDocumentText( string_view text )
	{
		AnimationGraphAsset restored;
		if ( text.empty() == false )
			restored.parseJson( text );
		_listNode = std::move( restored._listNode );
		_listLink = std::move( restored._listLink );
		if ( _listNode.empty() )
			ensureDefaults();
		_bGraphLayoutReady = SW_FALSE;
		_nodeGraph.requestContentFit();
	}

	AnimationGraphAsset AnimationGraphPanel::captureGraphData() const
	{
		AnimationGraphAsset data;
		data._listNode = _listNode;
		data._listLink = _listLink;
		return data;
	}

	void AnimationGraphPanel::syncPreviewGraph()
	{
		AnimationGraphAsset asset = captureGraphData();
		_previewPlayer.setGraph( asset );
		_previewPlayer.clearClips();
		_listPreviewClip.clear();
		_listPreviewClip.reserve( _listNode.size() );
		for ( const GraphNode& node : _listNode )
			_listPreviewClip.push_back( AnimClip( node._name, 0.75f ) );
		for ( AnimClip& clip : _listPreviewClip )
			_previewPlayer.registerClip( clip.getName(), &clip );
	}

	void AnimationGraphPanel::cacheNodeLayout()
	{
		bool bMoved{ false };
		for ( GraphNode& node : _listNode )
		{
			const ImVec2 pos	  = ed::GetNodePosition( AnimationGraphPanelInternal::toNodeId( node._id ) );
			const bool	 bChanged = pos.x != node._x || pos.y != node._y;
			if ( EditorSessionPolicy::shouldMarkDocumentDirtyOnNodeMove( _bGraphLayoutReady == SW_TRUE, bChanged ) )
				bMoved = true;
			node._x = pos.x;
			node._y = pos.y;
		}
		if ( bMoved )
			notifyDocumentEdited( "Move Animation Graph Nodes", "anim-graph-layout" );
		_bGraphLayoutReady = SW_TRUE;
	}

	void AnimationGraphPanel::tickPreview( float32 deltaSeconds )
	{
		if ( _bPreviewPlaying == SW_FALSE )
			return;
		_previewHoldSeconds += deltaSeconds;
		if ( _previewHoldSeconds < 0.75f )
			return;
		_previewHoldSeconds = 0.0f;
		if ( _previewPlayer.advance() == false )
			_bPreviewPlaying = SW_FALSE;
		else
			EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentNodeName(), getLoadedAssetPath() );
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
