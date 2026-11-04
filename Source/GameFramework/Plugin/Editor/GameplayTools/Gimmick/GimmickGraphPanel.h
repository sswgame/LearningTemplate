/**
 * @file GimmickGraphPanel.h
 * @brief 기믹 회로 그래프 편집 창입니다. 선택한 오브젝트의 `GimmickCircuitComponent` 를 노드 그래프로 보이고 고칩니다 — 노드 더하기 · 지우기, 배선 잇기, 매개변수, 대상 오브젝트 고르기, 검증 문제 표시.
 * @details 언리얼 블루프린트 그래프와 유니티 Visual Scripting 그래프의 자리이고, 소스 엔진의 엔티티 입출력 편집에 가장 가깝다. 노드 그래프 캔버스(`EditorNodeGraph`)와 판단(`EditorNodeGraphRules`)은 에디터의 것을 쓰고,
 *          문서는 파일이 아니라 씬에 저장되는 컴포넌트다 — 그래서 파일 문서 틀(`EditorGraphDocumentPanel`)이 아니라 편집마다 오브젝트 상태를 찍어 Undo 에 올린다(`EditorTransaction::recordModify`, 인스펙터와 같은 길).
 *          노드 자리와 주석 상자도 컴포넌트에 실린다(언리얼 블루프린트가 노드 좌표 · Comment 를 애셋에 두는 것과 같다). 끌기 · 늘이기 · 자동 배치는 마우스를 놓을 때 한 편집으로 남는다.
 *          컴포넌트가 바깥에서 바뀌면(되돌리기, 인스펙터) 저장 목록의 서명이 달라지는 것으로 알아보고 모델을 다시 읽는다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Common/Widgets/EditorNodeGraph.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/GimmickGraphModel.h"

namespace sw
{
    class GameObject;
    class GimmickCircuitComponent;
} // namespace sw

namespace sw::editor
{
    /**
     * @class GimmickGraphPanel
     * @brief 선택한 오브젝트의 기믹 회로를 그래프로 편집하는 창입니다.
     */
    class GimmickGraphPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 열기(`EditorPanelManager::setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "gimmick_graph";
        /** @brief 창 제목입니다(ImGui 창 이름 — 자체 테스트가 창을 찾을 때 이 상수를 쓴다). */
        static constexpr const utf8* kPanelTitle = "Gimmick Graph";

        GimmickGraphPanel();

        const utf8*      getPanelTitle() const override { return kPanelTitle; }
        bool             isToolPanel() const override { return true; }
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 147.0f, 37.0f ); }
        void             drawContent() override;
        void             shutdown() override;

    private:
        /** @brief 폼에서 고른 포트 한쪽입니다. */
        struct PortChoice
        {
            string _label{};
            string _port{};
            int32  _nodeGraphID{ 0 };
        };

        GimmickCircuitComponent* findCircuit();
        void                     reload( GimmickCircuitComponent& circuit );
        void                     refreshIssues();
        /** @brief 캔버스의 노드 자리를 모델에 싣고, 커밋하지 않은 배치(노드 자리 · 주석)가 있으면 마우스를 놓았을 때 한 편집으로 남깁니다. */
        void commitLayoutWhenReleased( GimmickCircuitComponent& circuit );
        /** @brief 주석 상자를 그리고(노드보다 먼저) 이름 바꾸기 · 보기 막대의 새 상자 요청을 편집으로 남깁니다. */
        void drawComments( GimmickCircuitComponent& circuit );

        // 그리기
        void               drawToolbar( GimmickCircuitComponent& circuit );
        void               drawCanvasArea( GimmickCircuitComponent& circuit );
        void               drawNode( const GimmickGraphNode& node );
        void               drawSidePanel( GimmickCircuitComponent& circuit );
        void               drawNodeInspector( GimmickCircuitComponent& circuit );
        void               drawWireSection( GimmickCircuitComponent& circuit );
        void               drawIssueSection( GimmickCircuitComponent& circuit );
        bool               drawCommittedText( const utf8* pID, const string& current, string& outText, const utf8* pHint = nullptr );
        void               handleCanvasInteractions( GimmickCircuitComponent& circuit );
        [[nodiscard]] bool findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const;
        bool               isPinLinked( int32 pinID ) const;

        // 편집 — 모델을 고치고 컴포넌트에 쓰고 Undo 에 올린다
        ObjectSnapshot beginEdit( GimmickCircuitComponent& circuit ) const;
        void           commitEdit( GimmickCircuitComponent& circuit, const ObjectSnapshot& before, const utf8* pLabel );
        void           editAddNode( GimmickCircuitComponent& circuit, const hashed_string& kind, const float2* pPosition );
        void           editRemove( GimmickCircuitComponent& circuit, const vector<int32>& listNode, const vector<int32>& listWire );
        /** @brief 노드를 비켜 복제합니다(Ctrl+D · 붙여 넣기). 한 편집으로 남깁니다. */
        void editDuplicate( GimmickCircuitComponent& circuit, const vector<int32>& listNode, const float2& offset );
        /** @brief 캔버스 단축키 Ctrl+C · X · V · D 를 받습니다. 복사 버퍼에는 노드 번호를 둔다 — 붙여 넣기는 같은 회로의 그 노드를 복제한다. */
        void handleEditCommands( GimmickCircuitComponent& circuit );
        void editRemoveOrphanWires( GimmickCircuitComponent& circuit );
        void editConnect( GimmickCircuitComponent& circuit, int32 fromNode, string_view fromPort, int32 toNode, string_view toPort );
        void editRename( GimmickCircuitComponent& circuit, int32 nodeID, const string& newID );
        void editSetParam( GimmickCircuitComponent& circuit, int32 nodeID, string_view name, string_view value );
        void editSetTarget( GimmickCircuitComponent& circuit, int32 nodeID, GameObjectHandle target );
        void editSetInvert( GimmickCircuitComponent& circuit, int32 wireID, bool bInvert );

    private:
        GimmickGraphModel         _model;
        EditorNodeGraph           _nodeGraph;
        vector<GimmickGraphIssue> _listIssue;
        GameObjectHandle          _ownerHandle;
        PortChoice                _formFrom;
        PortChoice                _formTo;
        string                    _editText;
        string                    _statusText;
        uint64                    _signature;
        uint32                    _editID;
        int32                     _selectedNode;
        uint8                     _bSeedComments : 1; ///< 다음 그리기에서 주석 상자 자리를 데이터에서 캔버스로 넣는다(다시 읽은 뒤)
    };
} // namespace sw::editor
