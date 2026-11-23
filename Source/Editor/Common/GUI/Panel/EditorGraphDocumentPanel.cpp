#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorGraphDocumentPanel.h"

#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    EditorGraphDocumentPanelBase::EditorGraphDocumentPanelBase( const EditorAssetType& kind, const utf8* pNodeMoveEditLabel, NodeEditorBindFunction bindModuleCopy )
        : EditorDocumentPanel{ kind, true }
        , _graphEditSession{ getDocumentEditRecorder() }
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

    void EditorGraphDocumentPanelBase::placeNodeOnNextDraw( int32 nodeID, const float2& canvasPosition )
    {
        // 같은 노드를 다시 예약하면 자리만 바꾼다 — 예약이 둘이면 두 프레임에 걸쳐 두 번 옮겨진다.
        for ( PendingPlacement& placement : _listPendingPlacement )
        {
            if ( placement._nodeID != nodeID )
                continue;
            placement._position = canvasPosition;
            return;
        }
        _listPendingPlacement.push_back( PendingPlacement{ nodeID, canvasPosition } );
    }

    void EditorGraphDocumentPanelBase::applyNodePlacement( int32 nodeID )
    {
        for ( size_t index = 0; index < _listPendingPlacement.size(); ++index )
        {
            const PendingPlacement placement = _listPendingPlacement[index];
            if ( placement._nodeID != nodeID )
                continue;
            ed::SetNodePosition( EditorNodeGraphID::toNodeID( nodeID ), ImVec2( placement._position._x, placement._position._y ) );
            ed::SelectNode( EditorNodeGraphID::toNodeID( nodeID ), true ); // 새로 넣거나 붙인 노드를 고른다(언리얼 붙여 넣기 · 유니티 복제처럼 바로 옮길 수 있게)
            _listPendingPlacement.erase( _listPendingPlacement.begin() + static_cast<std::ptrdiff_t>( index ) );
            return;
        }
    }

    void EditorGraphDocumentPanelBase::recordGraphEdit( string_view label ) const
    {
        getDocumentEditRecorder().notifyEdited( label );
    }
} // namespace sw::editor
