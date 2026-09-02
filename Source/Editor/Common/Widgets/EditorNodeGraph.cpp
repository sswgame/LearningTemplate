#include "pch.h"

#include "Editor/Common/Widgets/EditorNodeGraph.h"

#include "Editor/Common/EditorUtil.h"

#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    EditorNodeGraph::EditorNodeGraph()
        : _pEditor{ nullptr }
        , _settingsPath{}
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
        if ( pSettingsFileName != nullptr && pSettingsFileName[0] != '\0' )
        {
            const string settingsPath = EditorUtil::resolveEditorConfigFile( pSettingsFileName );
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

        ed::NavigateToContent( 0.1f );
        _bNeedsContentFit = false;
    }

} // namespace sw::editor
