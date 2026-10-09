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

    bool EditorNodeGraph::beginCanvas( const utf8* pCanvasId, const utf8* pSettingsFileName )
    {
        ensureContext( pSettingsFileName );
        if ( _pEditor == nullptr )
            return false;

        // ed::Begin 이 캔버스 크기로 쓰는 값과 같다(남은 자리 전부).
        const ImVec2 available = ImGui::GetContentRegionAvail();
        _previousCanvasSize    = _canvasSize;
        _canvasSize            = float2{ available.x, available.y };

        ed::SetCurrentEditor( _pEditor );
        ed::Begin( pCanvasId );
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
