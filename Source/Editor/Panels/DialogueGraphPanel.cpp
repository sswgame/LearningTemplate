#include "pch.h"

#include "Editor/Panels/DialogueGraphPanel.h"

#include "Core/Common/Defines.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/EditorViewportPreview.h"
#include "Editor/Common/EditorSessionPolicy.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
	namespace
	{
		struct DialogueGraphPanelInternal
		{
			static constexpr int32 kPinInputOffset	= 1;
			static constexpr int32 kPinOutputOffset = 2;
			static constexpr int32 kPinTrueOffset	= 3;
			static constexpr int32 kPinFalseOffset	= 4;
			static constexpr int32 kPinChoiceBase	= 10;

			static int32 pinIn( int32 nodeId )
			{
				return nodeId * 100 + kPinInputOffset;
			}

			static int32 pinOut( int32 nodeId )
			{
				return nodeId * 100 + kPinOutputOffset;
			}

			static int32 pinBranchTrue( int32 nodeId )
			{
				return nodeId * 100 + kPinTrueOffset;
			}

			static int32 pinBranchFalse( int32 nodeId )
			{
				return nodeId * 100 + kPinFalseOffset;
			}

			static int32 pinChoice( int32 nodeId, int32 choiceIndex )
			{
				return nodeId * 100 + kPinChoiceBase + choiceIndex;
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
	SW_LOG_CALLER( "DialogueGraphPanel" );

	DialogueGraphPanel::DialogueGraphPanel()
		: EditorDocumentPanel{ EditorAssetKind::DialogueGraph, true }
		, _nodeGraph{}
		, _listNode{}
		, _listLink{}
		, _selectedNodeId{ 0 }
		, _previewNodeId{ 0 }
		, _previewHoldSeconds{ 0.0f }
		, _bGraphLayoutReady{ SW_FALSE }
		, _bPreviewPlaying{ SW_FALSE }
		, _reservedGraph{ 0 }
	{
	}

	void DialogueGraphPanel::shutdown( IRHIDevice* /*pRhiDevice*/ )
	{
		_nodeGraph.shutdown();
	}

	void DialogueGraphPanel::drawContent()
	{
		updateFocusedDocument();
		if ( isDocumentLoaded() == false )
			loadGraphData();

		tickPreview( ImGui::GetIO().DeltaTime );

		BLOCK( "Toolbar" )
		{
			if ( EditorChrome::beginToolbar( "##DialogueToolbar" ) )
			{
				if ( ImGui::Button( "+ Dialogue" ) )
				{
					addNode( DialogueAssetNodeType::Dialogue, "NPC", "Enter dialogue text..." );
					notifyDocumentEdited( "Add Dialogue Node" );
				}
				ImGui::SameLine();
				if ( ImGui::Button( "+ Choice" ) )
				{
					addNode( DialogueAssetNodeType::Choice, "", "Player options" );
					notifyDocumentEdited( "Add Dialogue Node" );
				}
				ImGui::SameLine();
				if ( ImGui::Button( "+ Branch" ) )
				{
					addNode( DialogueAssetNodeType::Branch, "", "flag.visited == 1" );
					notifyDocumentEdited( "Add Dialogue Node" );
				}
				ImGui::SameLine();
				if ( ImGui::Button( "+ Action" ) )
				{
					addNode( DialogueAssetNodeType::Action, "", "give_item:potion:1" );
					notifyDocumentEdited( "Add Dialogue Node" );
				}
				ImGui::SameLine();
				if ( ImGui::Button( "+ End" ) )
				{
					addNode( DialogueAssetNodeType::End );
					notifyDocumentEdited( "Add Dialogue Node" );
				}
				ImGui::SameLine();
				EditorWidgets::drawToolbarSeparator();
				if ( ImGui::Button( "Save" ) )
					saveGraphData();
				ImGui::SameLine();
				if ( ImGui::Button( "Reload" ) )
					loadGraphData();
				ImGui::SameLine();
				if ( ImGui::Button( "Reset Default" ) )
				{
					_listNode.clear();
					_listLink.clear();
					ensureDefaults();
					_nodeGraph.requestContentFit();
					notifyDocumentEdited( "Reset Dialogue Graph" );
				}
				ImGui::SameLine();
				if ( ImGui::Button( "Zoom Fit" ) )
					_nodeGraph.requestContentFit();
				drawPreviewToolbar();

				ImGui::SameLine();
				ImGui::TextDisabled( "(Nodes: %zu, Links: %zu)", _listNode.size(), _listLink.size() );
			}
			EditorChrome::endToolbar();
		}

		const float32 availWidth  = ImGui::GetContentRegionAvail().x;
		const float32 canvasWidth = _selectedNodeId > 0 ? availWidth * 0.72f : availWidth;

		editor::EditorSectionDesc canvasDesc{};
		canvasDesc._pId		  = "DialogueCanvasRegion";
		canvasDesc._kind	  = editor::EditorSectionKind::Child;
		canvasDesc._childSize = float2{ canvasWidth, 0.0f };
		canvasDesc._flags	  = editor::EditorSectionFlags::NoScrollbar | editor::EditorSectionFlags::NoScrollWithMouse;
		EditorChrome::beginSection( canvasDesc );

		if ( _nodeGraph.beginCanvas( "DialogueGraphCanvas", "DialogueGraphEditor.json" ) == false )
		{
			ImGui::TextUnformatted( "Failed to create Dialogue Node Editor context." );
			EditorChrome::endSection();
			return;
		}

		// 노드 렌더링
		for ( DialogueNode& node : _listNode )
		{
			const ed::NodeId nodeId = DialogueGraphPanelInternal::toNodeId( node._id );
			ed::BeginNode( nodeId );

			switch ( node._type )
			{
				case DialogueAssetNodeType::Start:
				{
					ImGui::TextColored( ImVec4( 0.2f, 0.9f, 0.3f, 1.0f ), "[START]" );
					ed::BeginPin( DialogueGraphPanelInternal::toPinId( DialogueGraphPanelInternal::pinOut( node._id ) ), ed::PinKind::Output );
					ImGui::TextUnformatted( "Next ->" );
					ed::EndPin();
					break;
				}
				case DialogueAssetNodeType::Dialogue:
				{
					ImGui::TextColored( ImVec4( 0.4f, 0.7f, 1.0f, 1.0f ), "[DIALOGUE: %s]", node._speaker.empty() ? "(No Speaker)" : node._speaker.c_str() );
					ed::BeginPin( DialogueGraphPanelInternal::toPinId( DialogueGraphPanelInternal::pinIn( node._id ) ), ed::PinKind::Input );
					ImGui::TextUnformatted( "-> In" );
					ed::EndPin();
					ImGui::SameLine();
					ed::BeginPin( DialogueGraphPanelInternal::toPinId( DialogueGraphPanelInternal::pinOut( node._id ) ), ed::PinKind::Output );
					ImGui::TextUnformatted( "Next ->" );
					ed::EndPin();

					if ( node._text.empty() == false )
					{
						const string preview = node._text.size() > 40 ? node._text.substr( 0, 37 ) + "..." : node._text;
						ImGui::TextDisabled( "\"%s\"", preview.c_str() );
					}
					break;
				}
				case DialogueAssetNodeType::Choice:
				{
					ImGui::TextColored( ImVec4( 0.8f, 0.5f, 1.0f, 1.0f ), "[CHOICE]" );
					ed::BeginPin( DialogueGraphPanelInternal::toPinId( DialogueGraphPanelInternal::pinIn( node._id ) ), ed::PinKind::Input );
					ImGui::TextUnformatted( "-> In" );
					ed::EndPin();

					if ( node._listChoice.empty() )
					{
						ed::BeginPin( DialogueGraphPanelInternal::toPinId( DialogueGraphPanelInternal::pinOut( node._id ) ), ed::PinKind::Output );
						ImGui::TextUnformatted( "Choice 0 ->" );
						ed::EndPin();
					}
					else
					{
						for ( size_t choiceIndex = 0; choiceIndex < node._listChoice.size(); ++choiceIndex )
						{
							ed::BeginPin( DialogueGraphPanelInternal::toPinId( DialogueGraphPanelInternal::pinChoice( node._id, static_cast<int32>( choiceIndex ) ) ), ed::PinKind::Output );
							ImGui::Text( "#%zu: %s ->", choiceIndex + 1, node._listChoice[choiceIndex].c_str() );
							ed::EndPin();
						}
					}
					break;
				}
				case DialogueAssetNodeType::Branch:
				{
					ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ), "[BRANCH]" );
					ed::BeginPin( DialogueGraphPanelInternal::toPinId( DialogueGraphPanelInternal::pinIn( node._id ) ), ed::PinKind::Input );
					ImGui::TextUnformatted( "-> In" );
					ed::EndPin();
					ImGui::TextDisabled( "if (%s)", node._condition.c_str() );

					ed::BeginPin( DialogueGraphPanelInternal::toPinId( DialogueGraphPanelInternal::pinBranchTrue( node._id ) ), ed::PinKind::Output );
					ImGui::TextColored( ImVec4( 0.3f, 1.0f, 0.4f, 1.0f ), "True ->" );
					ed::EndPin();
					ImGui::SameLine();
					ed::BeginPin( DialogueGraphPanelInternal::toPinId( DialogueGraphPanelInternal::pinBranchFalse( node._id ) ), ed::PinKind::Output );
					ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ), "False ->" );
					ed::EndPin();
					break;
				}
				case DialogueAssetNodeType::Action:
				{
					ImGui::TextColored( ImVec4( 0.2f, 0.9f, 0.9f, 1.0f ), "[ACTION]" );
					ed::BeginPin( DialogueGraphPanelInternal::toPinId( DialogueGraphPanelInternal::pinIn( node._id ) ), ed::PinKind::Input );
					ImGui::TextUnformatted( "-> In" );
					ed::EndPin();
					ImGui::SameLine();
					ed::BeginPin( DialogueGraphPanelInternal::toPinId( DialogueGraphPanelInternal::pinOut( node._id ) ), ed::PinKind::Output );
					ImGui::TextUnformatted( "Next ->" );
					ed::EndPin();
					ImGui::TextDisabled( "cmd: %s", node._actionCommand.c_str() );
					break;
				}
				case DialogueAssetNodeType::End:
				{
					ImGui::TextColored( ImVec4( 0.9f, 0.3f, 0.3f, 1.0f ), "[END]" );
					ed::BeginPin( DialogueGraphPanelInternal::toPinId( DialogueGraphPanelInternal::pinIn( node._id ) ), ed::PinKind::Input );
					ImGui::TextUnformatted( "-> In" );
					ed::EndPin();
					break;
				}
				default:
					break;
			}

			ed::EndNode();

			if ( _nodeGraph.needsContentFit() )
				ed::SetNodePosition( nodeId, ImVec2( node._x, node._y ) );
		}

		// 링크 렌더링
		for ( const DialogueLink& link : _listLink )
		{
			ed::Link( DialogueGraphPanelInternal::toLinkId( link._id ), DialogueGraphPanelInternal::toPinId( link._fromPin ), DialogueGraphPanelInternal::toPinId( link._toPin ) );
		}

		// 새 링크 생성 처리
		if ( ed::BeginCreate() )
		{
			ed::PinId a;
			ed::PinId b;
			if ( ed::QueryNewLink( &a, &b ) )
			{
				if ( a.Get() != 0 && b.Get() != 0 && ed::AcceptNewItem() )
				{
					DialogueLink newLink{};
					newLink._id		 = nextLinkId();
					const int32 pinA = static_cast<int32>( a.Get() );
					const int32 pinB = static_cast<int32>( b.Get() );

					// 핀 종류(In vs Out) 분별: In 핀은 끝자리가 1
					const bool bIsAInput = ( pinA % 100 ) == DialogueGraphPanelInternal::kPinInputOffset;
					const bool bIsBInput = ( pinB % 100 ) == DialogueGraphPanelInternal::kPinInputOffset;

					if ( bIsAInput != bIsBInput )
					{
						if ( bIsAInput )
						{
							newLink._fromPin = pinB;
							newLink._toPin	 = pinA;
						}
						else
						{
							newLink._fromPin = pinA;
							newLink._toPin	 = pinB;
						}
						_listLink.push_back( newLink );
						notifyDocumentEdited( "Link Dialogue Nodes" );
					}
				}
			}
			ed::EndCreate();
		}

		// 삭제 처리
		if ( ed::BeginDelete() )
		{
			ed::LinkId linkId;
			while ( ed::QueryDeletedLink( &linkId ) )
			{
				if ( ed::AcceptDeletedItem() )
				{
					const int32 id = static_cast<int32>( linkId.Get() );
					_listLink.erase( std::remove_if( _listLink.begin(), _listLink.end(),
													 [id]( const DialogueLink& l )
					{ return l._id == id; } ),
									 _listLink.end() );
					notifyDocumentEdited( "Delete Dialogue Link" );
				}
			}
			ed::NodeId nodeId;
			while ( ed::QueryDeletedNode( &nodeId ) )
			{
				if ( ed::AcceptDeletedItem() )
				{
					const int32 id = static_cast<int32>( nodeId.Get() );
					_listNode.erase( std::remove_if( _listNode.begin(), _listNode.end(),
													 [id]( const DialogueNode& n )
					{ return n._id == id; } ),
									 _listNode.end() );
					_listLink.erase( std::remove_if( _listLink.begin(), _listLink.end(),
													 [id]( const DialogueLink& l )
					{ return ( l._fromPin / 100 ) == id || ( l._toPin / 100 ) == id; } ),
									 _listLink.end() );
					if ( _selectedNodeId == id )
						_selectedNodeId = 0;
					notifyDocumentEdited( "Delete Dialogue Node" );
				}
			}
			ed::EndDelete();
		}

		// 선택 노드 추적
		ed::NodeId	selectedNodes[1];
		const int32 count = ed::GetSelectedNodes( selectedNodes, 1 );
		if ( count > 0 )
			_selectedNodeId = static_cast<int32>( selectedNodes[0].Get() );

		_nodeGraph.applyContentFitIfNeeded();
		cacheNodeLayout();

		_nodeGraph.endCanvas();
		EditorChrome::endSection();

		// 2) 선택된 노드 상세 인스펙터 패널
		if ( _selectedNodeId > 0 )
		{
			ImGui::SameLine();
			editor::EditorSectionDesc inspectorDesc{};
			inspectorDesc._pId	 = "DialogueNodeInspector";
			inspectorDesc._kind	 = editor::EditorSectionKind::Child;
			inspectorDesc._flags = editor::EditorSectionFlags::Border;
			EditorChrome::beginSection( inspectorDesc );

			DialogueNode* pSelectedNode{ nullptr };
			for ( DialogueNode& node : _listNode )
			{
				if ( node._id == _selectedNodeId )
				{
					pSelectedNode = &node;
					break;
				}
			}

			if ( pSelectedNode != nullptr )
			{
				ImGui::TextColored( ImVec4( 0.2f, 0.8f, 1.0f, 1.0f ), "Node #%d (%s)", pSelectedNode->_id, DialogueGraphAsset::nodeTypeName( pSelectedNode->_type ) );
				ImGui::Separator();

				if ( pSelectedNode->_type == DialogueAssetNodeType::Dialogue )
				{
					fixed_string<constant::kMaxBuffer64> speakerBuf{ pSelectedNode->_speaker.c_str() };
					if ( ImGui::InputText( "Speaker", speakerBuf.data(), speakerBuf.capacity() ) )
						pSelectedNode->_speaker = speakerBuf.c_str();
					if ( ImGui::IsItemDeactivatedAfterEdit() )
						notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );

					fixed_string<constant::kMaxBuffer512> textBuf{ pSelectedNode->_text.c_str() };
					if ( ImGui::InputTextMultiline( "Text", textBuf.data(), textBuf.capacity(), ImVec2( -1, 100 ) ) )
						pSelectedNode->_text = textBuf.c_str();
					if ( ImGui::IsItemDeactivatedAfterEdit() )
						notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
				}
				else if ( pSelectedNode->_type == DialogueAssetNodeType::Choice )
				{
					fixed_string<constant::kMaxBuffer128> promptBuf{ pSelectedNode->_text.c_str() };
					if ( ImGui::InputText( "Prompt", promptBuf.data(), promptBuf.capacity() ) )
						pSelectedNode->_text = promptBuf.c_str();
					if ( ImGui::IsItemDeactivatedAfterEdit() )
						notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );

					ImGui::Text( "Choices (%zu):", pSelectedNode->_listChoice.size() );
					for ( size_t choiceIndex = 0; choiceIndex < pSelectedNode->_listChoice.size(); ++choiceIndex )
					{
						ImGui::PushID( static_cast<int32>( choiceIndex ) );
						fixed_string<constant::kMaxBuffer128> choiceBuf{ pSelectedNode->_listChoice[choiceIndex].c_str() };
						if ( ImGui::InputText( "##Choice", choiceBuf.data(), choiceBuf.capacity() ) )
							pSelectedNode->_listChoice[choiceIndex] = choiceBuf.c_str();
						if ( ImGui::IsItemDeactivatedAfterEdit() )
							notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
						ImGui::SameLine();
						if ( ImGui::Button( "X" ) )
						{
							pSelectedNode->_listChoice.erase( pSelectedNode->_listChoice.begin() + choiceIndex );
							notifyDocumentEdited( "Edit Dialogue Node" );
							ImGui::PopID();
							break;
						}
						ImGui::PopID();
					}

					if ( ImGui::Button( "+ Add Choice Option" ) )
					{
						pSelectedNode->_listChoice.push_back( "New choice option" );
						notifyDocumentEdited( "Edit Dialogue Node" );
					}
				}
				else if ( pSelectedNode->_type == DialogueAssetNodeType::Branch )
				{
					fixed_string<constant::kMaxBuffer128> condBuf{ pSelectedNode->_condition.c_str() };
					if ( ImGui::InputText( "Condition", condBuf.data(), condBuf.capacity() ) )
						pSelectedNode->_condition = condBuf.c_str();
					if ( ImGui::IsItemDeactivatedAfterEdit() )
						notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
					ImGui::TextDisabled( "Ex: flag.boss_defeated == 1" );
				}
				else if ( pSelectedNode->_type == DialogueAssetNodeType::Action )
				{
					fixed_string<constant::kMaxBuffer128> cmdBuf{ pSelectedNode->_actionCommand.c_str() };
					if ( ImGui::InputText( "Command", cmdBuf.data(), cmdBuf.capacity() ) )
						pSelectedNode->_actionCommand = cmdBuf.c_str();
					if ( ImGui::IsItemDeactivatedAfterEdit() )
						notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
					ImGui::TextDisabled( "Ex: give_item:potion:3" );
				}
			}

			EditorChrome::endSection();
		}
	}

	void DialogueGraphPanel::ensureDefaults()
	{
		_listNode.clear();
		_listLink.clear();

		DialogueNode startNode{};
		startNode._id	= 1;
		startNode._type = DialogueAssetNodeType::Start;
		startNode._x	= 50.0f;
		startNode._y	= 100.0f;
		_listNode.push_back( startNode );

		DialogueNode diagNode{};
		diagNode._id	  = 2;
		diagNode._type	  = DialogueAssetNodeType::Dialogue;
		diagNode._speaker = "Elder";
		diagNode._text	  = "Greetings adventurer! The ancient ruins ahead are full of peril.";
		diagNode._x		  = 250.0f;
		diagNode._y		  = 100.0f;
		_listNode.push_back( diagNode );

		DialogueNode choiceNode{};
		choiceNode._id		   = 3;
		choiceNode._type	   = DialogueAssetNodeType::Choice;
		choiceNode._text	   = "How do you respond?";
		choiceNode._listChoice = { "I am ready for any challenge!", "Could you give me some supplies first?" };
		choiceNode._x		   = 650.0f;
		choiceNode._y		   = 100.0f;
		_listNode.push_back( choiceNode );

		DialogueNode actionNode{};
		actionNode._id			  = 4;
		actionNode._type		  = DialogueAssetNodeType::Action;
		actionNode._actionCommand = "give_item:healing_potion:3";
		actionNode._x			  = 1050.0f;
		actionNode._y			  = 220.0f;
		_listNode.push_back( actionNode );

		DialogueNode endNode{};
		endNode._id	  = 5;
		endNode._type = DialogueAssetNodeType::End;
		endNode._x	  = 1350.0f;
		endNode._y	  = 120.0f;
		_listNode.push_back( endNode );

		// 기본 링크 연결
		_listLink.push_back( DialogueLink{ 1, DialogueGraphPanelInternal::pinOut( 1 ), DialogueGraphPanelInternal::pinIn( 2 ) } );
		_listLink.push_back( DialogueLink{ 2, DialogueGraphPanelInternal::pinOut( 2 ), DialogueGraphPanelInternal::pinIn( 3 ) } );
		_listLink.push_back( DialogueLink{ 3, DialogueGraphPanelInternal::pinChoice( 3, 0 ), DialogueGraphPanelInternal::pinIn( 5 ) } );
		_listLink.push_back( DialogueLink{ 4, DialogueGraphPanelInternal::pinChoice( 3, 1 ), DialogueGraphPanelInternal::pinIn( 4 ) } );
		_listLink.push_back( DialogueLink{ 5, DialogueGraphPanelInternal::pinOut( 4 ), DialogueGraphPanelInternal::pinIn( 5 ) } );
	}

	void DialogueGraphPanel::loadGraphData()
	{
		DialogueGraphAsset data;
		if ( EditorToolAssetCommands::loadDialogueGraph( data, getLoadedAssetPath() ) )
		{
			_listNode = std::move( data._listNode );
			_listLink = std::move( data._listLink );
		}
		if ( _listNode.empty() )
			ensureDefaults();

		_bGraphLayoutReady = SW_FALSE;
		_previewNodeId	   = 0;
		_bPreviewPlaying   = SW_FALSE;
		markDocumentLoaded();
		_nodeGraph.requestContentFit();
	}

	void DialogueGraphPanel::saveGraphData()
	{
		DialogueGraphAsset data = captureGraphData();
		EditorToolAssetCommands::saveDialogueGraph( data, getLoadedAssetPath() );
		clearDocumentDirty();
		syncDocumentUndoBaseline();
	}

	bool DialogueGraphPanel::saveDocument()
	{
		saveGraphData();
		return true;
	}

	string DialogueGraphPanel::captureDocumentText() const
	{
		return captureGraphData().toJson();
	}

	void DialogueGraphPanel::applyDocumentText( string_view text )
	{
		DialogueGraphAsset restored;
		if ( text.empty() == false )
			restored.parseJson( text );
		_listNode = std::move( restored._listNode );
		_listLink = std::move( restored._listLink );
		if ( _listNode.empty() )
			ensureDefaults();
		_bGraphLayoutReady = SW_FALSE;
		_nodeGraph.requestContentFit();
	}

	DialogueGraphAsset DialogueGraphPanel::captureGraphData() const
	{
		DialogueGraphAsset data;
		data._listNode = _listNode;
		data._listLink = _listLink;
		return data;
	}

	void DialogueGraphPanel::cacheNodeLayout()
	{
		bool bMoved{ false };
		for ( DialogueNode& node : _listNode )
		{
			const ImVec2 pos	  = ed::GetNodePosition( DialogueGraphPanelInternal::toNodeId( node._id ) );
			const bool	 bChanged = ( MathUtil::nearEqual( pos.x, node._x ) == false ) && ( MathUtil::nearEqual( pos.y, node._y ) == false );
			if ( EditorSessionPolicy::shouldMarkDocumentDirtyOnNodeMove( _bGraphLayoutReady == SW_TRUE, bChanged ) )
				bMoved = true;
			node._x = pos.x;
			node._y = pos.y;
		}
		if ( bMoved )
			notifyDocumentEdited( "Move Dialogue Nodes", "dialogue-graph-layout" );
		_bGraphLayoutReady = SW_TRUE;
	}

	void DialogueGraphPanel::drawPreviewToolbar()
	{
		ImGui::SameLine();
		EditorWidgets::drawToolbarSeparator();
		ImGui::SameLine();
		if ( ImGui::Button( "Play Preview" ) )
		{
			DialogueGraphAsset		 asset	= captureGraphData();
			const DialogueAssetNode* pStart = asset.findStartNode();
			_previewNodeId					= ( pStart != nullptr ) ? pStart->_id : 0;
			_bPreviewPlaying				= SW_TRUE;
			_previewHoldSeconds				= 0.0f;
			if ( pStart != nullptr )
				EditorViewportPreview::applyDialogueLine( pStart->_speaker, pStart->_text );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Advance Preview" ) )
			previewAdvance();
		ImGui::SameLine();
		if ( ImGui::Button( "Stop Preview" ) )
		{
			_bPreviewPlaying = SW_FALSE;
			_previewNodeId	 = 0;
		}
		if ( _previewNodeId > 0 )
		{
			ImGui::SameLine();
			ImGui::TextDisabled( "Preview node #%d", _previewNodeId );
			DialogueGraphAsset		 asset = captureGraphData();
			const DialogueAssetNode* pNode = asset.findNode( _previewNodeId );
			if ( pNode != nullptr && pNode->_type == DialogueAssetNodeType::Choice )
			{
				for ( int32 choiceIndex = 0; choiceIndex < static_cast<int32>( pNode->_listChoice.size() ); ++choiceIndex )
				{
					ImGui::SameLine();
					fixed_string<constant::kMaxBuffer64> label;
					formatstring( label.data(), label.capacity(), "Choice %#", choiceIndex );
					if ( ImGui::SmallButton( label.c_str() ) )
						previewAdvance( DialogueGraphPanelInternal::kPinChoiceBase + choiceIndex );
				}
			}
			if ( pNode != nullptr && pNode->_type == DialogueAssetNodeType::Branch )
			{
				ImGui::SameLine();
				if ( ImGui::SmallButton( "True" ) )
					previewAdvance( DialogueGraphPanelInternal::kPinTrueOffset );
				ImGui::SameLine();
				if ( ImGui::SmallButton( "False" ) )
					previewAdvance( DialogueGraphPanelInternal::kPinFalseOffset );
			}
		}
	}

	void DialogueGraphPanel::tickPreview( float32 deltaSeconds )
	{
		if ( _bPreviewPlaying == SW_FALSE || _previewNodeId <= 0 )
			return;
		DialogueGraphAsset		 asset = captureGraphData();
		const DialogueAssetNode* pNode = asset.findNode( _previewNodeId );
		if ( pNode == nullptr )
		{
			_bPreviewPlaying = SW_FALSE;
			return;
		}
		if ( pNode->_type == DialogueAssetNodeType::Choice || pNode->_type == DialogueAssetNodeType::Branch )
			return;
		_previewHoldSeconds += deltaSeconds;
		if ( _previewHoldSeconds < 0.9f )
			return;
		_previewHoldSeconds = 0.0f;
		previewAdvance();
	}

	void DialogueGraphPanel::previewAdvance( int32 pinOffset )
	{
		DialogueGraphAsset asset = captureGraphData();
		if ( _previewNodeId <= 0 )
		{
			const DialogueAssetNode* pStart = asset.findStartNode();
			_previewNodeId					= ( pStart != nullptr ) ? pStart->_id : 0;
			return;
		}
		int32 nextId{ 0 };
		if ( pinOffset == DialogueGraphPanelInternal::kPinOutputOffset )
			nextId = asset.findDefaultNextNodeId( _previewNodeId );
		else
			nextId = asset.findLinkedNodeId( _previewNodeId, pinOffset );
		if ( nextId <= 0 )
		{
			_bPreviewPlaying = SW_FALSE;
			return;
		}
		_previewNodeId				   = nextId;
		const DialogueAssetNode* pNext = asset.findNode( nextId );
		if ( pNext != nullptr )
			EditorViewportPreview::applyDialogueLine( pNext->_speaker, pNext->_text );
		if ( pNext != nullptr && pNext->_type == DialogueAssetNodeType::End )
			_bPreviewPlaying = SW_FALSE;
	}

	int32 DialogueGraphPanel::nextNodeId() const
	{
		return nextItemId( _listNode );
	}

	int32 DialogueGraphPanel::nextLinkId() const
	{
		return nextItemId( _listLink );
	}

	void DialogueGraphPanel::addNode( DialogueAssetNodeType type, const utf8* pSpeaker, const utf8* pText )
	{
		DialogueNode node{};
		node._id	  = nextNodeId();
		node._type	  = type;
		node._speaker = pSpeaker;
		node._text	  = pText;
		node._x		  = 200.0f + static_cast<float32>( ( node._id % 5 ) * 80 );
		node._y		  = 150.0f + static_cast<float32>( ( node._id % 5 ) * 60 );

		if ( type == DialogueAssetNodeType::Choice )
			node._listChoice = { "Option 1", "Option 2" };

		_listNode.push_back( node );
		_selectedNodeId = node._id;
	}
} // namespace sw::editor
