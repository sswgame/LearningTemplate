#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/GUI/EditorGraphDocumentPanel.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

namespace sw
{
    struct DialogueStepInput;
} // namespace sw

namespace sw::editor
{
    /** @brief imgui-node-editor 로 만든 대화 · 퀘스트 노드 그래프 편집 패널입니다. */
    class DialogueGraphPanel : public EditorGraphDocumentPanel<DialogueGraphAsset>
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "dialogue_graph";

        /** @brief 대화 그래프 도구를 생성합니다. */
        DialogueGraphPanel();
        /** @brief 노드 에디터 컨텍스트를 해제합니다. */
        virtual ~DialogueGraphPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel: 수명 주기와 UI 렌더링
        // ------------------------------------------------------------------------------
        void shutdown( IRHIDevice* pRHIDevice ) override;
        /** @brief 대화 노드 그래프 UI 를 그립니다. */
        void               drawContent() override;
        [[nodiscard]] bool saveDocument() override;

    private:
        /** @brief 노드 추가, 저장, 줌 등 그래프 툴바를 그립니다. */
        void drawGraphToolbar();
        /** @brief 노드 그래프 캔버스를 그립니다. */
        void drawGraphCanvas( float32 canvasWidth );
        /** @brief 노드를 캔버스에 그립니다. */
        void drawGraphNodes();
        /** @brief 노드 왼쪽의 입력 핀("-> In")을 그립니다. 입력이 있는 노드 종류가 함께 씁니다. */
        void drawInputPin( int32 nodeID );
        /** @brief 출력 핀 하나를 그립니다. 핀 번호는 호출하는 쪽이 인코딩합니다. 다음, 선택지, 분기가 있습니다. */
        void drawOutputPin( int32 pinID, const utf8* pLabel );
        /** @brief 캔버스의 링크 생성과 삭제 상호작용을 처리합니다. */
        void handleCanvasInteractions();
        /** @brief 핀 번호의 연결 정보를 구합니다. 들어오는 핀과 흐름 타입 하나입니다. 이 그래프의 핀이 아니면 false 입니다. */
        bool findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const;
        /** @brief 핀에 링크가 닿아 있으면 true 입니다. 핀 원을 채울지 정할 때 씁니다. */
        bool isPinLinked( int32 pinID ) const;
        /** @brief 들어오는 링크가 없는 노드를 캔버스에 문제로 넘깁니다. 시작 노드는 제외합니다. */
        void updateNodeIssues();
        /** @brief 선택한 노드의 상세 인스펙터를 그립니다. */
        void drawSelectedNodeInspector();

        using DialogueNode = NodeType;
        using DialogueLink = LinkType;

        // ------------------------------------------------------------------------------
        // 2) 내부 처리 함수. 공통 뼈대는 EditorGraphDocumentPanel 에 있다
        // ------------------------------------------------------------------------------
        /** @brief 기본 샘플 노드를 구성합니다. */
        void ensureDefaults() override;
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반 클래스가 결과를 보고 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        /** @brief 대화 그래프를 JSON 파일로 저장합니다. */
        [[nodiscard]] bool saveGraphData();

        /** @brief 지정한 타입의 노드를 특성 테이블의 기본값으로 추가하고 그 id 를 반환합니다. */
        int32 addNode( DialogueAssetNodeType type );
        /** @brief 노드 하나의 헤더, 핀, 본문 요약을 그립니다. 모양은 특성 테이블(`kArrDialogueNodeInfo`)이 정합니다. */
        void drawNodeBody( const DialogueNode& node );
        /** @brief 선택한 노드의 편집 항목을 그립니다. 어떤 항목을 보일지는 특성 테이블이 정합니다. */
        void drawNodeFields( DialogueNode& node, const DialogueNodeInfo& info );
        /** @brief 미리보기 재생을 한 틱 진행합니다. 기다리지 않는 노드는 곧바로 지납니다. */
        void updatePreview( float32 deltaSeconds );
        /** @brief 미리보기를 다음 노드로 보냅니다. 다음 노드는 러너와 같은 `DialogueCursor::step` 이 정합니다. */
        void previewStep( const DialogueStepInput& input );
        /** @brief 미리보기를 그 노드로 옮깁니다. 대사나 선택지 노드이면 뷰포트에 보이고, 끝 노드나 없는 노드이면 멈춥니다. */
        void enterPreviewNode( const DialogueGraphAsset& asset, int32 nodeID );
        /** @brief 미리보기 툴바를 그립니다. */
        void drawPreviewToolbar();

    private:
        int32 _selectedNodeID;
        int32 _previewNodeID;
    };
} // namespace sw::editor
