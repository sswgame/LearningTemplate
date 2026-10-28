/**
 * @file EditorGraphDocumentPanel.h
 * @brief 노드 그래프를 문서로 여닫는 패널의 공통 뼈대입니다.
 *
 * [왜 있는가]
 * `AnimationGraphPanel` 과 `DialogueGraphPanel` 은 같은 패널입니다. 둘 다 노드와 링크 목록을 보관하고, 캔버스를 하나
 * 소유하며, JSON 으로 읽고 쓰고, 노드를 옮기면 dirty 로 표시합니다. 이 뼈대를 패널마다 복사하면 복사본이 조용히
 * 달라집니다. "움직였는가" 판단의 `||` 와 `&&` 가 갈리면 수평 이동만 한 레이아웃이 저장되지 않는 식입니다.
 * 판단 자체는 `EditorSessionPolicy::hasNodeMoved` 로 올려 두고 테스트가 지킵니다. `EditorNodeGraphID.h` 가 id 변환을 모은 것과 같은
 * 이유로 뼈대도 여기에 모읍니다.
 *
 * [애셋에 요구하는 것]
 * `AssetType` 은 `_listNode` 와 `_listLink` 를 가지고 `toXML()` 과 `parseXML()` 을 제공해야 합니다. 노드와 링크 타입은
 * 그 목록에서 추론하므로 애셋이 따로 별칭을 노출할 필요는 없습니다.
 * 패널은 애셋을 **통째로** 보관하고 있습니다(`_graph`). 노드와 링크만 따로 들면 캔버스에 없는 필드(애니메이션 그래프의 레이어와 본 마스크)가
 * 읽기 → 저장 · Undo 를 한 번 지날 때마다 조용히 사라집니다.
 */
#pragma once
#include "Core/Container/VectorUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Common/Widgets/EditorNodeGraph.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"

namespace sw::editor
{
    /**
     * @class EditorGraphDocumentPanel
     * @brief 노드 그래프 문서 패널의 공통 상태와 절차를 담당합니다.
     * @tparam AssetType 이 패널이 읽고 쓰는 그래프 애셋입니다(`AnimationGraphAsset` 등).
     */
    template <typename AssetType>
    class EditorGraphDocumentPanel : public EditorDocumentPanel
    {
    protected:
        using NodeList = decltype( AssetType::_listNode );
        using LinkList = decltype( AssetType::_listLink );
        using NodeType = typename NodeList::value_type;
        using LinkType = typename LinkList::value_type;

        /**
         * @param kind 이 패널이 다루는 애셋 종류입니다.
         * @param pNodeMoveEditLabel 노드를 옮겼을 때 Undo 에 남길 이름입니다("Move Dialogue Nodes" 등).
         * @param pNodeMoveCoalesceKey 연속 이동을 한 편집으로 합칠 키입니다.
         * @details 패널마다 다른 것은 이 두 문자열뿐이고, 절차는 같다.
         */
        EditorGraphDocumentPanel( EditorAssetType kind, const utf8* pNodeMoveEditLabel, const utf8* pNodeMoveCoalesceKey )
            : EditorDocumentPanel{ kind, true }
            , _nodeGraph{}
            , _graph{}
            , _pNodeMoveEditLabel{ pNodeMoveEditLabel }
            , _pNodeMoveCoalesceKey{ pNodeMoveCoalesceKey }
            , _previewHoldSeconds{ 0.0f }
            , _pendingPlacementNodeID{ 0 }
            , _pendingPlacement{}
            , _bGraphLayoutReady{ SW_FALSE }
            , _bPreviewPlaying{ SW_FALSE }
            , _reservedGraph{ 0 }
        {
        }

        /** @brief 노드가 하나도 없을 때 기본 노드를 채웁니다. 패널마다 다르므로 반드시 구현해야 합니다. */
        virtual void ensureDefaults() = 0;

        /** @brief 다음에 쓸 노드 ID 입니다. */
        int32 nextNodeID() const { return nextItemID( _graph._listNode ); }
        /** @brief 다음에 쓸 링크 ID 입니다. */
        int32 nextLinkID() const { return nextItemID( _graph._listLink ); }

        /** @brief 편집 중인 애셋의 사본입니다. */
        AssetType captureGraphData() const { return _graph; }

