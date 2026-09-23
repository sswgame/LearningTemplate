#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Gui/EditorGraphDocumentPanel.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

namespace sw::editor
{
    /** @brief imgui-node-editor 기반 비주얼 대화/퀘스트 노드 그래프 에디터 */
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
        void drawContent() override;
        bool saveDocument() override;

    private:
        /** @brief 노드 추가·저장·줌 등 그래프 툴바를 그립니다. */
        void drawGraphToolbar();
        /** @brief 노드 그래프 캔버스를 그립니다. */
        void drawGraphCanvas( float32 canvasWidth );
        /** @brief 노드들을 캔버스에 그립니다. */
        void drawGraphNodes();
        /** @brief 노드 왼쪽의 입력 핀("-> In"). 여섯 종류 중 다섯이 같은 세 줄이었다. */
        void drawInputPin( int32 nodeId );
        /** @brief 출력 핀 하나 — 핀 번호는 호출자가 인코딩한다(다음 · 선택지 · 분기). */
        void drawOutputPin( int32 pinId, const utf8* pLabel );
        /** @brief 캔버스의 링크 생성·삭제 상호작용을 처리합니다. */
        void handleCanvasInteractions();
        /** @brief 선택된 노드의 상세 인스펙터를 그립니다. */
        void drawSelectedNodeInspector();

        using DialogueNode = NodeType;
        using DialogueLink = LinkType;

        // ------------------------------------------------------------------------------
        // 2) 내부 처리 함수 — 공통 뼈대는 EditorGraphDocumentPanel 이 든다.
        // ------------------------------------------------------------------------------
        /** @brief 기본 샘플 노드들을 구성합니다. */
        void ensureDefaults() override;
        /** @brief 대화 그래프 JSON 파일을 불러옵니다. */
        void loadGraphData();
        /** @brief 대화 그래프를 JSON 파일로 저장합니다. */
        bool saveGraphData();

        /** @brief 지정한 타입의 노드를 추가합니다. */
        void addNode( DialogueAssetNodeType type, const utf8* pSpeaker = "", const utf8* pText = "" );
        /** @brief 미리보기 재생을 한 틱 진행합니다. */
        void tickPreview( float32 deltaSeconds );
        /** @brief 미리보기를 다음 노드로 보냅니다. */
        void previewAdvance( int32 pinOffset = 2 );
        /** @brief 미리보기 툴바를 그립니다. */
        void drawPreviewToolbar();

    private:
        int32 _selectedNodeId;
        int32 _previewNodeId;
    };
} // namespace sw::editor
