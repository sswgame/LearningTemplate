/**
 * @file EditorGraphDocumentPanel.h
 * @brief 노드 그래프를 문서로 여닫는 패널의 공통 뼈대.
 *
 * [왜 있는가]
 * `AnimationGraphPanel` 과 `DialogueGraphPanel` 은 **같은 패널**이었다 — 노드·링크 목록을 들고,
 * 캔버스를 하나 소유하고, JSON 으로 오가고, 노드를 옮기면 dirty 를 찍는다. 그런데 그 뼈대가
 * 두 벌로 복사돼 있었고, 복사본이 **조용히 갈라졌다**:
 *
 *   - `cacheNodeLayout` 의 "움직였는가" 판단이 한쪽은 `||`, 다른 쪽은 `&&` 였다. `&&` 쪽에서는
 *     노드를 수평으로만 옮기면 dirty 가 찍히지 않아 **레이아웃이 저장되지 않았다.**
 *     (판단 자체는 `EditorSessionPolicy::hasNodeMoved` 로 올려 테스트가 지킨다.)
 *
 * 그래프 패널이 하나 더 생기면 또 한 벌이 늘어날 자리였다 — `EditorNodeGraphId.h` 가 id 변환을
 * 모은 것과 같은 이유로, 뼈대도 여기 모은다.
 *
 * [자산에 요구하는 것]
 * `AssetType` 은 `_listNode` · `_listLink` 를 갖고 `toJson()` · `parseJson()` 을 답할 수 있어야 한다.
 * 노드·링크 타입은 그 목록에서 **추론**하므로 자산이 따로 별칭을 노출할 필요는 없다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Gui/EditorDocumentPanel.h"
#include "Editor/Common/Widgets/EditorNodeGraph.h"
#include "Editor/Common/Widgets/EditorNodeGraphId.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"

namespace sw::editor
{
    /**
     * @class EditorGraphDocumentPanel
     * @brief 노드 그래프 문서 패널의 공통 상태와 절차입니다.
     * @tparam AssetType 이 패널이 읽고 쓰는 그래프 자산 (`AnimationGraphAsset` 등).
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
         * @details 두 문자열만 패널마다 다르다 — 절차는 같다.
         */
        EditorGraphDocumentPanel( EditorAssetKind kind, const utf8* pNodeMoveEditLabel, const utf8* pNodeMoveCoalesceKey )
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

        /** @brief 문서 텍스트 = 자산 JSON. */
        string captureDocumentText() const override { return captureGraphData().toJson(); }

        /**
         * @brief 텍스트 스냅샷을 그래프로 되돌립니다 (Undo·문서 전환).
         * @details 빈 텍스트는 "빈 그래프" 가 아니라 **기본 노드로 시작** 이다 — 되돌린 결과가 비면
         *          `ensureDefaults()` 가 채운다. 레이아웃 동기 깃발을 내려 캔버스가 다시 맞추게 한다.
         */
        void applyDocumentText( string_view text ) override
        {
            AssetType restored;
            if ( text.empty() == false )
                restored.parseJson( text );

            _listNode = std::move( restored._listNode );
            _listLink = std::move( restored._listLink );
            if ( _listNode.empty() )
                ensureDefaults();

            _bGraphLayoutReady = SW_FALSE;
            _nodeGraph.requestContentFit();
        }

        /**
         * @brief 캔버스의 노드 위치를 목록에 담고, 옮겨졌으면 dirty 로 찍습니다.
         * @details 캔버스가 진실이고 목록이 사본이다 — 사용자가 노드를 끌면 캔버스만 안다.
         *          첫 프레임의 "위치가 처음 정해지는 것" 은 편집이 아니므로 `_bGraphLayoutReady`
         *          가 서기 전에는 dirty 로 치지 않는다.
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

    protected:
        EditorNodeGraph        _nodeGraph;             /**< 캔버스 컨텍스트. 패널 하나가 하나를 소유한다. */
        NodeList               _listNode;              /**< 편집 중인 노드. 캔버스 위치는 cacheNodeLayout 이 담는다. */
        LinkList               _listLink;              /**< 편집 중인 링크. */
        const utf8*            _pNodeMoveEditLabel;    /**< 노드 이동 Undo 이름. */
        const utf8*            _pNodeMoveCoalesceKey;  /**< 노드 이동 합치기 키. */
        float32                _previewHoldSeconds;    /**< 미리보기 재생이 현재 노드에 머문 시간. */
        uint8                  _bGraphLayoutReady : 1; /**< 캔버스가 위치를 한 번 정한 뒤 선다. */
        uint8                  _bPreviewPlaying   : 1; /**< 미리보기 재생 중. */
        [[maybe_unused]] uint8 _reservedGraph     : 6;
    };
} // namespace sw::editor
