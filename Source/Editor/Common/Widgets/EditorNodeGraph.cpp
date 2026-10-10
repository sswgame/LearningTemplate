#include "pch.h"

#include "Editor/Common/Widgets/EditorNodeGraph.h"

#include "Core/Container/StringUtil.h"

#include "Editor/Common/EditorUtil.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    EditorNodeGraph::EditorNodeGraph()
        : _pEditor{ nullptr }
        , _settingsPath{}
        , _previousCanvasSize{}
        , _canvasSize{}
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
        ed::SetCurrentEditor( nullptr );
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
