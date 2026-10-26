/**
 * @file BehaviorTreePanel.h
 * @brief 행동 트리 편집 창입니다. 데이터 파일(`*.bt.xml`)을 노드 그래프로 열어 노드 더하기 · 지우기 · 순서 · 부모 바꾸기 · 속성 고치기를 하고 저장합니다.
 * @details 언리얼 Behavior Tree 에디터의 자리입니다. 노드 그래프 캔버스(`EditorNodeGraph`)와 판단(`EditorNodeGraphRules`)은 에디터의 것을 쓰고, 트리의 편집 상태는 화면 없는 모델
 *          (`BehaviorTreeEditModel`)이 가진다. 문서는 `EditorDocumentPanel` 의 Data 종류 가운데 접미 `.bt.xml` 만 받는다(같은 Data 종류의 VFX 미리보기가 `.vfx.xml` 만 받는 것과 같다).
 *          **노드 위치는 파일에 없다** - 편집할 때마다 트리 모양대로 자동 배치한다(루트가 왼쪽, 자식이 오른쪽, 형제 순서가 위에서 아래 = 우선순위).
 *          플레이 중에는 선택한 오브젝트의 `AIControllerComponent` 가 쥔 실행기(`BehaviorTreeRunner`)의 활성 노드를 같은 깊이 우선 번호로 맞춰 "running" 으로 표시한다(언리얼 BT 디버거에서 깜빡이는 노드).
 *          이 종류는 데이터 종류 레지스트리(`SW_RESOURCE_DATA_KIND`)에 올리지 않아 카탈로그 편집기(표)의 파일 목록에 없다 - 같은 파일을 두 편집기가 쥐지 않게 한다(`BehaviorTreeAsset::kDataKind`).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/EditorDocumentPanel.h"
#include "Editor/Common/Widgets/EditorNodeGraph.h"

#include "GameFramework/PlugIn/Editor/Base/AI/BehaviorTreeEditModel.h"

namespace sw
{
    class BehaviorTreeRunner;
} // namespace sw

namespace sw::editor
{
    /**
     * @class BehaviorTreePanel
     * @brief 행동 트리 데이터 파일 하나를 노드 그래프로 편집하는 창입니다.
     */
    class BehaviorTreePanel final : public EditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 열기(`EditorPanelManager::setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "behavior_tree";
        /** @brief 창 제목입니다(ImGui 창 이름 - 자체 테스트가 창을 찾을 때 이 상수를 쓴다). */
        static constexpr const utf8* kPanelTitle = "Behavior Tree";

        BehaviorTreePanel();

        const utf8*        getPanelTitle() const override { return kPanelTitle; }
        EditorTextExtent   getInitialPanelSize() const override { return EditorTextExtent::ofText( 147.0f, 37.0f ); }
        void               shutdown( IRHIDevice* pRHIDevice ) override;
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;

    protected:
        bool                acceptsDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        string              captureDocumentText() const override;
        void                applyDocumentText( string_view text ) override;

    private:
        /** @brief 자동 배치 결과 노드 하나의 캔버스 좌표입니다. */
        struct Placement
        {
            int32  _id{ 0 };
            float2 _position{};
        };

        // 그리기
        void drawToolbar();
        void drawCanvasArea();
        void drawNode( const BehaviorTreeEditNode& node, bool bActive );
        void drawSidePanel();
        void drawInspector( BehaviorTreeEditNode& node );
        void drawIssueList();
        void handleCanvasInteractions();
        bool findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const;

        // 편집
        int32                     addNodeFromSelection( BehaviorNodeType type );
        void                      markStructureChanged( const utf8* pLabel );
        void                      layoutTree();
        float32                   layoutNode( int32 id, uint32 depth, float32& inoutNextRow );
        const BehaviorTreeRunner* findLiveRunner() const;

        BehaviorTreeEditModel         _model;
        EditorNodeGraph               _nodeGraph;
        vector<BehaviorTreeEditIssue> _listIssue;
        vector<Placement>             _listPlacement;
        vector<int32>                 _listPreorder;
        vector<string>                _listTaskName;
        string                        _statusText;
        int32                         _selectedID;
        bool                          _bLayoutPending;
        bool                          _bSelectPending;
    };
} // namespace sw::editor
