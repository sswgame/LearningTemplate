#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorGraphDocumentPanel.h"

#include "Editor/Common/Widgets/EditorNodeGraphID.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    EditorGraphDocumentPanelBase::EditorGraphDocumentPanelBase( const EditorAssetType& kind, const utf8* pNodeMoveEditLabel, NodeEditorBindFunction bindModuleCopy )
        : EditorDocumentPanel{ kind, true }
        , _nodeGraph{}
        , _bindModuleCopy{ bindModuleCopy }
        , _pNodeMoveEditLabel{ pNodeMoveEditLabel }
        , _listPendingPlacement{}
        , _bGraphLayoutReady{ SW_FALSE }
        , _reservedGraph{ 0 }
    {
        SW_ASSERT( _bindModuleCopy != nullptr );
    }

    bool EditorGraphDocumentPanelBase::beginGraphCanvas( const utf8* pCanvasID, const utf8* pSettingsFileName, const utf8* pMarkScope )
    {
        if ( _nodeGraph.beginCanvas( pCanvasID, pSettingsFileName, pMarkScope ) == false )
            return false;
        // `beginCanvas` 는 EditorModule 의 사본에 편집기를 걸었다. 패널 모듈의 사본에도 같은 편집기를 건다.
        _bindModuleCopy( _nodeGraph.getContext() );
        return true;
    }

    void EditorGraphDocumentPanelBase::finishGraphCanvas( uint32 drawnNodeCount )
    {
        _nodeGraph.finishCanvas( drawnNodeCount );
        _bindModuleCopy( nullptr );
    }

    void EditorGraphDocumentPanelBase::applyNodePlacement( int32 nodeID )
    {
        for ( size_t index = 0; index < _listPendingPlacement.size(); ++index )
        {
            const PendingPlacement placement = _listPendingPlacement[index];
            if ( placement._nodeID != nodeID )
                continue;
            ed::SetNodePosition( toNodeID( nodeID ), ImVec2( placement._position._x, placement._position._y ) );
            ed::SelectNode( toNodeID( nodeID ), true ); // 새로 넣거나 붙인 노드를 고른다(언리얼 붙여 넣기 · 유니티 복제처럼 바로 옮길 수 있게)
            _listPendingPlacement.erase( _listPendingPlacement.begin() + static_cast<std::ptrdiff_t>( index ) );
            return;
        }
    }

    float2 EditorGraphDocumentPanelBase::getCanvasNodePosition( int32 nodeID ) const
    {
        const ImVec2 position = ed::GetNodePosition( toNodeID( nodeID ) );
        return float2{ position.x, position.y };
    }
} // namespace sw::editor
