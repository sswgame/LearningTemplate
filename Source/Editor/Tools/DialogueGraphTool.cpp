#include "pch.h"

#include "Editor/Tools/DialogueGraphTool.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Config/EditorConfig.h"
#include "Editor/EditorUtil.h"

#include "RuntimeAPI/EditorUIContext.h"

#include <imgui-node-editor/imgui_node_editor.h>
#include <imgui.h>
namespace ed = ax::NodeEditor;

namespace sw
{
	namespace
	{
		constexpr int32 kPinInputOffset	 = 1;
		constexpr int32 kPinOutputOffset = 2;
		constexpr int32 kPinTrueOffset	 = 3;
		constexpr int32 kPinFalseOffset	 = 4;
		constexpr int32 kPinChoiceBase	 = 10;

		int32 pinIn( int32 nodeId )
		{
			return nodeId * 100 + kPinInputOffset;
		}

		int32 pinOut( int32 nodeId )
		{
			return nodeId * 100 + kPinOutputOffset;
		}

		int32 pinBranchTrue( int32 nodeId )
		{
			return nodeId * 100 + kPinTrueOffset;
		}

		int32 pinBranchFalse( int32 nodeId )
		{
			return nodeId * 100 + kPinFalseOffset;
		}

		int32 pinChoice( int32 nodeId, int32 choiceIndex )
		{
			return nodeId * 100 + kPinChoiceBase + choiceIndex;
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

		const utf8* getNodeTypeString( DialogueNodeType type )
		{
			switch ( type )
			{
				case DialogueNodeType::Start:
					return "Start";
				case DialogueNodeType::Dialogue:
					return "Dialogue";
				case DialogueNodeType::Choice:
					return "Choice";
				case DialogueNodeType::Branch:
					return "Branch";
				case DialogueNodeType::Action:
					return "Action";
				case DialogueNodeType::End:
					return "End";
				default:
					return "Unknown";
			}
		}

		DialogueNodeType parseNodeTypeString( string_view typeStr )
		{
			if ( typeStr == "Start" )
				return DialogueNodeType::Start;
			if ( typeStr == "Choice" )
				return DialogueNodeType::Choice;
			if ( typeStr == "Branch" )
				return DialogueNodeType::Branch;
			if ( typeStr == "Action" )
				return DialogueNodeType::Action;
			if ( typeStr == "End" )
				return DialogueNodeType::End;
			return DialogueNodeType::Dialogue;
		}

	} // namespace

	DialogueGraphTool::DialogueGraphTool()
		: BaseNodeGraphEditor{ false }
		, _bLoaded{ false }
		, _selectedNodeId{ 0 }
		, _listNodes{}
		, _listLinks{}
	{
	}

	void DialogueGraphTool::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		if ( _bLoaded == false )
			loadGraphData();

