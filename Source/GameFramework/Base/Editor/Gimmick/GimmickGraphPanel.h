/**
 * @file GimmickGraphPanel.h
 * @brief 기믹 회로 그래프 편집 창입니다. 선택한 오브젝트의 `GimmickCircuitComponent` 를 노드 그래프로 보이고 고칩니다 — 노드 더하기 · 지우기, 배선 잇기, 매개변수, 대상 오브젝트 고르기, 검증 문제 표시.
 * @details 언리얼 블루프린트 그래프와 유니티 Visual Scripting 그래프의 자리이고, 소스 엔진의 엔티티 입출력 편집에 가장 가깝다. 노드 그래프 캔버스(`EditorNodeGraph`)와 판단(`EditorNodeGraphRules`)은 에디터의 것을 쓰고,
 *          문서는 파일이 아니라 씬에 저장되는 컴포넌트다 — 그래서 파일 문서 틀(`EditorGraphDocumentPanel`)이 아니라 편집마다 오브젝트 상태를 찍어 Undo 에 올린다(`EditorTransaction::recordModify`, 인스펙터와 같은 길).
 *          컴포넌트가 바깥에서 바뀌면(되돌리기, 인스펙터) 저장 목록의 서명이 달라지는 것으로 알아보고 모델을 다시 읽는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Widgets/EditorNodeGraph.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "GameFramework/Base/Editor/Gimmick/GimmickGraphModel.h"

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
        GimmickGraphPanel();

        const utf8* getPanelTitle() const override { return "Gimmick Graph"; }
        bool        isToolPanel() const override { return true; }
        float2      getInitialPanelSize() const override { return float2{ 1100.0f, 700.0f }; }
        void        drawContent() override;
        void        shutdown( IRHIDevice* pRHIDevice ) override;

    private:
        /** @brief 노드 위치를 이름으로 기억합니다. 다시 읽을 때(되돌리기, 다른 오브젝트에서 돌아옴) 같은 이름의 노드를 같은 자리에 둡니다. */
        struct SavedPosition
        {
            string _id{};
            float2 _position{};
        };

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
        void                     rememberPositions();

        // 그리기
        void drawToolbar( GimmickCircuitComponent& circuit );
        void drawCanvasArea( GimmickCircuitComponent& circuit );
        void drawNode( const GimmickGraphNode& node );
        void drawSidePanel( GimmickCircuitComponent& circuit );
        void drawNodeInspector( GimmickCircuitComponent& circuit );
        void drawWireSection( GimmickCircuitComponent& circuit );
        void drawIssueSection( GimmickCircuitComponent& circuit );
        bool drawCommittedText( const utf8* pID, const string& current, string& outText, const utf8* pHint = nullptr );
        void handleCanvasInteractions( GimmickCircuitComponent& circuit );
        bool findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const;
        bool isPinLinked( int32 pinID ) const;

        // 편집 — 모델을 고치고 컴포넌트에 쓰고 Undo 에 올린다
        ObjectSnapshot beginEdit( GimmickCircuitComponent& circuit ) const;
        void           commitEdit( GimmickCircuitComponent& circuit, const ObjectSnapshot& before, const utf8* pLabel );
        void           editAddNode( GimmickCircuitComponent& circuit, const hashed_string& kind, const float2* pPosition );
        void           editRemove( GimmickCircuitComponent& circuit, const vector<int32>& listNode, const vector<int32>& listWire );
        void           editRemoveOrphanWires( GimmickCircuitComponent& circuit );
        void           editConnect( GimmickCircuitComponent& circuit, int32 fromNode, string_view fromPort, int32 toNode, string_view toPort );
        void           editRename( GimmickCircuitComponent& circuit, int32 nodeID, const string& newID );
        void           editSetParam( GimmickCircuitComponent& circuit, int32 nodeID, string_view name, string_view value );
        void           editSetTarget( GimmickCircuitComponent& circuit, int32 nodeID, GameObjectHandle target );
        void           editSetInvert( GimmickCircuitComponent& circuit, int32 wireID, bool bInvert );

        GimmickGraphModel         _model;
        EditorNodeGraph           _nodeGraph;
        vector<GimmickGraphIssue> _listIssue;
        vector<SavedPosition>     _listSavedPosition;
        GameObjectHandle          _ownerHandle;
        PortChoice                _formFrom;
        PortChoice                _formTo;
        string                    _editText;
        string                    _statusText;
        uint64                    _signature;
        uint32                    _editID;
        int32                     _selectedNode;
    };
} // namespace sw::editor
