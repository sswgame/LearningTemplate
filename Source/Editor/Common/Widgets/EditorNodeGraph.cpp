// 노드 캔버스 지우기 구간 금지(EditorInputBan.h)를 받지 않는 유일한 자리다 — collectRemovals 가 그 구간의 입구다(표시 SW_EDITOR_NODE_GRAPH_ADAPTER=1 은 CMake `sw_applyEditorInputBan` 만 준다).
#include "pch.h"

#include "Editor/Common/Widgets/EditorNodeGraph.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    namespace
    {
        /** @brief 가장 최근에 그린 그래프의 노드 수 — 감시 값이 읽는다(그래프 패널은 RTTI 없이 찾을 수 없다). 에디터 범위의 상태 저장소가 소유한다. */
        struct EditorNodeGraphDrawnState
        {
            uint32 _nodeCount{ 0 };
            uint32 _autoLinkCount{ 0 };     ///< 핀을 끌어 만든 노드에 이은 링크 수(누적)
            uint32 _alignedNodeCount{ 0 };  ///< 마지막 정렬이 옮긴 노드 수
            bool   _bMinimapShown{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            uint32 _commentCount{ 0 };      ///< 가장 최근에 그린 캔버스의 주석 상자 수
        };

        /** @brief 그래프 복사 버퍼 — 마지막으로 복사한 노드 조각과 그 애셋 종류입니다(유니티처럼 편집기 프로세스 안에 둔다). */
        struct EditorNodeGraphClipboardState
        {
            string _kind;
            string _text;
        };

        struct EditorNodeGraphInternal
        {
            /// @brief 맞춤 · 미니맵 이동의 보기 움직임 시간(초) — 라이브러리 기본 스크롤 시간과 같은 결로 짧게 둔다.
            static constexpr float32 kNavigateSeconds = 0.2f;
            /// @brief 핀에서 고른 노드가 나타나기를 기다리는 프레임 — 패널은 고른 프레임에 노드를 넣고 다음 프레임에 그린다. 넣지 못했으면(한도) 요청을 버린다.
            static constexpr uint32 kAutoLinkWaitFrames = 3;
            /// @brief 미니맵의 기본 크기(글자 줄 높이 배) — 언리얼 · Godot 미니맵처럼 캔버스 구석의 작은 판이다.
            static constexpr float32 kMinimapWidthLines  = 11.0f;
            static constexpr float32 kMinimapHeightLines = 7.0f;
            /// @brief 미니맵이 캔버스에서 차지할 수 있는 몫 — 좁은 캔버스에서 노드를 가리지 않는다.
            static constexpr float32 kMinimapMaxFraction = 0.3f;
            /// @brief 이보다 작으면 미니맵을 그리지 않는다(글자 줄 높이 배).
            static constexpr float32 kMinimapMinLines = 3.0f;
            /// @brief 고른 노드를 감싸는 새 주석 상자의 여백(캔버스 좌표) — 언리얼 Comment 의 감싸기 여백과 같은 결.
            static constexpr float32 kCommentPadding = 24.0f;
            /// @brief 고른 노드가 없을 때 새 주석 상자의 크기(캔버스 좌표).
            static constexpr float32 kCommentDefaultWidth  = 320.0f;
            static constexpr float32 kCommentDefaultHeight = 200.0f;
            /// @brief 주석 상자 바탕의 불투명도 — 안의 노드와 격자가 비쳐 보이게.
            static constexpr float32 kCommentFillAlpha = 0.35f;
        };
    } // namespace

    EditorNodeGraph::EditorNodeGraph()
        : _pEditor{ nullptr }
        , _settingsPath{}
        , _previousCanvasSize{}
        , _canvasSize{}
        , _listPinTypeColor{}
        , _listNodeIssue{}
        , _addNodeFilter{}
        , _addNodePosition{}
        , _canvasOrigin{}
        , _listFrameNode{}
        , _listFramePin{}
        , _listMinimapRect{}
        , _listSelectedNode{}
        , _listAutoLinkBefore{}
        , _viewMin{}
        , _viewMax{}
        , _viewBarSize{}
        , _pendingAlign{ EditorGraphAlign::Count }
        , _currentNodeID{ 0 }
        , _pendingFocusNodeID{}
        , _pinDragSource{ 0 }
        , _autoLinkPin{ 0 }
        , _autoLinkFramesLeft{ 0 }
        , _bNeedsContentFit{ SW_TRUE }
        , _bPendingNavigate{ SW_FALSE }
        , _bOpenAddFromPin{ SW_FALSE }
        , _bMinimapVisible{ SW_TRUE }
        , _bAutoLayout{ SW_FALSE }
        , _bClearSelection{ SW_FALSE }
        , _bCommentsEnabled{ SW_FALSE }
        , _bCommentRequested{ SW_FALSE }
        , _bCommentReady{ SW_FALSE }
        , _commentPosition{}
        , _commentSize{}
        , _renameCommentID{ 0 }
        , _renameText{}
        , _listSeededComment{}
    {
    }

    EditorNodeGraph::~EditorNodeGraph()
    {
        destroyContext();
    }

    void EditorNodeGraph::shutdown()
    {
        destroyContext();
    }

    void EditorNodeGraph::ensureContext( const utf8* pSettingsFileName )
    {
        if ( _pEditor != nullptr )
            return;

        ed::Config config{};
        if ( StringUtil::isNullOrEmpty( pSettingsFileName ) == false )
        {
            const string settingsPath = EditorPaths::resolveEditorStateFile( pSettingsFileName );
            if ( settingsPath.empty() == false )
            {
                _settingsPath       = settingsPath;
                config.SettingsFile = _settingsPath.c_str();
            }
        }

        _pEditor = ed::CreateEditor( &config );
    }

    void EditorNodeGraph::destroyContext()
    {
        if ( _pEditor != nullptr )
        {
            ed::DestroyEditor( _pEditor );
            _pEditor = nullptr;
        }
    }

    bool EditorNodeGraph::beginCanvas( const utf8* pCanvasID, const utf8* pSettingsFileName, const utf8* pMarkScope )
    {
        _markScope = pMarkScope;
        ensureContext( pSettingsFileName );
        if ( _pEditor == nullptr )
            return false;

        // ed::Begin 이 캔버스 크기로 쓰는 값과 같다(남은 영역 전부).
        const ImVec2 available = ImGui::GetContentRegionAvail();
        _previousCanvasSize    = _canvasSize;
        _canvasSize            = float2{ available.x, available.y };
        if ( isCanvasRegionUsable( _canvasSize ) == false )
            return false;

        ed::SetCurrentEditor( _pEditor );
        applyThemeStyle();

        // 캔버스가 창의 첫 그리기면 노드 편집기가 자기 클립 사각형을 창의 빈 첫 그리기 명령에 덮어쓰고, 그 명령은 화면 좌표로 되돌리지 않는다
        // (imgui_canvas 의 EnterLocalSpace 는 마지막 명령이 비어 있지 않을 때만 따로 명령을 연다). 확대 · 축소가 1 이 아니면 배경과 노드가
        // 패널 일부에서 잘린다. 같은 색의 배경을 먼저 그려 마지막 명령을 채운다 — 노드 편집기가 그 위를 다시 칠하므로 보이는 차이는 없다.
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        _canvasOrigin       = float2{ origin.x, origin.y };
        _listFrameNode.clear();
        _listFramePin.clear();
        ImGui::GetWindowDrawList()->AddRectFilled( origin, ImVec2{ origin.x + available.x, origin.y + available.y },
                                                   ImGui::ColorConvertFloat4ToU32( ed::GetStyle().Colors[ed::StyleColor_Bg] ) );

        ed::Begin( pCanvasID );
        if ( _bClearSelection )
        {
            ed::ClearSelection();
            _bClearSelection = SW_FALSE;
        }
        return true;
    }

    EditorGraphEditCommand EditorNodeGraph::queryEditCommand( vector<int32>& outListNodeID, float2& outPastePosition )
    {
        outListNodeID.clear();
        EditorGraphEditCommand command = EditorGraphEditCommand::None;
        if ( ed::BeginShortcut() )
        {
            if ( ed::AcceptCut() )
                command = EditorGraphEditCommand::Cut;
            else if ( ed::AcceptCopy() )
                command = EditorGraphEditCommand::Copy;
            else if ( ed::AcceptPaste() )
                command = EditorGraphEditCommand::Paste;
            else if ( ed::AcceptDuplicate() )
                command = EditorGraphEditCommand::Duplicate;
            const int32        contextCount = command == EditorGraphEditCommand::None ? 0 : ed::GetActionContextSize();
            vector<ed::NodeId> listNode( static_cast<size_t>( contextCount > 0 ? contextCount : 0 ) );
            const int32        nodeCount = listNode.empty() ? 0 : ed::GetActionContextNodes( listNode.data(), contextCount );
            for ( int32 index = 0; index < nodeCount; ++index )
            {
                outListNodeID.push_back( static_cast<int32>( listNode[static_cast<size_t>( index )].Get() ) );
            }
        }
        ed::EndShortcut();
        const ImVec2 canvasMouse = ImGui::GetMousePos(); // 캔버스 안이라 캔버스 좌표다
        outPastePosition         = float2{ canvasMouse.x, canvasMouse.y };
        return command;
    }

    void EditorNodeGraph::storeClipboard( string_view kind, string text )
    {
        EditorNodeGraphClipboardState& state = getEditorState<EditorNodeGraphClipboardState>();
        state._kind                          = string{ kind };
        state._text                          = std::move( text );
    }

    bool EditorNodeGraph::readClipboard( string_view kind, string& outText )
    {
        const EditorNodeGraphClipboardState& state = getEditorState<EditorNodeGraphClipboardState>();
        if ( state._text.empty() || string_view{ state._kind } != kind )
            return false;
        outText = state._text;
        return true;
    }

    void EditorNodeGraph::applyThemeStyle()
    {
        // 라이브러리 기본 색(남색 기운의 반투명 바탕 · 흰 테두리 · 주황 선택)은 테마를 따르지 않았다. 바꾼 테마가 다음 프레임에 따르도록 그릴 때마다 칠한다(값 복사뿐이다).
        // 언리얼 그래프 편집기처럼 캔버스는 창 바탕 한 단계, 노드는 패널 면, 격자는 테두리 색, 강조색은 선택 · 호버에만 쓴다.
        ed::Style&   style                               = ed::GetStyle();
        const ImVec4 windowBg                            = EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "WindowBg" ) );
        const ImVec4 panelBg                             = EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "PanelBg" ) );
        const ImVec4 border                              = EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "Border" ) );
        const ImVec4 mutedText                           = EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "TextMuted" ) );
        const ImVec4 accent                              = EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "Accent" ) );
        const ImVec4 accentWash                          = ImVec4{ accent.x, accent.y, accent.z, 0.16f };
        const ImVec4 accentEdge                          = ImVec4{ accent.x, accent.y, accent.z, 0.5f };
        style.Colors[ed::StyleColor_Bg]                  = windowBg;
        style.Colors[ed::StyleColor_Grid]                = border;
        style.Colors[ed::StyleColor_NodeBg]              = panelBg;
        style.Colors[ed::StyleColor_NodeBorder]          = mutedText;
        style.Colors[ed::StyleColor_HovNodeBorder]       = accentEdge;
        style.Colors[ed::StyleColor_SelNodeBorder]       = accent;
        style.Colors[ed::StyleColor_NodeSelRect]         = accentWash;
        style.Colors[ed::StyleColor_NodeSelRectBorder]   = accentEdge;
        style.Colors[ed::StyleColor_HovLinkBorder]       = accentEdge;
        style.Colors[ed::StyleColor_SelLinkBorder]       = accent;
        style.Colors[ed::StyleColor_HighlightLinkBorder] = accent;
        style.Colors[ed::StyleColor_LinkSelRect]         = accentWash;
        style.Colors[ed::StyleColor_LinkSelRectBorder]   = accentEdge;
        style.Colors[ed::StyleColor_PinRect]             = accentWash;
        style.Colors[ed::StyleColor_PinRectBorder]       = accentEdge;
        style.Colors[ed::StyleColor_GroupBg]             = panelBg;
        style.Colors[ed::StyleColor_GroupBorder]         = border;
    }

    void EditorNodeGraph::endCanvas()
    {
        applyViewRequests();
        collectMinimap();
        measureCommentRequest();
        ed::End();
        EditorSelfTestMarks::record( ( _markScope + ".canvas" ).c_str() ); // 마지막 항목은 캔버스 자식 창이다
        // 막대 · 미니맵은 캔버스 다음에 연 자식 창이라 캔버스 위에 놓이고 마우스도 먼저 받는다(노드 편집기는 그 아래를 누른 것으로 보지 않는다).
        drawViewBar();
        if ( _bMinimapVisible )
            drawMinimap();
        getEditorState<EditorNodeGraphDrawnState>()._bMinimapShown = _bMinimapVisible;
        ed::SetCurrentEditor( nullptr );
    }

    void EditorNodeGraph::beginNode( int32 nodeID )
    {
        _currentNodeID = nodeID;
        _listFrameNode.push_back( nodeID );
        ed::BeginNode( toNodeID( nodeID ) );
    }

    void EditorNodeGraph::endNode()
    {
        ed::EndNode();
        _currentNodeID = 0;
    }

    void EditorNodeGraph::beginPin( int32 pinID, bool bInput )
    {
        _listFramePin.push_back( FramePin{ pinID, _currentNodeID } );
        ed::BeginPin( toPinID( pinID ), bInput ? ed::PinKind::Input : ed::PinKind::Output );
    }

    void EditorNodeGraph::endPin()
    {
        ed::EndPin();
        if ( EditorSelfTestMarks::isEnabled() == false || _listFramePin.empty() )
            return;
        // 캔버스 안의 항목 좌표는 캔버스 좌표다 — 화면 좌표로 바꿔 적는다(시나리오가 핀을 빈 곳으로 끈다).
        const ImVec2                           pinMin = ed::CanvasToScreen( ImGui::GetItemRectMin() );
        const ImVec2                           pinMax = ed::CanvasToScreen( ImGui::GetItemRectMax() );
        fixed_string<constant::buffer::kMax64> markName;
        formatstring( markName.data(), markName.capacity(), "%#.pin.%#", _markScope.c_str(), _listFramePin.back()._pinID );
        EditorSelfTestMarks::recordRect( markName.c_str(), float2{ pinMin.x, pinMin.y }, float2{ pinMax.x, pinMax.y } );
    }

    void EditorNodeGraph::applyViewRequests()
    {
        if ( _bPendingNavigate )
        {
            // 언리얼 그래프의 Zoom to Fit(Home) 과 고른 노드로 가기(F)를 한 단추로 둔다 — 고른 노드가 있으면 그 노드로.
            if ( ed::GetSelectedObjectCount() > 0 )
                ed::NavigateToSelection( false, EditorNodeGraphInternal::kNavigateSeconds );
            else
                ed::NavigateToContent( EditorNodeGraphInternal::kNavigateSeconds, 1.0f );
            _bPendingNavigate = SW_FALSE;
        }
        if ( _pendingFocusNodeID.has_value() )
        {
            const ImVec2 position = ed::GetNodePosition( toNodeID( *_pendingFocusNodeID ) );
            const ImVec2 size     = ed::GetNodeSize( toNodeID( *_pendingFocusNodeID ) );
            ed::NavigateToRect( position, ImVec2{ position.x + size.x, position.y + size.y }, false, EditorNodeGraphInternal::kNavigateSeconds );
            _pendingFocusNodeID.reset();
        }
        if ( _pendingAlign == EditorGraphAlign::Count )
            return;
        vector<EditorGraphNodeRect> listRect;
        for ( const EditorGraphNodeRect& rect : _listMinimapRect ) // 앞 프레임에 모은 노드 중 고른 것 — 정렬 메뉴도 그 목록으로 켜고 껐다
        {
            if ( VectorUtil::contains( _listSelectedNode, rect._nodeID ) == false )
                continue;
            const ImVec2        position = ed::GetNodePosition( toNodeID( rect._nodeID ) );
            const ImVec2        size     = ed::GetNodeSize( toNodeID( rect._nodeID ) );
            EditorGraphNodeRect current{};
            current._nodeID   = rect._nodeID;
            current._position = float2{ position.x, position.y };
            current._size     = float2{ size.x, size.y };
            listRect.push_back( current );
        }
        uint32 alignedCount = 0;
        if ( EditorNodeGraphRules::alignNodes( listRect, _pendingAlign ) )
        {
            // 패널은 다음 프레임에 캔버스 위치를 읽어 자기 데이터에 싣고 편집으로 남긴다(끌어 옮긴 것과 같은 길).
            for ( const EditorGraphNodeRect& rect : listRect )
            {
                ed::SetNodePosition( toNodeID( rect._nodeID ), ImVec2{ rect._position._x, rect._position._y } );
            }
            alignedCount = static_cast<uint32>( listRect.size() );
        }
        getEditorState<EditorNodeGraphDrawnState>()._alignedNodeCount = alignedCount;
        _pendingAlign                                                 = EditorGraphAlign::Count;
    }

    bool EditorNodeGraph::drawComments( vector<GraphCommentBox>& listComment, bool bSeed )
    {
        if ( bSeed )
            _listSeededComment.clear();
        const ImVec4 panel = EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "PanelBg" ) );
        ed::PushStyleColor( ed::StyleColor_NodeBg, ImVec4{ panel.x, panel.y, panel.z, EditorNodeGraphInternal::kCommentFillAlpha } );
        ed::PushStyleColor( ed::StyleColor_NodeBorder, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "Border" ) ) );
        for ( const GraphCommentBox& comment : listComment )
        {
            const ed::NodeId nodeID{ static_cast<uintptr_t>( kCommentNodeIDBase + comment._id ) };
            if ( VectorUtil::contains( _listSeededComment, comment._id ) == false )
            {
                ed::SetNodePosition( nodeID, ImVec2{ comment._position._x, comment._position._y } );
                ed::SetGroupSize( nodeID, ImVec2{ comment._size._x, comment._size._y } );
                _listSeededComment.push_back( comment._id );
            }
            ed::BeginNode( nodeID );
            ImGui::TextUnformatted( comment._text.empty() ? SW_LOCKEY( "EdCommon", "GraphCommentUntitled" ) : comment._text.c_str() );
            ed::Group( ImVec2{ comment._size._x, comment._size._y } );
            ed::EndNode();
        }
        ed::PopStyleColor( 2 );
        getEditorState<EditorNodeGraphDrawnState>()._commentCount = static_cast<uint32>( listComment.size() );

        // 머리줄을 두 번 누르면 이름 바꾸기(언리얼 Comment 의 제목 고치기).
        const int32 doubleClicked = static_cast<int32>( ed::GetDoubleClickedNode().Get() );
        bool        bRenamed      = false;
        ed::Suspend();
        if ( isCommentNode( doubleClicked ) )
        {
            const GraphCommentBox* pComment = VectorUtil::findBy<&GraphCommentBox::_id>( listComment, doubleClicked - kCommentNodeIDBase );
            _renameCommentID                = pComment != nullptr ? pComment->_id : 0;
            _renameText                     = fixed_string<constant::buffer::kMax64>{ pComment != nullptr ? pComment->_text.c_str() : "" };
            if ( pComment != nullptr )
                EditorInputGUI::openPopup( "##graphCommentRename" );
        }
        if ( ImGui::BeginPopup( "##graphCommentRename" ) )
        {
            if ( ImGui::IsWindowAppearing() )
                ImGui::SetKeyboardFocusHere();
            EditorChrome::setNextItemWidthColumns( 29.0f );
            const bool bSubmitted = EditorWidgets::drawHintField( "##commentTitle", _renameText, SW_LOCKEY( "EdCommon", "GraphCommentHint" ), ( _markScope + ".comment.title" ).c_str(), true )._bSubmitted;
            if ( bSubmitted || ImGui::IsItemDeactivatedAfterEdit() )
            {
                for ( GraphCommentBox& comment : listComment )
                {
                    if ( comment._id != _renameCommentID || comment._text == _renameText.c_str() )
                        continue;
                    comment._text = _renameText.c_str();
                    bRenamed      = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ed::Resume();
        return bRenamed;
    }

    bool EditorNodeGraph::syncCommentLayout( vector<GraphCommentBox>& listComment ) const
    {
        bool bChanged = false;
        for ( GraphCommentBox& comment : listComment )
        {
            if ( VectorUtil::contains( _listSeededComment, comment._id ) == false )
                continue; // 아직 캔버스에 넣지 않았다 — 데이터가 원본이다
            const ed::NodeId nodeID{ static_cast<uintptr_t>( kCommentNodeIDBase + comment._id ) };
            const ImVec2     position = ed::GetNodePosition( nodeID );
            const ImVec2     size     = ed::GetGroupSize( nodeID );
            if ( size.x <= 0.0f || size.y <= 0.0f )
                continue; // 그룹 영역이 아직 없다(처음 그린 프레임)
            const bool bMoved   = EditorSessionPolicy::hasNodeMoved( comment._position._x, comment._position._y, position.x, position.y );
            const bool bResized = EditorSessionPolicy::hasNodeMoved( comment._size._x, comment._size._y, size.x, size.y );
            bChanged            = bChanged || bMoved || bResized;
            comment._position   = float2{ position.x, position.y };
            comment._size       = float2{ size.x, size.y };
        }
        return bChanged;
    }

    bool EditorNodeGraph::takeCommentRequest( float2& outPosition, float2& outSize )
    {
        if ( _bCommentReady == false )
            return false;
        outPosition    = _commentPosition;
        outSize        = _commentSize;
        _bCommentReady = SW_FALSE;
        return true;
    }

    bool EditorNodeGraph::addRequestedComment( vector<GraphCommentBox>& listComment )
    {
        GraphCommentBox comment{};
        if ( takeCommentRequest( comment._position, comment._size ) == false )
            return false;
        int32 maxID{ 0 };
        for ( const GraphCommentBox& existing : listComment )
        {
            maxID = MathUtil::max( maxID, existing._id );
        }
        comment._id = maxID + 1;
        listComment.push_back( std::move( comment ) );
        return true;
    }

    bool EditorNodeGraph::removeComments( vector<GraphCommentBox>& listComment, const vector<int32>& listCommentID )
    {
        const size_t countBefore = listComment.size();
        VectorUtil::removeIf( listComment, [&listCommentID]( const GraphCommentBox& comment )
        { return VectorUtil::contains( listCommentID, comment._id ); } );
        return listComment.size() != countBefore;
    }

    void EditorNodeGraph::collectRemovals( EditorGraphRemoval& outDeletion, const EditorGraphLinkRemoval linkRemoval )
    {
        outDeletion._listLinkID.clear();
        outDeletion._listNodeID.clear();
        outDeletion._listCommentID.clear();
        outDeletion._rejectedLinkCount = 0;
        if ( ed::BeginDelete() )
        {
            ed::LinkId linkID;
            while ( ed::QueryDeletedLink( &linkID ) )
            {
                if ( linkRemoval == EditorGraphLinkRemoval::Reject )
                {
                    ed::RejectDeletedItem();
                    ++outDeletion._rejectedLinkCount;
                }
                else if ( ed::AcceptDeletedItem() )
                {
                    outDeletion._listLinkID.push_back( static_cast<int32>( linkID.Get() ) );
                }
            }
            ed::NodeId nodeID;
            while ( ed::QueryDeletedNode( &nodeID ) )
            {
                if ( ed::AcceptDeletedItem() == false )
                    continue;
                const int32 id = static_cast<int32>( nodeID.Get() );
                if ( isCommentNode( id ) )
                    outDeletion._listCommentID.push_back( id - kCommentNodeIDBase );
                else
                    outDeletion._listNodeID.push_back( id );
            }
        }
        ed::EndDelete();
    }

    void EditorNodeGraph::requestNodeDeletion( const int32 nodeID )
    {
        ed::DeleteNode( toNodeID( nodeID ) );
    }

    bool EditorNodeGraph::findSelectedNode( int32& outNodeID ) const
    {
        ed::NodeId arrSelected[1];
        if ( ed::GetSelectedNodes( arrSelected, 1 ) <= 0 )
            return false;
        outNodeID = static_cast<int32>( arrSelected[0].Get() );
        return true;
    }

    void EditorNodeGraph::finishCanvas( const uint32 drawnNodeCount )
    {
        applyContentFitIfNeeded();
        recordDrawnNodeCount( drawnNodeCount );
        endCanvas();
    }

    uint32 EditorNodeGraph::getDrawnCommentCount()
    {
        return getEditorState<EditorNodeGraphDrawnState>()._commentCount;
    }

    void EditorNodeGraph::measureCommentRequest()
    {
        if ( _bCommentRequested == false )
            return;
        _bCommentRequested = SW_FALSE;
        // 고른 노드(주석 상자 제외)를 여백을 두고 감싼다. 없으면 보이는 가운데에 기본 크기로 둔다(언리얼 C 키 · Godot 프레임 만들기).
        bool   bAny      = false;
        float2 boundsMin = _viewMin;
        float2 boundsMax = _viewMax;
        for ( const EditorGraphNodeRect& rect : _listMinimapRect )
        {
            if ( isCommentNode( rect._nodeID ) || VectorUtil::contains( _listSelectedNode, rect._nodeID ) == false )
                continue;
            boundsMin = bAny ? float2{ MathUtil::min( boundsMin._x, rect._position._x ), MathUtil::min( boundsMin._y, rect._position._y ) } : rect._position;
            boundsMax = bAny ? float2{ MathUtil::max( boundsMax._x, rect._position._x + rect._size._x ), MathUtil::max( boundsMax._y, rect._position._y + rect._size._y ) }
                             : rect._position + rect._size;
            bAny      = true;
        }
        const float32 padding = EditorNodeGraphInternal::kCommentPadding;
        const float32 header  = ImGui::GetTextLineHeightWithSpacing() + ed::GetStyle().NodePadding.y * 2.0f; // 머리줄(제목 한 줄) 높이만큼 위로 비켜 노드가 영역 안에 든다
        if ( bAny )
        {
            _commentPosition = float2{ boundsMin._x - padding, boundsMin._y - padding - header };
            _commentSize     = float2{ boundsMax._x - boundsMin._x + padding * 2.0f, boundsMax._y - boundsMin._y + padding * 2.0f };
        }
        else
        {
            const float2 center = ( _viewMin + _viewMax ) * 0.5f;
            _commentSize        = float2{ EditorNodeGraphInternal::kCommentDefaultWidth, EditorNodeGraphInternal::kCommentDefaultHeight };
            _commentPosition    = center - _commentSize * 0.5f;
        }
        _bCommentReady = SW_TRUE;
    }

    void EditorNodeGraph::collectMinimap()
    {
        _listMinimapRect.clear();
        _listSelectedNode.clear();
        const int32        nodeCount = ed::GetNodeCount();
        vector<ed::NodeId> listNode( static_cast<size_t>( nodeCount > 0 ? nodeCount : 0 ) );
        const int32        filled = listNode.empty() ? 0 : ed::GetOrderedNodeIds( listNode.data(), nodeCount );
        for ( int32 index = 0; index < filled; ++index )
        {
            const ed::NodeId    nodeID   = listNode[static_cast<size_t>( index )];
            const ImVec2        position = ed::GetNodePosition( nodeID );
            const ImVec2        size     = ed::GetNodeSize( nodeID );
            EditorGraphNodeRect rect{};
            rect._nodeID   = static_cast<int32>( nodeID.Get() );
            rect._position = float2{ position.x, position.y };
            rect._size     = float2{ size.x, size.y };
            _listMinimapRect.push_back( rect );
            if ( ed::IsNodeSelected( nodeID ) )
                _listSelectedNode.push_back( static_cast<int32>( nodeID.Get() ) );
        }
        const ImVec2 viewMin = ed::ScreenToCanvas( ImVec2{ _canvasOrigin._x, _canvasOrigin._y } );
        const ImVec2 viewMax = ed::ScreenToCanvas( ImVec2{ _canvasOrigin._x + _canvasSize._x, _canvasOrigin._y + _canvasSize._y } );
        _viewMin             = float2{ viewMin.x, viewMin.y };
        _viewMax             = float2{ viewMax.x, viewMax.y };
    }

    void EditorNodeGraph::drawViewBar()
    {
        const float32 margin = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorScreenPos( ImVec2{ _canvasOrigin._x + _canvasSize._x - _viewBarSize._x - margin, _canvasOrigin._y + margin } );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "PanelBg" ) ) );
        const ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding;
        if ( ImGui::BeginChild( "##graphViewBar", ImVec2{ 0.0f, 0.0f }, childFlags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings ) )
        {
            if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "GraphFit" ), "Fit" ) ) )
                _bPendingNavigate = SW_TRUE;
            EditorSelfTestMarks::record( ( _markScope + ".view.fit" ).c_str() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "GraphFitTip" ) );
            if ( _bAutoLayout == false )
            {
                ImGui::SameLine();
                if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "GraphAlign" ), "Align" ) ) )
                    EditorInputGUI::openPopup( "##graphAlignMenu" );
                EditorSelfTestMarks::record( ( _markScope + ".view.align" ).c_str() );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "GraphAlignTip" ) );
                drawAlignMenu();
            }
            if ( _bCommentsEnabled )
            {
                ImGui::SameLine();
                if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "GraphComment" ), "Comment" ) ) )
                    _bCommentRequested = SW_TRUE;
                EditorSelfTestMarks::record( ( _markScope + ".view.comment" ).c_str() );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "GraphCommentTip" ) );
            }
            ImGui::SameLine();
            ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( _bMinimapVisible ? SW_THEME_COLOR( "Base", "ToggleActive" ) : SW_THEME_COLOR( "Base", "ToggleInactive" ) ) );
            if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "GraphMinimap" ), "Minimap" ) ) )
                _bMinimapVisible = _bMinimapVisible == false;
            ImGui::PopStyleColor();
            EditorSelfTestMarks::record( ( _markScope + ".view.minimap" ).c_str() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "GraphMinimapTip" ) );
            const ImVec2 barSize = ImGui::GetWindowSize();
            _viewBarSize         = float2{ barSize.x, barSize.y };
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void EditorNodeGraph::drawAlignMenu()
    {
        if ( ImGui::BeginPopup( "##graphAlignMenu" ) == false )
            return;
        // 언리얼 그래프 Alignment 메뉴의 순서 — 맞추기 넷, 나누기 둘. 고른 노드가 모자라면 끄고 까닭은 툴팁으로.
        struct AlignItem
        {
            const utf8*      _pLabel;
            const utf8*      _pMark;
            EditorGraphAlign _align;
            uint32           _minCount;
        };
        const AlignItem arrItem[] = {
            {SW_LOCKEY( "EdCommon",    "GraphAlignLeft" ),        ".view.align.left",                 EditorGraphAlign::Left, 2},
            {SW_LOCKEY( "EdCommon",     "GraphAlignTop" ),         ".view.align.top",                  EditorGraphAlign::Top, 2},
            {SW_LOCKEY( "EdCommon", "GraphAlignCenterX" ),     ".view.align.centerX",     EditorGraphAlign::CenterHorizontal, 2},
            {SW_LOCKEY( "EdCommon", "GraphAlignCenterY" ),     ".view.align.centerY",       EditorGraphAlign::CenterVertical, 2},
            {SW_LOCKEY( "EdCommon",  "GraphDistributeX" ), ".view.align.distributeX", EditorGraphAlign::DistributeHorizontal, 3},
            {SW_LOCKEY( "EdCommon",  "GraphDistributeY" ), ".view.align.distributeY",   EditorGraphAlign::DistributeVertical, 3},
        };
        const uint32 selectedCount = static_cast<uint32>( _listSelectedNode.size() );
        for ( const AlignItem& item : arrItem )
        {
            if ( ImGui::MenuItem( EditorThemeUtil::makeStableLabel( item._pLabel, item._pMark ), nullptr, false, selectedCount >= item._minCount ) )
                _pendingAlign = item._align;
            EditorSelfTestMarks::record( ( _markScope + item._pMark ).c_str() );
            if ( selectedCount < item._minCount )
                EditorWidgets::drawTooltip( item._minCount > 2 ? SW_LOCKEY( "EdCommon", "GraphNeedThreeNodes" ) : SW_LOCKEY( "EdCommon", "GraphNeedTwoNodes" ) );
        }
        ImGui::EndPopup();
    }

    void EditorNodeGraph::drawMinimap()
    {
        const float32 line   = ImGui::GetTextLineHeightWithSpacing();
        const float32 width  = MathUtil::min( line * EditorNodeGraphInternal::kMinimapWidthLines, _canvasSize._x * EditorNodeGraphInternal::kMinimapMaxFraction );
        const float32 height = MathUtil::min( line * EditorNodeGraphInternal::kMinimapHeightLines, _canvasSize._y * EditorNodeGraphInternal::kMinimapMaxFraction );
        if ( width < line * EditorNodeGraphInternal::kMinimapMinLines || height < line * EditorNodeGraphInternal::kMinimapMinLines )
            return; // 캔버스가 좁아 판이 노드를 가린다
        const float32 margin = ImGui::GetStyle().ItemSpacing.x;
        const ImVec2  mapMin{ _canvasOrigin._x + _canvasSize._x - width - margin, _canvasOrigin._y + _canvasSize._y - height - margin };
        ImGui::SetCursorScreenPos( mapMin );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "PanelBg" ) ) );
        if ( ImGui::BeginChild( "##graphMinimap", ImVec2{ width, height }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings ) )
        {
            const float2                inset{ ImGui::GetStyle().WindowPadding.x, ImGui::GetStyle().WindowPadding.y };
            const float2                inner{ width - inset._x * 2.0f, height - inset._y * 2.0f };
            const float2                innerMin{ mapMin.x + inset._x, mapMin.y + inset._y };
            EditorGraphMinimapTransform transform{};
            if ( EditorNodeGraphRules::fitMinimap( _listMinimapRect, _viewMin, _viewMax, inner, transform ) )
            {
                ImDrawList*  pDrawList = ImGui::GetWindowDrawList();
                const ImVec4 muted     = EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "TextMuted" ) );
                const ImU32  nodeColor = ImGui::ColorConvertFloat4ToU32( ImVec4{ muted.x, muted.y, muted.z, 0.55f } );
                const ImU32  pickColor = EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Accent" ) );
                const ImU32  failColor = EditorColorUtil::toImU32( SW_THEME_COLOR( "Signal", "Error" ) );
                for ( const EditorGraphNodeRect& rect : _listMinimapRect )
                {
                    const float2 cornerA = EditorNodeGraphRules::canvasToMinimap( transform, rect._position ) + innerMin;
                    const float2 cornerB = EditorNodeGraphRules::canvasToMinimap( transform, rect._position + rect._size ) + innerMin;
                    bool         bIssue  = false;
                    for ( const EditorGraphNodeIssue& issue : _listNodeIssue )
                    {
                        bIssue = bIssue || issue._nodeID == rect._nodeID;
                    }
                    // 강조색은 고른 노드에만, 검증 오류는 캔버스의 노드 테두리와 같은 빨강이다.
                    const bool  bSelected = VectorUtil::contains( _listSelectedNode, rect._nodeID );
                    const ImU32 color     = bIssue ? failColor : ( bSelected ? pickColor : nodeColor );
                    pDrawList->AddRectFilled( ImVec2{ cornerA._x, cornerA._y }, ImVec2{ cornerB._x, cornerB._y }, color );
                }
                const float2 viewA = EditorNodeGraphRules::canvasToMinimap( transform, _viewMin ) + innerMin;
                const float2 viewB = EditorNodeGraphRules::canvasToMinimap( transform, _viewMax ) + innerMin;
                pDrawList->AddRect( ImVec2{ viewA._x, viewA._y }, ImVec2{ viewB._x, viewB._y }, EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Text" ) ) );
            }
            ImGui::SetCursorScreenPos( mapMin );
            ImGui::InvisibleButton( "##graphMinimapArea", ImVec2{ width, height } );
            EditorSelfTestMarks::record( ( _markScope + ".minimap" ).c_str() );
            // 누르거나 끄는 곳에 가장 가까운 노드로 보기를 옮긴다(Godot GraphEdit · 언리얼 머티리얼 그래프 미니맵의 끌어 옮기기).
            if ( ImGui::IsItemActive() && transform._scale > 0.0f )
            {
                const ImVec2 mouse       = ImGui::GetMousePos();
                const float2 canvasPoint = EditorNodeGraphRules::minimapToCanvas( transform, float2{ mouse.x - innerMin._x, mouse.y - innerMin._y } );
                _pendingFocusNodeID      = EditorNodeGraphRules::findNearestNode( _listMinimapRect, canvasPoint );
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "GraphMinimapAreaTip" ) );
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    bool EditorNodeGraph::drawAddNodePopup( const vector<EditorGraphNodeKind>& listKind, uint32& outKindID, float2& outCanvasPosition )
    {
        // 캔버스 안의 마우스 위치는 캔버스 좌표다 — 팝업을 그리려고 멈추기(Suspend) 전에 잡는다.
        const ImVec2 canvasMouse = ImGui::GetMousePos();
        ed::Suspend();
        if ( ed::ShowBackgroundContextMenu() )
        {
            _addNodePosition = float2{ canvasMouse.x, canvasMouse.y };
            _pinDragSource   = 0; // 빈 곳에서 연 검색은 이을 핀이 없다
            _addNodeFilter.clear();
            EditorInputGUI::openPopup( "AddGraphNode" );
        }
        if ( _bOpenAddFromPin )
        {
            // 핀을 빈 곳에 놓았다(언리얼 · 유니티 그래프의 "핀에서 끌어 노드 만들기") — 위치는 놓은 곳(queryNewLink 가 잡았다).
            _bOpenAddFromPin = SW_FALSE;
            _addNodeFilter.clear();
            EditorInputGUI::openPopup( "AddGraphNode" );
        }
        bool bPicked = false;
        if ( ImGui::BeginPopup( "AddGraphNode" ) )
        {
            if ( ImGui::IsWindowAppearing() )
                ImGui::SetKeyboardFocusHere();
            EditorChrome::setNextItemWidthColumns( 29.0f );
            const bool     bEnter = EditorWidgets::drawHintField( "##addNodeSearch", _addNodeFilter, SW_LOCKEY( "EdCommon", "SearchNodesHint" ), ( _markScope + ".addNode.search" ).c_str(), true )._bSubmitted;
            vector<uint32> listIndex;
            EditorNodeGraphRules::filterNodeKinds( listKind, _addNodeFilter.c_str(), listIndex );
            const utf8* pLastCategory = nullptr;
            for ( const uint32 index : listIndex )
            {
                const EditorGraphNodeKind& kind      = listKind[index];
                const utf8*                pCategory = kind._pCategory != nullptr ? kind._pCategory : "General";
                if ( pLastCategory == nullptr || string_view{ pLastCategory } != string_view{ pCategory } )
                {
                    ImGui::SeparatorText( pCategory );
                    pLastCategory = pCategory;
                }
                if ( ImGui::Selectable( kind._pName ) && bPicked == false )
                {
                    outKindID = kind._kindID;
                    bPicked   = true;
                }
            }
            if ( listIndex.empty() )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EdCommon", "NoNodeMatches" ) );
            // Enter 는 맨 위 줄을 고른다(언리얼 · 유니티 검색 팝업과 같다).
            const bool bPickFirstNode = bEnter && bPicked == false && listIndex.empty() == false;
            if ( bPickFirstNode )
            {
                outKindID = listKind[listIndex[0]]._kindID;
                bPicked   = true;
            }
            if ( bPicked )
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        else
        {
            _pinDragSource = 0; // 고르지 않고 닫았다
        }
        if ( bPicked && _pinDragSource != 0 )
        {
            // 패널이 이 프레임에 노드를 넣고 다음 프레임에 그린다 — 그때 새로 나타난 노드의 맞는 핀에 잇는다(takeAutoLink).
            _autoLinkPin        = _pinDragSource;
            _listAutoLinkBefore = _listFrameNode;
            _autoLinkFramesLeft = EditorNodeGraphInternal::kAutoLinkWaitFrames;
            _pinDragSource      = 0;
        }
        ed::Resume();
        outCanvasPosition = _addNodePosition;
        return bPicked;
    }

    bool EditorNodeGraph::queryNewLink( const Delegate<bool( int32, EditorGraphPinInfo& )>& findPin, int32& outFromPin, int32& outToPin )
    {
        bool bAccepted = false;
        if ( ed::BeginCreate() )
        {
            ed::PinId  pinA;
            ed::PinId  pinB;
            const bool bNewLinkProposed = ed::QueryNewLink( &pinA, &pinB ) && pinA.Get() != 0 && pinB.Get() != 0;
            if ( bNewLinkProposed )
            {
                const int32        idA = static_cast<int32>( pinA.Get() );
                const int32        idB = static_cast<int32>( pinB.Get() );
                EditorGraphPinInfo infoA{};
                EditorGraphPinInfo infoB{};
                const utf8*        pReason = SW_LOCKEY( "EdCommon", "UnknownPin" );
                const bool         bKnown  = findPin.isBound() && findPin( idA, infoA ) && findPin( idB, infoB );
                if ( bKnown && EditorNodeGraphRules::canConnect( idA, infoA, idB, infoB, outFromPin, outToPin, pReason ) )
                {
                    if ( ed::AcceptNewItem( EditorColorUtil::toImVec4( getPinTypeColor( infoA._type ) ), 2.0f ) )
                        bAccepted = true;
                }
                else
                {
                    ed::RejectNewItem( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), 2.0f );
                    ed::Suspend();
                    ImGui::SetTooltip( "%s", pReason );
                    ed::Resume();
                }
            }
            // 핀을 빈 곳에 놓으면 노드 검색을 연다. 고른 노드는 놓은 곳에 생기고 이 핀과 이어진다(drawAddNodePopup · takeAutoLink).
            ed::PinId  pinDropped;
            const bool bDroppedOnEmpty = bNewLinkProposed == false && ed::QueryNewNode( &pinDropped ) && pinDropped.Get() != 0;
            if ( bDroppedOnEmpty && ed::AcceptNewItem() )
            {
                const ImVec2 canvasMouse = ImGui::GetMousePos();
                _addNodePosition         = float2{ canvasMouse.x, canvasMouse.y };
                _pinDragSource           = static_cast<int32>( pinDropped.Get() );
                _bOpenAddFromPin         = SW_TRUE;
            }
        }
        ed::EndCreate();
        if ( bAccepted == false && _autoLinkPin != 0 )
            bAccepted = takeAutoLink( findPin, outFromPin, outToPin );
        return bAccepted;
    }

    bool EditorNodeGraph::takeAutoLink( const Delegate<bool( int32, EditorGraphPinInfo& )>& findPin, int32& outFromPin, int32& outToPin )
    {
        EditorGraphPinInfo sourceInfo{};
        const bool         bSourceKnown = findPin.isBound() && findPin( _autoLinkPin, sourceInfo );
        for ( const FramePin& pin : _listFramePin )
        {
            const bool bNewNode = VectorUtil::contains( _listAutoLinkBefore, pin._nodeID ) == false;
            if ( bSourceKnown == false || bNewNode == false )
                continue;
            EditorGraphPinInfo candidate{};
            const utf8*        pReason = nullptr;
            if ( findPin( pin._pinID, candidate ) == false || EditorNodeGraphRules::canConnect( _autoLinkPin, sourceInfo, pin._pinID, candidate, outFromPin, outToPin, pReason ) == false )
                continue;
            _autoLinkPin = 0;
            ++getEditorState<EditorNodeGraphDrawnState>()._autoLinkCount;
            return true;
        }
        // 새 노드가 아직 없거나(다음 프레임에 그린다) 맞는 핀이 없다 — 기다릴 프레임이 다하면 요청을 버린다.
        _autoLinkFramesLeft = _autoLinkFramesLeft > 0 ? _autoLinkFramesLeft - 1 : 0;
        if ( _autoLinkFramesLeft == 0 )
            _autoLinkPin = 0;
        return false;
    }

    Color EditorNodeGraph::getPinTypeColor( uint32 pinType ) const
    {
        if ( pinType < _listPinTypeColor.size() )
            return _listPinTypeColor[pinType];
        return EditorColorUtil::fromImVec4( ImGui::GetStyleColorVec4( ImGuiCol_Text ) );
    }

    void EditorNodeGraph::drawPinIcon( uint32 pinType, bool bConnected ) const
    {
        const float32 size   = ImGui::GetTextLineHeight();
        const ImVec2  corner = ImGui::GetCursorScreenPos();
        ImGui::Dummy( ImVec2{ size, size } );
        const ImVec2 center{ corner.x + size * 0.5f, corner.y + size * 0.5f };
        const ImU32  color     = EditorColorUtil::toImU32( getPinTypeColor( pinType ) );
        ImDrawList*  pDrawList = ImGui::GetWindowDrawList();
        if ( bConnected )
            pDrawList->AddCircleFilled( center, size * 0.3f, color );
        else
            pDrawList->AddCircle( center, size * 0.3f, color, 0, 1.5f );
    }

    void EditorNodeGraph::drawNodeIssues() const
    {
        const ImU32      color   = EditorColorUtil::toImU32( SW_THEME_COLOR( "Signal", "Error" ) );
        const ed::NodeId hovered = ed::GetHoveredNode();
        for ( const EditorGraphNodeIssue& issue : _listNodeIssue )
        {
            const ed::NodeId nodeID{ static_cast<uintptr_t>( issue._nodeID ) };
            ImDrawList*      pDrawList = ed::GetNodeBackgroundDrawList( nodeID );
            if ( pDrawList == nullptr )
                continue;
            const ImVec2 position = ed::GetNodePosition( nodeID );
            const ImVec2 size     = ed::GetNodeSize( nodeID );
            pDrawList->AddRect( ImVec2{ position.x - 2.0f, position.y - 2.0f }, ImVec2{ position.x + size.x + 2.0f, position.y + size.y + 2.0f }, color,
                                ed::GetStyle().NodeRounding, 0, 3.0f );
            if ( hovered == nodeID )
            {
                ed::Suspend();
                ImGui::SetTooltip( "%s", issue._message.c_str() );
                ed::Resume();
            }
        }
    }

    void EditorNodeGraph::recordDrawnNodeCount( uint32 nodeCount )
    {
        getEditorState<EditorNodeGraphDrawnState>()._nodeCount = nodeCount;
    }

    uint32 EditorNodeGraph::getDrawnNodeCount()
    {
        return getEditorState<EditorNodeGraphDrawnState>()._nodeCount;
    }

    uint32 EditorNodeGraph::getAutoLinkCount()
    {
        return getEditorState<EditorNodeGraphDrawnState>()._autoLinkCount;
    }

    uint32 EditorNodeGraph::getAlignedNodeCount()
    {
        return getEditorState<EditorNodeGraphDrawnState>()._alignedNodeCount;
    }

    bool EditorNodeGraph::isMinimapShown()
    {
        return getEditorState<EditorNodeGraphDrawnState>()._bMinimapShown;
    }

    bool EditorNodeGraph::bind() const
    {
        if ( _pEditor == nullptr )
            return false;

        ed::SetCurrentEditor( _pEditor );
        return true;
    }

    void EditorNodeGraph::unbind() const
    {
        ed::SetCurrentEditor( nullptr );
    }

    void EditorNodeGraph::applyContentFitIfNeeded()
    {
        if ( _bNeedsContentFit == false )
            return;
        if ( isCanvasSizeSettled( _previousCanvasSize, _canvasSize ) == false )
            return; // 크기가 정해진 다음 프레임에 맞춘다(노드 위치 시드도 그때까지 매 프레임 같은 값으로 걸린다)

        ed::NavigateToContent( 0.1f, 1.0f ); // 작은 그래프는 1:1 에서 가운데로 — 맞추기가 노드를 화면 가득 키우지 않는다(오버레이 포트 패치)
        _bNeedsContentFit = SW_FALSE;
    }

} // namespace sw::editor