        /**
         * @brief 캔버스를 열고 이 이미지의 imgui-node-editor 사본에도 현재 편집기를 바인딩합니다. false 이면 `endGraphCanvas` 를 호출하지 않습니다.
         * @details 이 헤더는 템플릿이므로 확장 모듈 안에서 컴파일된다. 확장은 imgui-node-editor 를 정적으로 따로 링크하므로, 프레임워크(EditorModule)가
         *          바인딩한 현재 편집기를 확장의 사본은 모른다. 바인딩하지 않으면 캔버스 안의 `ax::NodeEditor` 호출이 편집기 없음으로 멈춘다.
         */
        bool beginGraphCanvas( const utf8* pCanvasID, const utf8* pSettingsFileName )
        {
            if ( _nodeGraph.beginCanvas( pCanvasID, pSettingsFileName ) == false )
                return false;
            ax::NodeEditor::SetCurrentEditor( _nodeGraph.getContext() );
            return true;
        }

        /** @brief `beginGraphCanvas` 와 짝입니다. 그린 노드 수를 프로브(`Editor.GraphNodeCount`)에 기록합니다. */
        void endGraphCanvas()
        {
            EditorNodeGraph::noteDrawnNodeCount( static_cast<uint32>( _graph._listNode.size() ) );
            _nodeGraph.endCanvas();
            ax::NodeEditor::SetCurrentEditor( nullptr );
        }

        /** @brief 새 노드 @p nodeID 를 다음에 그릴 때 캔버스 좌표 @p canvasPosition 에 둡니다. 노드 검색 팝업에서 오른쪽 클릭한 위치입니다. */
        void placeNodeOnNextDraw( int32 nodeID, const float2& canvasPosition )
        {
            _pendingPlacementNodeID = nodeID;
            _pendingPlacement       = canvasPosition;
        }

        /** @brief 노드를 그린 직후(`ed::EndNode` 뒤)에 호출합니다. 놓을 위치가 예약된 노드이면 그 위치에 둡니다. */
        void applyNodePlacement( int32 nodeID )
        {
            if ( _pendingPlacementNodeID == 0 || _pendingPlacementNodeID != nodeID )
                return;
            ax::NodeEditor::SetNodePosition( toNodeID( nodeID ), ImVec2( _pendingPlacement._x, _pendingPlacement._y ) );
            _pendingPlacementNodeID = 0;
        }

        /** @brief 문서 텍스트는 애셋 XML 입니다. */
        string captureDocumentText() const override { return captureGraphData().toXML(); }

        /**
         * @brief 텍스트 스냅샷을 그래프로 되돌립니다. Undo 와 문서 전환에서 씁니다.
         * @details 빈 텍스트는 빈 그래프가 아니라 기본 노드로 시작한다는 뜻이다. 되돌린 결과가 비면
         *          `ensureDefaults()` 가 채운다. 레이아웃 동기화 플래그를 해제해 캔버스가 위치를 다시 맞추게 한다.
         */
        void applyDocumentText( string_view text ) override
        {
            AssetType restored;
            // 되돌리기 텍스트는 이 패널이 쓴 XML 이다. 읽지 못하면 결함이므로 알리고 기본 그래프로 둔다.
            if ( text.empty() == false && restored.parseXML( text ) == false )
                SW_LOG_WARNING( "Graph undo snapshot could not be read - showing the default graph" );

            _graph = std::move( restored );
            if ( _graph._listNode.empty() )
                ensureDefaults();

            _bGraphLayoutReady = SW_FALSE;
            _nodeGraph.requestContentFit();
        }

        /**
         * @brief 파일에서 읽은 그래프를 목록으로 받습니다. `loadDocument` 의 공통 절차입니다.
         * @details 읽지 못한 파일이면 이전 문서의 그래프를 보관하지 않고 기본 그래프를 보여 주며 저장을 막는다. 읽지 못한 파일을 덮어쓰지 않기 위해서다.
         *          레이아웃 동기화 플래그와 미리보기 재생을 끄고 캔버스가 내용에 맞추게 한다. 패널별 미리보기 상태는 패널이 정리한다.
         */
        void adoptLoadedGraph( AssetType&& data, ToolAssetLoadResult result )
        {
            _graph = result == ToolAssetLoadResult::Loaded ? std::move( data ) : AssetType{};
            if ( _graph._listNode.empty() )
                ensureDefaults();

            _bGraphLayoutReady = SW_FALSE;
            _bPreviewPlaying   = SW_FALSE;
            _nodeGraph.requestContentFit();
        }

