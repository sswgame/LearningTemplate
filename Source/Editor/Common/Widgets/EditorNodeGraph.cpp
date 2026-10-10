#include "pch.h"

#include "Editor/Common/Widgets/EditorNodeGraph.h"

#include "Core/Container/StringUtil.h"

#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    namespace
    {
        struct EditorNodeGraphInternal
        {
            static ImVec4 toImVec4( const Color4& color ) { return ImVec4{ color._r, color._g, color._b, color._a }; }

            /** @brief 가장 최근에 그린 그래프의 노드 수 — 탐침이 읽는다(그래프 패널은 RTTI 없이 찾을 수 없다). */
            static uint32& getDrawnNodeCountSlot()
            {
                static uint32 s_nodeCount{ 0 };
                return s_nodeCount;
            }
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
        , _bNeedsContentFit{ true }
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
            const string settingsPath = EditorUtil::resolveEditorStateFile( pSettingsFileName );
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

    bool EditorNodeGraph::beginCanvas( const utf8* pCanvasID, const utf8* pSettingsFileName )
    {
        ensureContext( pSettingsFileName );
        if ( _pEditor == nullptr )
            return false;

        // ed::Begin 이 캔버스 크기로 쓰는 값과 같다(남은 자리 전부).
        const ImVec2 available = ImGui::GetContentRegionAvail();
        _previousCanvasSize    = _canvasSize;
        _canvasSize            = float2{ available.x, available.y };
        if ( isCanvasRegionUsable( _canvasSize ) == false )
            return false;

        ed::SetCurrentEditor( _pEditor );

        // 캔버스가 창의 첫 그리기면 노드 편집기가 자기 클립 사각형을 창의 빈 첫 그리기 명령에 덮어쓰고, 그 명령은 화면 좌표로 되돌리지 않는다
        // (imgui_canvas 의 EnterLocalSpace 는 마지막 명령이 비어 있지 않을 때만 따로 명령을 연다). 확대 · 축소가 1 이 아니면 배경과 노드가
        // 패널 일부에서 잘린다(패널 점검 D21). 같은 색의 배경을 먼저 그려 마지막 명령을 채운다 — 노드 편집기가 그 위를 다시 칠하므로 보이는 차이는 없다.
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled( origin, ImVec2{ origin.x + available.x, origin.y + available.y },
                                                   ImGui::ColorConvertFloat4ToU32( ed::GetStyle().Colors[ed::StyleColor_Bg] ) );

        ed::Begin( pCanvasID );
        return true;
    }

    void EditorNodeGraph::endCanvas()
    {
        ed::End();
        EditorSelfTestMarks::note( "graph.canvas" ); // 마지막 항목은 캔버스 자식 창이다
        ed::SetCurrentEditor( nullptr );
    }

    bool EditorNodeGraph::drawAddNodePopup( const vector<EditorGraphNodeKind>& listKind, uint32& outKindID, float2& outCanvasPosition )
    {
        // 캔버스 안의 마우스 자리는 캔버스 좌표다 — 팝업을 그리려고 멈추기(Suspend) 전에 잡는다.
        const ImVec2 canvasMouse = ImGui::GetMousePos();
        ed::Suspend();
        if ( ed::ShowBackgroundContextMenu() )
        {
            _addNodePosition = float2{ canvasMouse.x, canvasMouse.y };
            _addNodeFilter.clear();
            ImGui::OpenPopup( "AddGraphNode" );
        }
        bool bPicked = false;
        if ( ImGui::BeginPopup( "AddGraphNode" ) )
        {
            if ( ImGui::IsWindowAppearing() )
                ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth( 220.0f * EditorThemeUtil::getDpiScale() );
            const bool bEnter = ImGui::InputTextWithHint( "##addNodeSearch", "Search nodes...", _addNodeFilter.data(), _addNodeFilter.capacity(),
                                                          ImGuiInputTextFlags_EnterReturnsTrue );
            EditorSelfTestMarks::note( "graph.addNode.search" );
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
                ImGui::TextDisabled( "No node matches." );
            // Enter 는 맨 위 줄을 고른다(언리얼 · 유니티 검색 팝업과 같다).
            if ( bEnter && bPicked == false && listIndex.empty() == false )
            {
                outKindID = listKind[listIndex[0]]._kindID;
                bPicked   = true;
            }
            if ( bPicked )
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
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
            ed::PinId pinA;
            ed::PinId pinB;
            if ( ed::QueryNewLink( &pinA, &pinB ) && pinA.Get() != 0 && pinB.Get() != 0 )
            {
                const int32        idA = static_cast<int32>( pinA.Get() );
                const int32        idB = static_cast<int32>( pinB.Get() );
                EditorGraphPinInfo infoA{};
                EditorGraphPinInfo infoB{};
                const utf8*        pReason = "Unknown pin";
                const bool         bKnown  = findPin.isBound() && findPin( idA, infoA ) && findPin( idB, infoB );
                if ( bKnown && EditorNodeGraphRules::canConnect( idA, infoA, idB, infoB, outFromPin, outToPin, pReason ) )
                {
                    if ( ed::AcceptNewItem( EditorNodeGraphInternal::toImVec4( getPinTypeColor( infoA._type ) ), 2.0f ) )
                        bAccepted = true;
                }
                else
                {
                    ed::RejectNewItem( EditorNodeGraphInternal::toImVec4( style::kError ), 2.0f );
                    ed::Suspend();
                    ImGui::SetTooltip( "%s", pReason );
                    ed::Resume();
                }
            }
        }
        ed::EndCreate();
        return bAccepted;
    }

    Color4 EditorNodeGraph::getPinTypeColor( uint32 pinType ) const
    {
        if ( pinType < _listPinTypeColor.size() )
            return _listPinTypeColor[pinType];
        const ImVec4 text = ImGui::GetStyleColorVec4( ImGuiCol_Text );
        return Color4{ text.x, text.y, text.z, text.w };
    }

    void EditorNodeGraph::drawPinIcon( uint32 pinType, bool bConnected ) const
    {
        const float32 size   = ImGui::GetTextLineHeight();
        const ImVec2  corner = ImGui::GetCursorScreenPos();
        ImGui::Dummy( ImVec2{ size, size } );
        const ImVec2 center{ corner.x + size * 0.5f, corner.y + size * 0.5f };
        const ImU32  color     = ImGui::ColorConvertFloat4ToU32( EditorNodeGraphInternal::toImVec4( getPinTypeColor( pinType ) ) );
        ImDrawList*  pDrawList = ImGui::GetWindowDrawList();
        if ( bConnected )
            pDrawList->AddCircleFilled( center, size * 0.3f, color );
        else
            pDrawList->AddCircle( center, size * 0.3f, color, 0, 1.5f );
    }

    void EditorNodeGraph::drawNodeIssues() const
    {
        const ImU32      color   = ImGui::ColorConvertFloat4ToU32( EditorNodeGraphInternal::toImVec4( style::kError ) );
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

    void EditorNodeGraph::noteDrawnNodeCount( uint32 nodeCount )
    {
        EditorNodeGraphInternal::getDrawnNodeCountSlot() = nodeCount;
    }

    uint32 EditorNodeGraph::getDrawnNodeCount()
    {
        return EditorNodeGraphInternal::getDrawnNodeCountSlot();
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

        ed::NavigateToContent( 0.1f );
        _bNeedsContentFit = false;
    }

} // namespace sw::editor