		BLOCK( "Toolbar" )
		{
			if ( ImGui::Button( "+ Dialogue" ) )
				addNode( DialogueNodeType::Dialogue, "NPC", "Enter dialogue text..." );
			ImGui::SameLine();
			if ( ImGui::Button( "+ Choice" ) )
				addNode( DialogueNodeType::Choice, "", "Player options" );
			ImGui::SameLine();
			if ( ImGui::Button( "+ Branch" ) )
				addNode( DialogueNodeType::Branch, "", "flag.visited == 1" );
			ImGui::SameLine();
			if ( ImGui::Button( "+ Action" ) )
				addNode( DialogueNodeType::Action, "", "give_item:potion:1" );
			ImGui::SameLine();
			if ( ImGui::Button( "+ End" ) )
				addNode( DialogueNodeType::End );
			ImGui::SameLine();
			ImGui::TextDisabled( "|" );
			ImGui::SameLine();
			if ( ImGui::Button( "Save" ) )
				saveGraphData();
			ImGui::SameLine();
			if ( ImGui::Button( "Reload" ) )
			{
				_bLoaded = false;
				loadGraphData();
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Reset Default" ) )
			{
				_listNodes.clear();
				_listLinks.clear();
				ensureDefaults();
				_bNavigatedToContent = false;
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Zoom Fit" ) )
				_bNavigatedToContent = false;

			ImGui::SameLine();
			ImGui::TextDisabled( "(Nodes: %zu, Links: %zu)", _listNodes.size(), _listLinks.size() );
		}

		ensureEditorContext( "DialogueGraphEditor.json" );
		if ( _pEditor == nullptr )
		{
			ImGui::TextUnformatted( "Failed to create Dialogue Node Editor context." );
			ImGui::End();
			return;
		}

		const float32 availWidth  = ImGui::GetContentRegionAvail().x;
		const float32 canvasWidth = _selectedNodeId > 0 ? availWidth * 0.72f : availWidth;

		// 1) 캔버스 영역
		ImGui::BeginChild( "DialogueCanvasRegion", ImVec2( canvasWidth, 0 ), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );

		ed::SetCurrentEditor( _pEditor );
		ed::Begin( "DialogueGraphCanvas" );

		// 노드 렌더링
		for ( DialogueNode& node : _listNodes )
		{
			const ed::NodeId nodeId = toNodeId( node._id );
			ed::BeginNode( nodeId );

			switch ( node._type )
			{
				case DialogueNodeType::Start:
				{
					ImGui::TextColored( ImVec4( 0.2f, 0.9f, 0.3f, 1.0f ), "[START]" );
					ed::BeginPin( toPinId( pinOut( node._id ) ), ed::PinKind::Output );
					ImGui::TextUnformatted( "Next ->" );
					ed::EndPin();
					break;
				}
				case DialogueNodeType::Dialogue:
				{
					ImGui::TextColored( ImVec4( 0.4f, 0.7f, 1.0f, 1.0f ), "[DIALOGUE: %s]", node._speaker.empty() ? "(No Speaker)" : node._speaker.c_str() );
					ed::BeginPin( toPinId( pinIn( node._id ) ), ed::PinKind::Input );
					ImGui::TextUnformatted( "-> In" );
					ed::EndPin();
					ImGui::SameLine();
					ed::BeginPin( toPinId( pinOut( node._id ) ), ed::PinKind::Output );
					ImGui::TextUnformatted( "Next ->" );
					ed::EndPin();

					if ( node._text.empty() == false )
					{
						const string preview = node._text.size() > 40 ? node._text.substr( 0, 37 ) + "..." : node._text;
						ImGui::TextDisabled( "\"%s\"", preview.c_str() );
					}
					break;
				}
				case DialogueNodeType::Choice:
				{
					ImGui::TextColored( ImVec4( 0.8f, 0.5f, 1.0f, 1.0f ), "[CHOICE]" );
					ed::BeginPin( toPinId( pinIn( node._id ) ), ed::PinKind::Input );
					ImGui::TextUnformatted( "-> In" );
					ed::EndPin();

					if ( node._listChoices.empty() )
					{
						ed::BeginPin( toPinId( pinOut( node._id ) ), ed::PinKind::Output );
						ImGui::TextUnformatted( "Choice 0 ->" );
						ed::EndPin();
					}
					else
					{
						for ( size_t choiceIndex = 0; choiceIndex < node._listChoices.size(); ++choiceIndex )
						{
							ed::BeginPin( toPinId( pinChoice( node._id, static_cast<int32>( choiceIndex ) ) ), ed::PinKind::Output );
							ImGui::Text( "#%zu: %s ->", choiceIndex + 1, node._listChoices[choiceIndex].c_str() );
							ed::EndPin();
						}
					}
					break;
				}
				case DialogueNodeType::Branch:
				{
					ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ), "[BRANCH]" );
					ed::BeginPin( toPinId( pinIn( node._id ) ), ed::PinKind::Input );
					ImGui::TextUnformatted( "-> In" );
					ed::EndPin();
					ImGui::TextDisabled( "if (%s)", node._condition.c_str() );

					ed::BeginPin( toPinId( pinBranchTrue( node._id ) ), ed::PinKind::Output );
					ImGui::TextColored( ImVec4( 0.3f, 1.0f, 0.4f, 1.0f ), "True ->" );
					ed::EndPin();
					ImGui::SameLine();
					ed::BeginPin( toPinId( pinBranchFalse( node._id ) ), ed::PinKind::Output );
					ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ), "False ->" );
					ed::EndPin();
					break;
				}
				case DialogueNodeType::Action:
				{
					ImGui::TextColored( ImVec4( 0.2f, 0.9f, 0.9f, 1.0f ), "[ACTION]" );
					ed::BeginPin( toPinId( pinIn( node._id ) ), ed::PinKind::Input );
					ImGui::TextUnformatted( "-> In" );
					ed::EndPin();
					ImGui::SameLine();
					ed::BeginPin( toPinId( pinOut( node._id ) ), ed::PinKind::Output );
					ImGui::TextUnformatted( "Next ->" );
					ed::EndPin();
					ImGui::TextDisabled( "cmd: %s", node._actionCommand.c_str() );
					break;
				}
				case DialogueNodeType::End:
				{
					ImGui::TextColored( ImVec4( 0.9f, 0.3f, 0.3f, 1.0f ), "[END]" );
					ed::BeginPin( toPinId( pinIn( node._id ) ), ed::PinKind::Input );
					ImGui::TextUnformatted( "-> In" );
					ed::EndPin();
					break;
				}
			}