        /**
         * @brief 캔버스의 노드 위치를 목록에 담고, 옮겨졌으면 dirty 로 표시합니다.
         * @details 원본은 캔버스이고 목록은 사본이다. 사용자가 노드를 끌면 캔버스만 알고 있다. 첫 프레임에 위치가 처음
         *          정해지는 것은 편집이 아니므로 `_bGraphLayoutReady` 가 켜지기 전에는 dirty 로 표시하지 않는다.
         */
        void cacheNodeLayout()
        {
            bool bMoved{ false };
            for ( NodeType& node : _graph._listNode )
            {
                const ImVec2 position = ax::NodeEditor::GetNodePosition( toNodeID( node._id ) );
                const bool   bChanged = EditorSessionPolicy::hasNodeMoved( node._position._x, node._position._y, position.x, position.y );
                if ( EditorSessionPolicy::shouldMarkDocumentDirtyOnNodeMove( _bGraphLayoutReady == SW_TRUE, bChanged ) )
                    bMoved = true;

                node._position._x = position.x;
                node._position._y = position.y;
            }

            if ( bMoved )
                notifyDocumentEdited( _pNodeMoveEditLabel, _pNodeMoveCoalesceKey );
            _bGraphLayoutReady = SW_TRUE;
        }

        /**
         * @brief 캔버스의 삭제 요청(링크, 노드)을 목록에 반영합니다. `ed::BeginDelete` 구간 전체를 처리합니다.
         * @details 패널마다 다른 것은 "이 링크가 이 노드에 닿는가" 의 판단과 Undo 이름뿐이다.
         *          애니메이션은 노드 id 로, 대화는 핀 번호를 풀어서 판단한다. 노드를 지우면 그 노드에 닿은 링크도
         *          함께 지운다. 링크를 남기면 저장된 그래프에 없는 노드를 가리키게 된다.
         * @param linkTouchesNode `( const LinkType&, int32 nodeID ) -> bool` 시그니처의 콜백입니다.
         * @param onNodeDeleted `( int32 nodeID ) -> void` 시그니처의 콜백입니다. 선택 해제 같은 패널별 뒷정리를 합니다.
         */
        template <typename LinkTouchesNodeFn, typename OnNodeDeletedFn>
        void processCanvasDeletions( LinkTouchesNodeFn&& linkTouchesNode, OnNodeDeletedFn&& onNodeDeleted, const utf8* pDeleteLinkLabel,
                                     const utf8* pDeleteNodeLabel )
        {
            if ( ax::NodeEditor::BeginDelete() == false )
                return;

            ax::NodeEditor::LinkId linkID;
            while ( ax::NodeEditor::QueryDeletedLink( &linkID ) )
            {
                if ( ax::NodeEditor::AcceptDeletedItem() == false )
                    continue;
                const int32 id = static_cast<int32>( linkID.Get() );
                VectorUtil::removeIf( _graph._listLink, [id]( const LinkType& link )
                { return link._id == id; } );
                notifyDocumentEdited( pDeleteLinkLabel );
            }

            ax::NodeEditor::NodeId nodeID;
            while ( ax::NodeEditor::QueryDeletedNode( &nodeID ) )
            {
                if ( ax::NodeEditor::AcceptDeletedItem() == false )
                    continue;
                const int32 id = static_cast<int32>( nodeID.Get() );
                VectorUtil::removeIf( _graph._listNode, [id]( const NodeType& node )
                { return node._id == id; } );
                VectorUtil::removeIf( _graph._listLink, [id, &linkTouchesNode]( const LinkType& link )
                { return linkTouchesNode( link, id ); } );
                onNodeDeleted( id );
                notifyDocumentEdited( pDeleteNodeLabel );
            }
            ax::NodeEditor::EndDelete();
        }

    protected:
        EditorNodeGraph        _nodeGraph;              /**< 캔버스 컨텍스트. 패널마다 하나씩 소유한다. */
        AssetType              _graph;                  /**< 편집 중인 애셋. 노드의 캔버스 위치는 cacheNodeLayout 이 담는다. */
        const utf8*            _pNodeMoveEditLabel;     /**< 노드 이동 Undo 이름. */
        const utf8*            _pNodeMoveCoalesceKey;   /**< 노드 이동을 합치는 키. */
        float32                _previewHoldSeconds;     /**< 미리보기 재생이 현재 노드에 머문 시간. */
        int32                  _pendingPlacementNodeID; /**< 다음 그리기에서 위치를 정할 새 노드. 0 이면 없다. */
        float2                 _pendingPlacement;       /**< 그 노드의 캔버스 좌표. */
        uint8                  _bGraphLayoutReady : 1;  /**< 캔버스가 위치를 한 번 정한 뒤에 켠다. */
        uint8                  _bPreviewPlaying   : 1;  /**< 미리보기를 재생 중이다. */
        [[maybe_unused]] uint8 _reservedGraph     : 6;
    };
} // namespace sw::editor
