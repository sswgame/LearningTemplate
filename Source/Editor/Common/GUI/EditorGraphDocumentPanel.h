/**
 * @file EditorGraphDocumentPanel.h
 * @brief 노드 그래프를 문서로 여닫는 패널의 공통 뼈대입니다.
 *
 * [왜 있는가]
 * `AnimGraphPanel` 과 `DialogueGraphPanel` 은 **같은 패널**입니다. 노드 · 링크 목록을 들고, 캔버스를 하나
 * 소유하고, JSON 으로 읽고 쓰고, 노드를 옮기면 dirty 로 표시합니다. 뼈대를 패널마다 복사하면 복사본이 **조용히
 * 갈라집니다**("움직였는가" 판단의 `||` · `&&` 가 갈리면 수평 이동만 한 레이아웃이 저장되지 않는 식 — 판단 자체는
 * `EditorSessionPolicy::hasNodeMoved` 로 올려 테스트가 지킵니다). `EditorNodeGraphId.h` 가 id 변환을 모은 것과 같은
 * 이유로 뼈대도 여기에 모읍니다.
 *
 * [애셋에 요구하는 것]
 * `AssetType` 은 `_listNode` · `_listLink` 를 갖고 `toJSON()` · `parseJSON()` 을 제공해야 합니다. 노드 · 링크 타입은
 * 그 목록에서 **추론**하므로 애셋이 따로 별칭을 노출할 필요는 없습니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/GUI/EditorDocumentPanel.h"
#include "Editor/Common/Widgets/EditorNodeGraph.h"
#include "Editor/Common/Widgets/EditorNodeGraphId.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"

namespace sw::editor
{
    /**
     * @class EditorGraphDocumentPanel
     * @brief 노드 그래프 문서 패널의 공통 상태와 절차입니다.
     * @tparam AssetType 이 패널이 읽고 쓰는 그래프 애셋 (`AnimGraphAsset` 등).
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
         * @param kind 이 패널이 다루는 애셋 종류.
         * @param pNodeMoveEditLabel 노드를 옮겼을 때 Undo 에 남길 이름 ("Move Dialogue Nodes" 등).
         * @param pNodeMoveCoalesceKey 연속 이동을 한 편집으로 합칠 키.
         * @details 패널마다 다른 것은 이 두 문자열뿐이고, 절차는 같습니다.
         */
        EditorGraphDocumentPanel( EditorAssetType kind, const utf8* pNodeMoveEditLabel, const utf8* pNodeMoveCoalesceKey )
            : EditorDocumentPanel{ kind, true }
            , _nodeGraph{}
            , _listNode{}
            , _listLink{}
            , _pNodeMoveEditLabel{ pNodeMoveEditLabel }
            , _pNodeMoveCoalesceKey{ pNodeMoveCoalesceKey }
            , _previewHoldSeconds{ 0.0f }
            , _bGraphLayoutReady{ SW_FALSE }
            , _bPreviewPlaying{ SW_FALSE }
            , _reservedGraph{ 0 }
        {
        }

        /** @brief 기본 노드가 하나도 없을 때 채워 넣습니다. 패널마다 다르므로 반드시 구현합니다. */
        virtual void ensureDefaults() = 0;

        /** @brief 다음에 쓸 노드 ID 입니다. */
        int32 nextNodeId() const { return nextItemId( _listNode ); }
        /** @brief 다음에 쓸 링크 ID 입니다. */
        int32 nextLinkId() const { return nextItemId( _listLink ); }

        /** @brief 지금 목록을 자산 한 벌로 만듭니다. */
        AssetType captureGraphData() const
        {
            AssetType data;
            data._listNode = _listNode;
            data._listLink = _listLink;
            return data;
        }

        /** @brief 문서 텍스트는 애셋 JSON 입니다. */
        string captureDocumentText() const override { return captureGraphData().toJSON(); }

        /**
         * @brief 텍스트 스냅샷을 그래프로 되돌립니다 (Undo·문서 전환).
         * @details 빈 텍스트는 "빈 그래프" 가 아니라 **기본 노드로 시작** 한다는 뜻입니다. 되돌린 결과가 비면
         *          `ensureDefaults()` 가 채웁니다. 레이아웃 동기화 플래그를 내려 캔버스가 위치를 다시 맞추게 합니다.
         */
        void applyDocumentText( string_view text ) override
        {
            AssetType restored;
            // 되돌리기 텍스트는 이 패널이 쓴 JSON 이다 — 못 읽으면 결함이라 알리고 기본 그래프로 둔다.
            if ( text.empty() == false && restored.parseJSON( text ) == false )
                SW_LOG_WARNING( "Graph undo snapshot could not be read - showing the default graph" );

            _listNode = std::move( restored._listNode );
            _listLink = std::move( restored._listLink );
            if ( _listNode.empty() )
                ensureDefaults();

            _bGraphLayoutReady = SW_FALSE;
            _nodeGraph.requestContentFit();
        }

        /**
         * @brief 파일에서 읽은 그래프를 목록으로 받습니다(`loadDocument` 의 공통 절차).
         * @details 읽지 못한 파일 앞에서는 앞 문서의 그래프를 들고 있지 않습니다 — 기본 그래프를 보이고 저장을 막습니다(덮지 않게).
         *          레이아웃 동기화 플래그와 미리보기 재생을 내리고 캔버스가 내용에 맞추게 합니다. 패널별 미리보기 상태는 패널이 정리합니다.
         */
        void adoptLoadedGraph( AssetType&& data, ToolAssetLoadResult result )
        {
            _listNode.clear();
            _listLink.clear();
            if ( result == ToolAssetLoadResult::Loaded )
            {
                _listNode = std::move( data._listNode );
                _listLink = std::move( data._listLink );
            }
            if ( _listNode.empty() )
                ensureDefaults();

            _bGraphLayoutReady = SW_FALSE;
            _bPreviewPlaying   = SW_FALSE;
            _nodeGraph.requestContentFit();
        }

        /**
         * @brief 캔버스의 노드 위치를 목록에 담고, 옮겨졌으면 dirty 로 표시합니다.
         * @details 정본은 캔버스이고 목록은 사본입니다. 사용자가 노드를 끌면 캔버스만 압니다. 첫 프레임에 "위치가 처음
         *          정해지는 것" 은 편집이 아니므로 `_bGraphLayoutReady` 가 켜지기 전에는 dirty 로 치지 않습니다.
         */
        void cacheNodeLayout()
        {
            bool bMoved{ false };
            for ( NodeType& node : _listNode )
            {
                const ImVec2 position = ax::NodeEditor::GetNodePosition( toNodeId( node._id ) );
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
         * @brief 캔버스의 삭제 요청(링크 · 노드)을 목록에 반영합니다(`ed::BeginDelete` 구간 전체).
         * @details 패널마다 다른 것은 "이 링크가 이 노드에 닿는가"
         *          (애니메이션은 노드 id, 대화는 핀 번호를 풀어 봅니다)와 Undo 이름뿐입니다. 노드를 지우면 그 노드에 닿은 링크도
         *          함께 지웁니다. 남기면 저장된 그래프가 없는 노드를 가리킵니다.
         * @param linkTouchesNode `( const LinkType&, int32 nodeId ) -> bool`
         * @param onNodeDeleted `( int32 nodeId ) -> void`. 선택 해제 같은 패널별 뒷정리
         */
        template <typename LinkTouchesNodeFn, typename OnNodeDeletedFn>
        void processCanvasDeletions( LinkTouchesNodeFn&& linkTouchesNode, OnNodeDeletedFn&& onNodeDeleted, const utf8* pDeleteLinkLabel,
                                     const utf8* pDeleteNodeLabel )
        {
            if ( ax::NodeEditor::BeginDelete() == false )
                return;

            ax::NodeEditor::LinkId linkId;
            while ( ax::NodeEditor::QueryDeletedLink( &linkId ) )
            {
                if ( ax::NodeEditor::AcceptDeletedItem() == false )
                    continue;
                const int32 id = static_cast<int32>( linkId.Get() );
                _listLink.erase( std::remove_if( _listLink.begin(), _listLink.end(), [id]( const LinkType& link )
                { return link._id == id; } ),
                                 _listLink.end() );
                notifyDocumentEdited( pDeleteLinkLabel );
            }

            ax::NodeEditor::NodeId nodeId;
            while ( ax::NodeEditor::QueryDeletedNode( &nodeId ) )
            {
                if ( ax::NodeEditor::AcceptDeletedItem() == false )
                    continue;
                const int32 id = static_cast<int32>( nodeId.Get() );
                _listNode.erase( std::remove_if( _listNode.begin(), _listNode.end(), [id]( const NodeType& node )
                { return node._id == id; } ),
                                 _listNode.end() );
                _listLink.erase( std::remove_if( _listLink.begin(), _listLink.end(),
                                                 [id, &linkTouchesNode]( const LinkType& link )
                { return linkTouchesNode( link, id ); } ),
                                 _listLink.end() );
                onNodeDeleted( id );
                notifyDocumentEdited( pDeleteNodeLabel );
            }
            ax::NodeEditor::EndDelete();
        }

    protected:
        EditorNodeGraph        _nodeGraph;             /**< 캔버스 컨텍스트. 패널마다 하나씩 소유합니다. */
        NodeList               _listNode;              /**< 편집 중인 노드. 캔버스 위치는 cacheNodeLayout 이 담습니다. */
        LinkList               _listLink;              /**< 편집 중인 링크. */
        const utf8*            _pNodeMoveEditLabel;    /**< 노드 이동 Undo 이름. */
        const utf8*            _pNodeMoveCoalesceKey;  /**< 노드 이동 합치기 키. */
        float32                _previewHoldSeconds;    /**< 미리보기 재생이 현재 노드에 머문 시간. */
        uint8                  _bGraphLayoutReady : 1; /**< 캔버스가 위치를 한 번 정한 뒤에 켜집니다. */
        uint8                  _bPreviewPlaying   : 1; /**< 미리보기 재생 중. */
        [[maybe_unused]] uint8 _reservedGraph     : 6;
    };
} // namespace sw::editor
