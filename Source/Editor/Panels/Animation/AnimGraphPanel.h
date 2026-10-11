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
        // 1) IEditorPanel: 제목과 그리기
        // ------------------------------------------------------------------------------
        void shutdown( IRHIDevice* pRHIDevice ) override;
        /** @brief 애니메이션 그래프 UI 를 그립니다. */
        void drawContent() override;
        /** @brief 노드 추가, 저장, 줌 툴바를 그립니다. */
        void drawAnimationToolbar();
        /** @brief 노드 그래프 캔버스를 그립니다. */
        void               drawAnimationCanvas();
        [[nodiscard]] bool saveDocument() override;
        /** @brief 조건이 있는 전이 수를 반환합니다. 프로브 `Editor.AnimGraphConditionCount` 가 읽습니다. */
        uint32 countConditionLinks() const;
        /** @brief 전이 수를 반환합니다. 프로브 `Editor.AnimGraphLinkCount` 가 읽습니다. */
        uint32 getLinkCount() const { return static_cast<uint32>( _listLink.size() ); }

    private:
        using GraphNode = NodeType;
        using GraphLink = LinkType;

        // ------------------------------------------------------------------------------
        // 2) 그래프 조작과 JSON 로드, 저장. 공통 뼈대는 EditorGraphDocumentPanel 에 있다
        // ------------------------------------------------------------------------------
        /** @brief 기본 노드가 없으면 넣습니다. */
        void ensureDefaults() override;
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반 클래스가 결과를 보고 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        /** @brief 그래프 데이터를 저장합니다. */
        [[nodiscard]] bool saveGraphData();
        /** @brief 주어진 이름의 노드를 추가하고 그 id 를 반환합니다. */
        int32 addNamedNode( const utf8* pName );
        /** @brief 핀 번호의 연결 정보를 구합니다. 포즈 타입 하나입니다. 이 그래프의 핀이 아니면 false 입니다. */
        bool findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const;
        /** @brief 미리보기 플레이어에 현재 그래프를 넣습니다. */
        void syncPreviewGraph();
        /** @brief 미리보기 재생을 한 틱 진행합니다. */
        void tickPreview( float32 deltaSeconds );
        /** @brief 미리보기가 상태를 옮겼으면 그 전이 링크에 흐름이 보이도록 기록합니다. */
        void notePreviewTransition();
        /** @brief 오른쪽의 상태와 전이 편집 영역을 그립니다. 이름이 곧 클립이며, 반복, 전이 조건, 블렌드를 편집합니다. */
        void drawGraphInspector();
        /** @brief 캔버스의 노드와 링크 선택을 선택한 순서대로 따라갑니다. */
        void trackCanvasSelection();
        /** @brief 선택한 노드 둘을 먼저 선택한 것에서 나중에 선택한 것으로 연결합니다. */
        void linkSelectedNodes();
        /** @brief 노드를 옆에 복제합니다. */
        void duplicateNode( int32 nodeID );

    private:
        AnimGraphAsset  _previewGraph;        ///< 미리보기 플레이어가 빌려 쓰는 그래프 복사본이다
        AnimGraphPlayer _previewPlayer;       ///< 노드 이름만 넘긴다. 클립 없이 "끝나면 다음" 을 직접 진행한다
        string          _previewStateName;    ///< 지난번 미리보기 상태다. 전이 흐름을 표시하는 데 쓴다
        string          _nameEditBuffer;      ///< 상태 이름 입력란의 텍스트
        string          _parameterEditBuffer; ///< 조건 파라미터 입력란의 텍스트
        vector<int32>   _listSelectionOrder;  ///< 캔버스에서 선택한 노드를 선택한 순서대로 담는다
        int32           _selectedNodeID;
        int32           _selectedLinkID;
        int32           _canvasLinkID; ///< 지난 프레임에 캔버스에서 선택한 링크다. 바뀐 때만 따라간다
        int32           _flowLinkID;   ///< 다음 캔버스에서 흐름을 보여 줄 전이다. 0 이면 없다
    };
} // namespace sw::editor
