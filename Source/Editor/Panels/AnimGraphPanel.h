#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/GUI/EditorGraphDocumentPanel.h"

#include "Engine/Animation/Graph/AnimGraphAsset.h"
#include "Engine/Animation/Graph/AnimGraphPlayer.h"

namespace sw::editor
{
    /** @brief imgui-node-editor 로 만든 애니메이션 그래프 편집 패널입니다. */
    class AnimGraphPanel : public EditorGraphDocumentPanel<AnimGraphAsset>
    {
    public:
        /** @brief 애니메이션 그래프 도구를 생성합니다. */
        AnimGraphPanel();
        /** @brief 노드 에디터 컨텍스트를 해제합니다. */
        virtual ~AnimGraphPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        void shutdown( IRHIDevice* pRHIDevice ) override;
        /** @brief 애니메이션 그래프 UI를 그립니다. */
        void drawContent() override;
        /** @brief 노드 추가·저장·줌 툴바를 그립니다. */
        void drawAnimationToolbar();
        /** @brief 노드 그래프 캔버스를 그립니다. */
        void               drawAnimationCanvas();
        [[nodiscard]] bool saveDocument() override;
        /** @brief 조건이 있는 전이 수입니다(탐침 `Editor.AnimGraphConditionCount`). */
        uint32 countConditionLinks() const;
        /** @brief 전이 수입니다(탐침 `Editor.AnimGraphLinkCount`). */
        uint32 getLinkCount() const { return static_cast<uint32>( _listLink.size() ); }

    private:
        using GraphNode = NodeType;
        using GraphLink = LinkType;

        // ------------------------------------------------------------------------------
        // 2) 그래프 조작 · JSON 로드/저장 (공통 뼈대는 EditorGraphDocumentPanel 에 있다)
        // ------------------------------------------------------------------------------
        /** @brief 기본 노드가 없으면 넣습니다. */
        void ensureDefaults() override;
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반이 결과로 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        /** @brief 그래프 데이터를 저장합니다. */
        [[nodiscard]] bool saveGraphData();
        /** @brief 주어진 이름의 노드를 추가하고 그 id 를 돌려줍니다. */
        int32 addNamedNode( const utf8* pName );
        /** @brief 핀 번호의 연결 정보입니다(포즈 타입 하나). 이 그래프의 핀이 아니면 false 입니다. */
        bool findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const;
        /** @brief 미리보기 플레이어에 현재 그래프를 넣습니다. */
        void syncPreviewGraph();
        /** @brief 미리보기 재생을 한 틱 진행합니다. */
        void tickPreview( float32 deltaSeconds );
        /** @brief 미리보기가 상태를 옮겼으면 그 전이 링크에 흐름을 보이게 적습니다. */
        void notePreviewTransition();
        /** @brief 오른쪽 상태 · 전이 편집(이름 = 클립, 반복, 전이 조건 · 블렌드)입니다. */
        void drawGraphInspector();
        /** @brief 캔버스의 노드 · 링크 선택을 고른 순서대로 따라갑니다. */
        void trackCanvasSelection();
        /** @brief 고른 노드 둘(먼저 고른 것 → 나중 것)을 잇습니다. */
        void linkSelectedNodes();
        /** @brief 노드를 옆에 복제합니다. */
        void duplicateNode( int32 nodeID );

    private:
        AnimGraphAsset  _previewGraph;        ///< 미리보기 플레이어가 빌려 쓰는 그래프 사본입니다
        AnimGraphPlayer _previewPlayer;       ///< 노드 이름만 넘깁니다 — 클립 없이 "끝나면 다음" 을 손으로 진행합니다
        string          _previewStateName;    ///< 지난번 미리보기 상태(전이 흐름 표시)
        string          _nameEditBuffer;      ///< 상태 이름 칸의 글
        string          _parameterEditBuffer; ///< 조건 파라미터 칸의 글
        vector<int32>   _listSelectionOrder;  ///< 캔버스에서 고른 노드(고른 순서)
        int32           _selectedNodeID;
        int32           _selectedLinkID;
        int32           _canvasLinkID; ///< 지난 프레임에 캔버스에서 고른 링크(바뀐 때만 따라간다)
        int32           _flowLinkID;   ///< 다음 캔버스에서 흐름을 보일 전이(0 이면 없음)
    };
} // namespace sw::editor
