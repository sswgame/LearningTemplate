#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Gui/EditorGraphDocumentPanel.h"

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
        /** @brief 대화 그래프 도구를 생성합니다. */
        DialogueGraphPanel();
        /** @brief 노드 에디터 컨텍스트를 해제합니다. */
        virtual ~DialogueGraphPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 수명주기 및 UI 렌더링
        // ------------------------------------------------------------------------------
        void shutdown( IRHIDevice* pRhiDevice ) override;
        /** @brief 대화 노드 그래프 UI를 렌더링합니다. */
        void               drawContent() override;
        [[nodiscard]] bool saveDocument() override;

    private:
        /** @brief 노드 추가·저장·줌 등 그래프 툴바를 그립니다. */
        void drawGraphToolbar();
        /** @brief 노드 그래프 캔버스를 그립니다. */
        void drawGraphCanvas( float32 canvasWidth );
        /** @brief 노드들을 캔버스에 그립니다. */
        void drawGraphNodes();
        /** @brief 노드 왼쪽의 입력 핀("-> In")을 그립니다(입력이 있는 노드 종류가 함께 씁니다). */
        void drawInputPin( int32 nodeID );
        /** @brief 출력 핀 하나를 그립니다. 핀 번호는 부르는 쪽이 인코딩합니다(다음 · 선택지 · 분기). */
        void drawOutputPin( int32 pinID, const utf8* pLabel );
        /** @brief 캔버스의 링크 생성·삭제 상호작용을 처리합니다. */
        void handleCanvasInteractions();
        /** @brief 선택된 노드의 상세 인스펙터를 그립니다. */
        void drawSelectedNodeInspector();

        using DialogueNode = NodeType;
        using DialogueLink = LinkType;

        // ------------------------------------------------------------------------------
        // 2) 내부 처리 함수 (공통 뼈대는 EditorGraphDocumentPanel 에 있다)
        // ------------------------------------------------------------------------------
        /** @brief 기본 샘플 노드들을 구성합니다. */
        void ensureDefaults() override;
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반이 결과로 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        /** @brief 대화 그래프를 JSON 파일로 저장합니다. */
        [[nodiscard]] bool saveGraphData();

        /** @brief 지정한 타입의 노드를 특성 표의 기본값으로 추가합니다. */
        void addNode( DialogueAssetNodeType type );
        /** @brief 노드 하나의 머리 · 핀 · 본문 요약을 그립니다. 모양은 특성 표(`kArrDialogueNodeInfo`)가 정합니다. */
        void drawNodeBody( const DialogueNode& node );
        /** @brief 선택한 노드의 편집 칸을 그립니다. 어떤 칸을 보일지는 특성 표가 정합니다. */
        void drawNodeFields( DialogueNode& node, const DialogueNodeInfo& info );
        /** @brief 미리보기 재생을 한 틱 진행합니다. 기다리지 않는 노드는 곧바로 지납니다. */
        void tickPreview( float32 deltaSeconds );
        /** @brief 미리보기를 다음 노드로 보냅니다. 다음 노드는 러너와 같은 `DialogueCursor::step` 이 정합니다. */
        void previewStep( const DialogueStepInput& input );
        /** @brief 미리보기를 그 노드로 옮깁니다. 대사 · 선택지 노드면 뷰포트에 보이고, 끝 노드 · 없는 노드면 멈춥니다. */
        void enterPreviewNode( const DialogueGraphAsset& asset, int32 nodeID );
        /** @brief 미리보기 툴바를 그립니다. */
        void drawPreviewToolbar();

    private:
        int32 _selectedNodeID;
        int32 _previewNodeID;
    };
} // namespace sw::editor
