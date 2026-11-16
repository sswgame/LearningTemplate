#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/EditorGraphDocumentPanel.h"
#include "Editor/Panels/Dialogue/Model/DialogueGraphPreviewModel.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"

#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"

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
        void shutdown() override;
        /** @brief 대화 노드 그래프 UI 를 그립니다. */
        void               drawDocumentContent() override;
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
        [[nodiscard]] bool findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const;
        /** @brief 핀에 링크가 닿아 있으면 true 입니다. 핀 원을 채울지 정할 때 씁니다. */
        bool isPinLinked( int32 pinID ) const;
        /** @brief 들어오는 링크가 없는 노드를 캔버스에 문제로 넘깁니다. 시작 노드는 제외합니다. */
        void updateNodeIssues();
        /** @brief 선택한 노드의 상세 인스펙터를 그립니다. */
        void drawSelectedNodeInspector();
        /** @brief 그래프의 화자 목록(이름 고치기 · 더하기 · 빼기)을 그립니다. 노드는 화자를 id 로 가리키므로 이름을 바꿔도 줄이 끊기지 않는다. */
        void drawSpeakerList();

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
        int32 addNode( DialogueAssetNodeKind type );
        /** @brief 노드 하나의 헤더, 핀, 본문 요약을 그립니다. 모양은 특성 테이블(`kArrDialogueNodeInfo`)이 정합니다. */
        void drawNodeBody( const DialogueNode& node );
        /** @brief 선택한 노드의 편집 항목을 그립니다. 어떤 항목을 보일지는 특성 테이블이 정합니다. */
        void drawNodeProperties( DialogueNode& node, const DialogueNodeInfo& info );
        /** @brief Action 노드의 동작 고르기와 등록된 칸을 그립니다. 칸을 고치면 글로 되돌려 노드에 씁니다. */
        void drawActionProperties( DialogueNode& node );
        /** @brief 칸 그리드가 고친 값을 노드의 글로 되돌립니다. */
        void onActionFieldEdited( const PropertyInfo& prop );
        /** @brief 미리보기 툴바를 그립니다. */
        void drawPreviewToolbar();

    private:
        int32                     _selectedNodeID;
        DialogueGraphPreviewModel _preview;
        EditorPropertyGrid        _actionGrid;           ///< 고른 동작의 칸
        InstancedStruct           _actionProperties;     ///< 지금 그리는 노드의 칸 값
        string                    _actionPropertiesText; ///< `_actionProperties` 를 푼 글. 노드 글이 바뀌면 다시 푼다
        string                    _actionError;          ///< 글을 풀지 못한 까닭
        vector<string>            _listActionName;       ///< 고르기 목록
        const ActionEntry*        _pActionEntry;         ///< `_actionProperties` 의 등록 줄
        string*                   _pActionTargetText;    ///< 그리는 동안만 쓴다 — 칸을 고치면 이 글을 고친다
    };
} // namespace sw::editor