			ed::EndNode();

			if ( _bNavigatedToContent == false )
				ed::SetNodePosition( nodeId, ImVec2( node._x, node._y ) );
		}

		// 링크 렌더링
		for ( const DialogueLink& link : _listLinks )
		{
			ed::Link( toLinkId( link._id ), toPinId( link._fromPin ), toPinId( link._toPin ) );
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
					const bool bIsAInput = ( pinA % 100 ) == kPinInputOffset;
					const bool bIsBInput = ( pinB % 100 ) == kPinInputOffset;

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
						_listLinks.push_back( newLink );
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
					_listLinks.erase( std::remove_if( _listLinks.begin(), _listLinks.end(),
													  [id]( const DialogueLink& l )
					{ return l._id == id; } ),
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
													  [id]( const DialogueNode& n )
					{ return n._id == id; } ),
									  _listNodes.end() );
					_listLinks.erase( std::remove_if( _listLinks.begin(), _listLinks.end(),
													  [id]( const DialogueLink& l )
					{ return ( l._fromPin / 100 ) == id || ( l._toPin / 100 ) == id; } ),
									  _listLinks.end() );
					if ( _selectedNodeId == id )
						_selectedNodeId = 0;
				}
			}
			ed::EndDelete();
		}

		// 선택 노드 추적
		ed::NodeId	selectedNodes[1];
		const int32 count = ed::GetSelectedNodes( selectedNodes, 1 );
		if ( count > 0 )
			_selectedNodeId = static_cast<int32>( selectedNodes[0].Get() );

		if ( _bNavigatedToContent == false )
		{
			ed::NavigateToContent( 0.1f );
			_bNavigatedToContent = true;
		}

		// 위치 캐시
		for ( DialogueNode& node : _listNodes )
		{
			const ImVec2 pos = ed::GetNodePosition( toNodeId( node._id ) );
			node._x			 = pos.x;
			node._y			 = pos.y;
		}

		ed::End();
		ed::SetCurrentEditor( nullptr );
		ImGui::EndChild();

		// 2) 선택된 노드 상세 인스펙터 패널
		if ( _selectedNodeId > 0 )
		{
			ImGui::SameLine();
			ImGui::BeginChild( "DialogueNodeInspector", ImVec2( 0, 0 ), true );

			DialogueNode* pSelectedNode{ nullptr };
			for ( DialogueNode& node : _listNodes )
			{
				if ( node._id == _selectedNodeId )
				{
					pSelectedNode = &node;
					break;
				}
			}

			if ( pSelectedNode != nullptr )
			{
				ImGui::TextColored( ImVec4( 0.2f, 0.8f, 1.0f, 1.0f ), "Node #%d (%s)", pSelectedNode->_id, getNodeTypeString( pSelectedNode->_type ) );
				ImGui::Separator();

				if ( pSelectedNode->_type == DialogueNodeType::Dialogue )
				{
					utf8 speakerBuf[64]{};
					StringUtil::strncpy( speakerBuf, pSelectedNode->_speaker.c_str(), sizeof( speakerBuf ) - 1 );
					if ( ImGui::InputText( "Speaker", speakerBuf, sizeof( speakerBuf ) ) )
						pSelectedNode->_speaker = speakerBuf;

					utf8 textBuf[512]{};
					StringUtil::strncpy( textBuf, pSelectedNode->_text.c_str(), sizeof( textBuf ) - 1 );
					if ( ImGui::InputTextMultiline( "Text", textBuf, sizeof( textBuf ), ImVec2( -1, 100 ) ) )
						pSelectedNode->_text = textBuf;
				}
				else if ( pSelectedNode->_type == DialogueNodeType::Choice )
				{
					utf8 promptBuf[128]{};
					StringUtil::strncpy( promptBuf, pSelectedNode->_text.c_str(), sizeof( promptBuf ) - 1 );
					if ( ImGui::InputText( "Prompt", promptBuf, sizeof( promptBuf ) ) )
						pSelectedNode->_text = promptBuf;

					ImGui::Text( "Choices (%zu):", pSelectedNode->_listChoices.size() );
					for ( size_t choiceIndex = 0; choiceIndex < pSelectedNode->_listChoices.size(); ++choiceIndex )
					{
						ImGui::PushID( static_cast<int32>( choiceIndex ) );
						utf8 choiceBuf[128]{};
						StringUtil::strncpy( choiceBuf, pSelectedNode->_listChoices[choiceIndex].c_str(), sizeof( choiceBuf ) - 1 );
						if ( ImGui::InputText( "##Choice", choiceBuf, sizeof( choiceBuf ) ) )
							pSelectedNode->_listChoices[choiceIndex] = choiceBuf;
						ImGui::SameLine();
						if ( ImGui::Button( "X" ) )
						{
							pSelectedNode->_listChoices.erase( pSelectedNode->_listChoices.begin() + choiceIndex );
							ImGui::PopID();
							break;
						}
						ImGui::PopID();
					}

					if ( ImGui::Button( "+ Add Choice Option" ) )
						pSelectedNode->_listChoices.push_back( "New choice option" );
				}
				else if ( pSelectedNode->_type == DialogueNodeType::Branch )
				{
					utf8 condBuf[128]{};
					StringUtil::strncpy( condBuf, pSelectedNode->_condition.c_str(), sizeof( condBuf ) - 1 );
					if ( ImGui::InputText( "Condition", condBuf, sizeof( condBuf ) ) )
						pSelectedNode->_condition = condBuf;
					ImGui::TextDisabled( "Ex: flag.boss_defeated == 1" );
				}
				else if ( pSelectedNode->_type == DialogueNodeType::Action )
				{
					utf8 cmdBuf[128]{};
					StringUtil::strncpy( cmdBuf, pSelectedNode->_actionCommand.c_str(), sizeof( cmdBuf ) - 1 );
					if ( ImGui::InputText( "Command", cmdBuf, sizeof( cmdBuf ) ) )
						pSelectedNode->_actionCommand = cmdBuf;
					ImGui::TextDisabled( "Ex: give_item:potion:3" );
				}
			}

			ImGui::EndChild();
		}

		ImGui::End();
	}

	void DialogueGraphTool::ensureDefaults()
	{
		_listNodes.clear();
		_listLinks.clear();

		DialogueNode startNode{};
		startNode._id	= 1;
		startNode._type = DialogueNodeType::Start;
		startNode._x	= 50.0f;
		startNode._y	= 100.0f;
		_listNodes.push_back( startNode );

		DialogueNode diagNode{};
		diagNode._id	  = 2;
		diagNode._type	  = DialogueNodeType::Dialogue;
		diagNode._speaker = "Elder";
		diagNode._text	  = "Greetings adventurer! The ancient ruins ahead are full of peril.";
		diagNode._x		  = 250.0f;
		diagNode._y		  = 100.0f;
		_listNodes.push_back( diagNode );

		DialogueNode choiceNode{};
		choiceNode._id			= 3;
		choiceNode._type		= DialogueNodeType::Choice;
		choiceNode._text		= "How do you respond?";
		choiceNode._listChoices = { "I am ready for any challenge!", "Could you give me some supplies first?" };
		choiceNode._x			= 650.0f;
		choiceNode._y			= 100.0f;
		_listNodes.push_back( choiceNode );

		DialogueNode actionNode{};
		actionNode._id			  = 4;
		actionNode._type		  = DialogueNodeType::Action;
		actionNode._actionCommand = "give_item:healing_potion:3";
		actionNode._x			  = 1050.0f;
		actionNode._y			  = 220.0f;
		_listNodes.push_back( actionNode );

		DialogueNode endNode{};
		endNode._id	  = 5;
		endNode._type = DialogueNodeType::End;
		endNode._x	  = 1350.0f;
		endNode._y	  = 120.0f;
		_listNodes.push_back( endNode );

		// 기본 링크 연결
		_listLinks.push_back( DialogueLink{ 1, pinOut( 1 ), pinIn( 2 ) } );
		_listLinks.push_back( DialogueLink{ 2, pinOut( 2 ), pinIn( 3 ) } );
		_listLinks.push_back( DialogueLink{ 3, pinChoice( 3, 0 ), pinIn( 5 ) } );
		_listLinks.push_back( DialogueLink{ 4, pinChoice( 3, 1 ), pinIn( 4 ) } );
		_listLinks.push_back( DialogueLink{ 5, pinOut( 4 ), pinIn( 5 ) } );
	}

	void DialogueGraphTool::loadGraphData()
	{
		const string path = "Saved/Dialogue/default_dialogue.json";
		if ( FileUtil::fileExists( path ) == false )
		{
			ensureDefaults();
			_bLoaded			 = true;
			_bNavigatedToContent = false;
			return;
		}

		vector<uint8> listData;
		if ( FileUtil::readFile( path, listData ) == false || listData.empty() )
		{
			ensureDefaults();
			_bLoaded			 = true;
			_bNavigatedToContent = false;
			return;
		}

		const string json( listData.begin(), listData.end() );
		_listNodes.clear();
		_listLinks.clear();

		// Nodes parsing
		const size_t nodesPos = json.find( "\"nodes\"" );
		if ( nodesPos != string::npos )
		{
			const size_t arr = json.find( '[', nodesPos );
			const size_t end = json.find( ']', arr );
			if ( arr != string::npos && end != string::npos )
			{
				size_t cursor = arr;
				while ( true )
				{
					const size_t obj = json.find( '{', cursor );
					if ( obj == string::npos || obj > end )
						break;

					DialogueNode node{};
					const size_t idPos = json.find( "\"id\"", obj );
					if ( idPos != string::npos && idPos < end )
					{
						const size_t colon = json.find( ':', idPos );
						utf8*		 pEndPtr{ nullptr };
						node._id = static_cast<int32>( StringUtil::strtoll( json.c_str() + colon + 1, &pEndPtr, 10 ) );
					}

					const size_t typePos = json.find( "\"type\"", obj );
					if ( typePos != string::npos && typePos < end )
					{
						const size_t q0 = json.find( '"', json.find( ':', typePos ) + 1 );
						const size_t q1 = json.find( '"', q0 + 1 );
						if ( q0 != string::npos && q1 != string::npos )
							node._type = parseNodeTypeString( json.substr( q0 + 1, q1 - q0 - 1 ) );
					}

					const size_t speakerPos = json.find( "\"speaker\"", obj );
					if ( speakerPos != string::npos && speakerPos < end )
					{
						const size_t q0 = json.find( '"', json.find( ':', speakerPos ) + 1 );
						const size_t q1 = json.find( '"', q0 + 1 );
						if ( q0 != string::npos && q1 != string::npos )
							node._speaker.assign( json, q0 + 1, q1 - q0 - 1 );
					}

					const size_t textPos = json.find( "\"text\"", obj );
					if ( textPos != string::npos && textPos < end )
					{
						const size_t q0 = json.find( '"', json.find( ':', textPos ) + 1 );
						const size_t q1 = json.find( '"', q0 + 1 );
						if ( q0 != string::npos && q1 != string::npos )
							node._text.assign( json, q0 + 1, q1 - q0 - 1 );
					}

					const size_t condPos = json.find( "\"condition\"", obj );
					if ( condPos != string::npos && condPos < end )
					{
						const size_t q0 = json.find( '"', json.find( ':', condPos ) + 1 );
						const size_t q1 = json.find( '"', q0 + 1 );
						if ( q0 != string::npos && q1 != string::npos )
							node._condition.assign( json, q0 + 1, q1 - q0 - 1 );
					}

					const size_t actPos = json.find( "\"action\"", obj );
					if ( actPos != string::npos && actPos < end )
					{
						const size_t q0 = json.find( '"', json.find( ':', actPos ) + 1 );
						const size_t q1 = json.find( '"', q0 + 1 );
						if ( q0 != string::npos && q1 != string::npos )
							node._actionCommand.assign( json, q0 + 1, q1 - q0 - 1 );
					}

					const size_t xPos = json.find( "\"x\"", obj );
					if ( xPos != string::npos && xPos < end )
					{
						const size_t colon = json.find( ':', xPos );
						node._x			   = static_cast<float32>( StringUtil::atof( json.c_str() + colon + 1 ) );
					}

					const size_t yPos = json.find( "\"y\"", obj );
					if ( yPos != string::npos && yPos < end )
					{
						const size_t colon = json.find( ':', yPos );
						node._y			   = static_cast<float32>( StringUtil::atof( json.c_str() + colon + 1 ) );
					}

					if ( node._id > 0 )
						_listNodes.push_back( node );

					cursor = json.find( '}', obj );
					if ( cursor == string::npos )
						break;
					++cursor;
				}
			}
		}

		// Links parsing
		const size_t linksPos = json.find( "\"links\"" );
		if ( linksPos != string::npos )
		{
			const size_t arr = json.find( '[', linksPos );
			const size_t end = json.find( ']', arr );
			if ( arr != string::npos && end != string::npos )
			{
				size_t cursor = arr;
				while ( true )
				{
					const size_t obj = json.find( '{', cursor );
					if ( obj == string::npos || obj > end )
						break;

					DialogueLink l{};
					auto		 parseInt = [&]( const utf8* pKey, int32& out )
					{
						const size_t p = json.find( string( "\"" ) + pKey + "\"", obj );
						if ( p == string::npos || p > end )
							return;
						const size_t colon = json.find( ':', p );
						utf8*		 pEndPtr{ nullptr };
						out = static_cast<int32>( StringUtil::strtoll( json.c_str() + colon + 1, &pEndPtr, 10 ) );
					};

					parseInt( "id", l._id );
					parseInt( "from", l._fromPin );
					parseInt( "to", l._toPin );
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

	void DialogueGraphTool::saveGraphData() const
	{
		const string path = "Saved/Dialogue/default_dialogue.json";
		FileUtil::ensureDirectoryExists( path );

		StringBuilder<constant::kMaxBuffer4096> sb;
		sb.append( "{\n  \"nodes\": [\n" );
		for ( size_t nodeIndex = 0; nodeIndex < _listNodes.size(); ++nodeIndex )
		{
			const DialogueNode& n = _listNodes[nodeIndex];
			sb.append( "    { \"id\": " ).append( n._id );
			sb.append( ", \"type\": \"" ).append( getNodeTypeString( n._type ) ).append( "\"" );
			sb.append( ", \"speaker\": \"" ).append( n._speaker.c_str() ).append( "\"" );
			sb.append( ", \"text\": \"" ).append( n._text.c_str() ).append( "\"" );
			sb.append( ", \"condition\": \"" ).append( n._condition.c_str() ).append( "\"" );
			sb.append( ", \"action\": \"" ).append( n._actionCommand.c_str() ).append( "\"" );
			sb.append( ", \"x\": " ).append( n._x );
			sb.append( ", \"y\": " ).append( n._y );
			sb.append( " }" );
			if ( nodeIndex + 1 < _listNodes.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ],\n  \"links\": [\n" );
		for ( size_t linkIndex = 0; linkIndex < _listLinks.size(); ++linkIndex )
		{
			const DialogueLink& l = _listLinks[linkIndex];
			sb.append( "    { \"id\": " ).append( l._id );
			sb.append( ", \"from\": " ).append( l._fromPin );
			sb.append( ", \"to\": " ).append( l._toPin );
			sb.append( " }" );
			if ( linkIndex + 1 < _listLinks.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ]\n}\n" );

		FileUtil::writeTextFile( path, sb.view() );
		SW_LOG_INFO( "[DialogueGraphTool] Saved %zu nodes, %zu links -> %#", _listNodes.size(), _listLinks.size(), path );
	}

	int32 DialogueGraphTool::nextNodeId() const
	{
		int32 maxId = 0;
		for ( const DialogueNode& node : _listNodes )
		{
			if ( node._id > maxId )
				maxId = node._id;
		}
		return maxId + 1;
	}

	int32 DialogueGraphTool::nextLinkId() const
	{
		int32 maxId = 0;
		for ( const DialogueLink& link : _listLinks )
		{
			if ( link._id > maxId )
				maxId = link._id;
		}
		return maxId + 1;
	}

	void DialogueGraphTool::addNode( DialogueNodeType type, const utf8* pSpeaker, const utf8* pText )
	{
		DialogueNode node{};
		node._id	  = nextNodeId();
		node._type	  = type;
		node._speaker = pSpeaker;
		node._text	  = pText;
		node._x		  = 200.0f + static_cast<float32>( ( node._id % 5 ) * 80 );
		node._y		  = 150.0f + static_cast<float32>( ( node._id % 5 ) * 60 );

		if ( type == DialogueNodeType::Choice )
			node._listChoices = { "Option 1", "Option 2" };

		_listNodes.push_back( node );
		_selectedNodeId = node._id;
	}
} // namespace sw
