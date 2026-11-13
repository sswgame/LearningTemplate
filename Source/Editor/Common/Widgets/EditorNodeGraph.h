/**
 * @file EditorNodeGraph.h
 * @brief imgui-node-editor 컨텍스트의 수명 주기와 캔버스의 Begin/End 를 관리합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorColor.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Widgets/EditorNodeGraphRules.h"

#include "Engine/Asset/GraphCommentBox.h"

namespace ax::NodeEditor
{
    struct EditorContext;
} // namespace ax::NodeEditor

namespace sw::editor
{
    /** @brief 캔버스 단축키가 낸 편집 명령입니다(`EditorNodeGraph::queryEditCommand`). */
    enum class EditorGraphEditCommand : uint8
    {
        None = 0,
        Copy,      ///< Ctrl+C
        Cut,       ///< Ctrl+X
        Paste,     ///< Ctrl+V
        Duplicate, ///< Ctrl+D
    };

    /** @brief 캔버스가 링크 지우기 요청을 어떻게 받을지입니다. */
    enum class EditorGraphLinkRemoval : uint8
    {
        Accept, ///< 받아들인다.
        Reject, ///< 거절한다(행동 트리 — 링크를 끊으면 루트가 둘이 된다).
    };

    /** @brief 캔버스가 이번 프레임에 받아들인 지우기 요청입니다(`EditorNodeGraph::collectRemovals`). 묻는 동안 그래프를 고치지 않도록 모은 뒤 패널이 지웁니다. */
    struct EditorGraphRemoval
    {
        vector<int32> _listLinkID{};           ///< 지운 링크 id
        vector<int32> _listNodeID{};           ///< 지운 노드 id(주석 상자 빼고)
        vector<int32> _listCommentID{};        ///< 지운 주석 상자의 `GraphCommentBox::_id`(노드 id 가 아니다)
        uint32        _rejectedLinkCount{ 0 }; ///< 거절한 링크 수(`EditorGraphLinkRemoval::Reject`)

        [[nodiscard]] bool isEmpty() const { return _listLinkID.empty() && _listNodeID.empty() && _listCommentID.empty(); }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorNodeGraph
     * @brief 노드 그래프 캔버스를 호스팅합니다. 언리얼의 SGraphEditor, 유니티의 GraphView 에 해당하며 패널, 팝업, 섹션이 멤버로 소유합니다.
     * @details 이 클래스가 제공하는 기능은 다음과 같다. 빈 곳을 오른쪽 클릭하면 노드를 검색해 넣을 수 있고(`drawAddNodePopup`),
     *          핀 타입 색이 맞지 않는 연결을 거절하고(`queryNewLink`), 문제가 있는 노드에 빨간 테두리와 툴팁을 보여 준다(`setNodeIssues`, `drawNodeIssues`).
     *          판단은 ImGui 에 의존하지 않는 `EditorNodeGraphRules` 가 한다.
     *          확장 모듈은 imgui-node-editor 사본을 따로 가지므로(정적 링크) 캔버스 안에서 `ax::NodeEditor` 를 직접 호출하기 전에 자기 사본에도
     *          현재 편집기를 바인딩해야 한다. `EditorGraphDocumentPanel::beginGraphCanvas` 가 이 일을 한다.
     *          지우기 요청 모으기 · 주석 더하기와 지우기 · 프레임 끝처럼 모든 그래프 패널에 같은 일은 이 클래스가 맡고(`collectRemovals` · `addRequestedComment` · `removeComments` · `finishCanvas`), 패널은 노드 그리기 · 링크 열거 · 모델 반영만 답한다. 문서 틀이 다른 패널(미리보기 · 컴포넌트 편집)도 멤버로 쓰므로 상속이 아니라 포함으로 나눈다.
     */
    class SW_EDITOR_API EditorNodeGraph
    {
    public:
        /** @brief 주석 상자 노드 id 의 시작입니다. 패널의 노드 id 와 겹치지 않는 높은 범위다(주석 id 1 → 이 값 + 1). */
        static constexpr int32 kCommentNodeIDBase = 0x40000000;
        /** @brief 캔버스를 여는 최소 변 길이(픽셀)입니다. 노드 하나도 그릴 수 없는 크기에서는 열지 않습니다. */
        static constexpr float32 kMinCanvasExtent = 16.0f;
        /** @brief 복제가 원본에서 비켜 놓는 거리(캔버스 좌표)입니다. 노드 머리줄 하나쯤이라 원본과 사본이 함께 보인다(언리얼 Ctrl+W · 유니티 Ctrl+D 의 비켜 놓기). */
        static constexpr float32 kDuplicateOffset = 40.0f;
        EditorNodeGraph();
        ~EditorNodeGraph();
        EditorNodeGraph( const EditorNodeGraph& )            = delete;
        EditorNodeGraph& operator=( const EditorNodeGraph& ) = delete;

        /** @brief 노드 에디터 컨텍스트를 해제합니다. */
        void shutdown();

        /**
         * @brief 컨텍스트를 준비하고 캔버스를 엽니다. false 이면 endCanvas 를 호출하지 마세요.
         * @details 남은 영역이 거의 없으면 캔버스를 열지 않고 false 를 반환한다. 도킹 직후의 첫 프레임은 높이가 음수일 수 있다. 컨텍스트가 없어서 false 인지는
         *          `hasContext` 로 구분한다. 캔버스가 창의 첫 그리기여도 배경과 노드가 잘리지 않도록 같은 색의 배경을 먼저 그린다(구현 주석).
         * @param pMarkScope 레이블(`<scope>.canvas` · `<scope>.addNode.search`) 앞부분입니다. 패널마다 다르다 — 그래프 패널 둘이 나란히 보이면 같은 레이블이 두 자리에 남는다.
         */
        bool beginCanvas( const utf8* pCanvasID, const utf8* pSettingsFileName, const utf8* pMarkScope );
        /** @brief 노드 편집기 컨텍스트가 있는지 확인합니다. `beginCanvas` 가 false 일 때 실패인지, 아직 그릴 영역이 없는 것인지 구분하는 데 씁니다. */
        bool hasContext() const { return _pEditor != nullptr; }
        /**
         * @brief beginCanvas() 와 짝입니다. 캔버스를 닫기 전에 보기 요청(맞춤 · 정렬 · 미니맵 이동)을 반영하고, 닫은 뒤 캔버스 위에 보기 막대와 미니맵을 얹습니다.
         * @details 막대와 미니맵은 캔버스를 연 창의 자식 창이다 — 캔버스는 자기 자식 영역(`EditorSectionKind::Child`)에 두어야 한다(뒤에 오는 항목이 없어야 커서를 되돌리지 않아도 된다).
         */
        void endCanvas();
        /** @brief 캔버스 프레임의 끝을 한 번에 합니다: 보기 맞춤(`applyContentFitIfNeeded`) → 노드 수 기록(`recordDrawnNodeCount`) → `endCanvas`. */
        void finishCanvas( uint32 drawnNodeCount );

        /**
         * @brief 이번 프레임의 지우기 요청을 @p outDeletion 에 모읍니다(버퍼를 재사용하도록 비우고 채웁니다). 캔버스 안에서 프레임마다 부릅니다. 언리얼 `SGraphEditor` 의 삭제 명령이 스키마에 묻는 것과 같은 자리입니다(기억에 의존).
         * @details 받아들인 항목만 담고 주석 상자는 따로 가릅니다 — 패널은 제 모델에서 지우기만 합니다. 노드 편집기 호출이 이 모듈의 사본에서 돌아 확장 모듈도 그대로 부를 수 있다.
         */
        void collectRemovals( EditorGraphRemoval& outDeletion, EditorGraphLinkRemoval linkRemoval = EditorGraphLinkRemoval::Accept );
        /** @brief 노드 @p nodeID 를 지우라고 캔버스에 요청합니다. 지우기는 그 프레임의 `collectRemovals` 가 받습니다(잘라내기 · 메뉴의 Delete). */
        void requestNodeDeletion( int32 nodeID );
        /** @brief 고른 첫 노드의 id 입니다(주석 상자일 수 있다). 고른 노드가 없으면 false 입니다. */
        [[nodiscard]] bool findSelectedNode( int32& outNodeID ) const;
        /** @brief 상자 id 가 @p listCommentID 에 든 주석을 지웁니다(안의 노드는 남는다 — 언리얼 Comment 지우기와 같다). 지웠으면 true 입니다. */
        static bool removeComments( vector<GraphCommentBox>& listComment, const vector<int32>& listCommentID );
        /** @brief 보기 막대의 주석 단추가 낸 요청이면 새 상자를 @p listComment 에 더하고 true 입니다(id 는 가장 큰 id + 1). */
        bool addRequestedComment( vector<GraphCommentBox>& listComment );

        /** @brief 노드를 엽니다(`ed::BeginNode`). 캔버스가 이번 프레임의 노드와 그 핀을 기억합니다 — 핀을 끌어 만든 노드에 링크를 잇는 데 씁니다. */
        void beginNode( int32 nodeID );
        /** @brief beginNode() 와 짝입니다. */
        void endNode();
        /** @brief 핀을 엽니다(`ed::BeginPin`). 지금 연 노드의 핀으로 기억합니다. */
        void beginPin( int32 pinID, bool bInput );
        /** @brief beginPin() 와 짝입니다. */
        void endPin();
        /**
         * @brief 캔버스 단축키 Ctrl+C · Ctrl+X · Ctrl+V · Ctrl+D 를 받습니다. 캔버스 안에서 노드를 그린 뒤 프레임마다 부릅니다.
         * @param outListNodeID 대상 노드(고른 노드)입니다. 붙여 넣기는 비어 있습니다.
         * @param outPastePosition 붙일 자리 — 마우스의 캔버스 좌표입니다.
         * @return 이번 프레임의 명령입니다. 없으면 `None` 입니다.
         */
        EditorGraphEditCommand queryEditCommand( vector<int32>& outListNodeID, float2& outPastePosition );
        /** @brief 다음 캔버스를 열 때 고른 노드를 비웁니다 — 붙여 넣은 노드만 고른 채로 두려고 붙이기 전에 부릅니다. */
        void clearSelectionOnNextDraw() { _bClearSelection = SW_TRUE; }
        /** @brief 그래프 복사 버퍼에 애셋 종류 @p kind 의 조각 @p text 를 둡니다. 편집기 프로세스 안의 버퍼라 OS 클립보드를 건드리지 않습니다. */
        static void storeClipboard( string_view kind, string text );
        /** @brief 복사 버퍼가 종류 @p kind 의 조각이면 @p outText 에 담고 true 입니다. 비었거나 다른 종류면 false 입니다. */
        [[nodiscard]] static bool readClipboard( string_view kind, string& outText );
        /** @brief 캔버스 노드 id @p nodeID 가 주석 상자이면 true 입니다. 고른 노드 · 지우기 요청을 패널 노드와 가를 때 씁니다. */
        static bool isCommentNode( int32 nodeID ) { return nodeID > kCommentNodeIDBase; }
        /** @brief 보기 막대에 주석 단추를 둡니다. 주석 상자를 저장하는 그래프(`EditorGraphDocumentPanel`)만 켭니다. */
        void setCommentsEnabled( bool bEnabled ) { _bCommentsEnabled = bEnabled; }
        /**
         * @brief 주석 상자를 그립니다(언리얼 그래프 Comment · Godot GraphFrame). 캔버스 안에서 노드보다 먼저 부릅니다. 상자를 옮기면 안의 노드도 함께 옮겨진다.
         * @param bSeed 자리와 크기를 데이터에서 캔버스로 다시 넣습니다(문서를 열거나 되돌린 프레임). 처음 보는 상자는 늘 넣는다.
         * @details 머리줄을 두 번 누르면 이름 바꾸기 칸이 열린다.
         * @return 이름을 바꿨으면 true 입니다 — 패널이 편집으로 남긴다.
         */
        bool drawComments( vector<GraphCommentBox>& listComment, bool bSeed );
        /** @brief 캔버스의 주석 상자 자리와 크기를 @p listComment 에 싣습니다. 하나라도 바뀌었으면 true 입니다(끌어 옮기거나 늘였다). */
        bool syncCommentLayout( vector<GraphCommentBox>& listComment ) const;
        /** @brief 보기 막대의 주석 단추가 낸 요청입니다. 고른 노드를 감싸는 자리(없으면 보이는 가운데)를 돌려주고 요청을 지웁니다. */
        bool takeCommentRequest( float2& outPosition, float2& outSize );
        /** @brief 가장 최근에 그린 캔버스의 주석 상자 수입니다(감시 값 `Editor.GraphCommentCount`). */
        static uint32 getDrawnCommentCount();

        /** @brief 노드 위치를 그래프가 스스로 정하면(행동 트리의 자동 배치) true 로 둡니다. 보기 막대가 정렬 메뉴를 감춥니다 — 맞춘 위치가 다음 배치에 사라진다. */
        void setAutoLayout( bool bAutoLayout ) { _bAutoLayout = bAutoLayout; }

        /** @brief Begin 없이 현재 에디터만 바인딩합니다. 위치를 조회할 때 씁니다. */
        [[nodiscard]] bool bind() const;
        /** @brief bind() 와 짝입니다. */
        void unbind() const;

        /** @brief 컨텍스트가 만들어져 있으면 true 입니다. */
        bool isReady() const { return _pEditor != nullptr; }
        /** @brief 노드 편집기 컨텍스트를 반환합니다. 확장 모듈이 자기 imgui-node-editor 사본에 바인딩할 때 씁니다. 없으면 nullptr 입니다. */
        ax::NodeEditor::EditorContext* getContext() const { return _pEditor; }

        /**
         * @brief 빈 곳을 오른쪽 클릭하면 노드 검색 팝업을 엽니다. 캔버스 안(`beginCanvas` 에서 `endCanvas` 사이)에서 프레임마다 호출합니다.
         * @details 팝업은 검색 입력란(열 때 포커스를 받는다)과 그룹별 목록으로 이루어진다. Enter 는 첫 항목을 고른다. 레이블은 캔버스가 `<scope>.canvas`(endCanvas), 검색 입력란이 `<scope>.addNode.search` 이다(`beginCanvas` 의 범위).
         * @return 골랐으면 true 이며, 고른 종류와 오른쪽 클릭한 위치의 캔버스 좌표를 반환합니다.
         */
        bool drawAddNodePopup( const vector<EditorGraphNodeKind>& listKind, uint32& outKindID, float2& outCanvasPosition );
        /**
         * @brief 드래그해서 잇는 새 링크를 받습니다. `ed::BeginCreate` 에서 `EndCreate` 까지 전체를 처리하며 캔버스 안에서 호출합니다.
         * @param findPin 핀 번호로 연결 정보를 찾는 콜백입니다. 모르는 핀이면 false 를 반환하며, 그 연결은 거절됩니다.
         * @details 방향이나 타입이 맞지 않으면 링크를 빨갛게 표시해 거절하고 이유를 그 위치의 툴팁으로 보여 준다(`EditorNodeGraphRules::canConnect`).
         *          핀을 빈 곳에 놓으면 노드 검색 팝업을 열고, 거기서 고른 노드가 다음 프레임에 나타나면 그 노드의 맞는 첫 핀과 잇는 링크를 이 함수가 돌려준다
         *          (언리얼 · 유니티 그래프의 핀 끌어 노드 만들기). 그래서 패널은 노드를 `beginNode` · `beginPin` 으로 열고 이 함수를 노드를 그린 뒤 부른다.
         * @return 받아들인 연결이면 true 이며, 나가는 핀과 들어오는 핀 순서로 반환합니다.
         */
        bool queryNewLink( const Delegate<bool( int32, EditorGraphPinInfo& )>& findPin, int32& outFromPin, int32& outToPin );
        /** @brief 핀 타입 색 테이블을 설정합니다. 핀 타입 번호가 인덱스이며, 테이블 밖의 타입은 글자색을 씁니다. */
        void setPinTypeColors( const vector<Color>& listColor ) { _listPinTypeColor = listColor; }
        /** @brief 핀 타입의 색입니다. */
        Color getPinTypeColor( uint32 pinType ) const;
        /** @brief 핀 위치에 타입 색 원을 그립니다. 연결되었으면 채웁니다. `ed::BeginPin` 에서 `EndPin` 사이에서 호출합니다. */
        void drawPinIcon( uint32 pinType, bool bConnected ) const;
        /** @brief 이번 프레임의 문제 노드입니다. 패널이 검증한 결과를 넘깁니다. */
        void setNodeIssues( vector<EditorGraphNodeIssue> listIssue ) { _listNodeIssue = std::move( listIssue ); }
        /** @brief 문제 노드에 빨간 테두리를 두르고, 노드 위에 마우스가 있으면 이유를 툴팁으로 보여 줍니다. 캔버스 안에서 노드를 그린 뒤 호출합니다. */
        void drawNodeIssues() const;
        /** @brief 현재 문제 노드 수를 반환합니다. */
        uint32 getNodeIssueCount() const { return static_cast<uint32>( _listNodeIssue.size() ); }

        /** @brief 핀을 끌어 만든 노드에 링크를 이은 횟수입니다(감시 값 `Editor.GraphAutoLinkCount`). */
        static uint32 getAutoLinkCount();
        /** @brief 마지막 정렬이 옮긴 노드 수입니다(감시 값 `Editor.GraphAlignedNodeCount`). */
        static uint32 getAlignedNodeCount();
        /** @brief 가장 최근에 그린 캔버스가 미니맵을 보였으면 true 입니다(감시 값 `Editor.GraphMinimapShown`). */
        static bool isMinimapShown();

        /** @brief 이번 프레임에 그린 그래프의 노드 수를 기록합니다. 시나리오 프로브 `Editor.GraphNodeCount` 가 읽습니다. */
        static void recordDrawnNodeCount( uint32 nodeCount );
        /** @brief 가장 최근에 그린 그래프의 노드 수입니다. */
        static uint32 getDrawnNodeCount();

        /** @brief 다음 Begin 에서 노드 위치를 초기화하고 뷰를 맞출지 정합니다. */
        bool needsContentFit() const { return _bNeedsContentFit; }
        /** @brief 다음 캔버스에서 콘텐츠에 맞게 줌과 팬을 하도록 요청합니다. */
        void requestContentFit() { _bNeedsContentFit = SW_TRUE; }
        /**
         * @brief 요청이 있으면 캔버스 안에서 NavigateToContent 를 한 번 호출합니다. 캔버스 크기가 앞 프레임과 같을 때만 호출하며, 아니면 요청을 남겨 다음 프레임에 처리합니다.
         * @details 캔버스 크기가 막 정해진 프레임(처음 열 때, Reload 와 함께 옆 인스펙터가 붙거나 떨어질 때)에 맞추면 노드 편집기가 이전 크기의 보이는
         *          사각형으로 맞춘다. 그렇게 맞추면 Dialogue Graph 캔버스가 패널 일부만 덮고 링크가 패널 밖에 그려진다.
         */
        void applyContentFitIfNeeded();
        /** @brief 캔버스를 열어도 되는 크기인지 확인합니다. 한 변이라도 `kMinCanvasExtent` 보다 작으면 false 입니다. */
        static bool isCanvasRegionUsable( const float2& available ) { return available._x >= kMinCanvasExtent && available._y >= kMinCanvasExtent; }
        /** @brief 뷰 맞추기를 해도 되는 크기인지 확인합니다. 0 이 아니고 앞 프레임과 같아야 합니다. */
        static bool isCanvasSizeStable( const float2& previousSize, const float2& currentSize )
        {
            return currentSize._x > 0.0f && currentSize._y > 0.0f && previousSize._x == currentSize._x && previousSize._y == currentSize._y;
        }

    private:
        /** @brief 이번 프레임에 연 핀 하나와 그 노드입니다. */
        struct FramePin
        {
            int32 _pinID{ 0 };
            int32 _nodeID{ 0 };
        };

        /** @brief 노드 편집기의 색을 지금 테마 토큰으로 칠합니다(`beginCanvas` 가 프레임마다). */
        void applyThemeStyle();
        /** @brief 앞 프레임 보기 막대 · 미니맵이 남긴 요청(맞춤 · 정렬 · 노드로 이동)을 캔버스를 닫기 전에 반영합니다. */
        void applyViewRequests();
        /** @brief 미니맵에 그릴 노드 사각형 · 고른 노드 · 보이는 범위를 캔버스를 닫기 전에 모읍니다. */
        void collectMinimap();
        /** @brief 주석 단추를 눌렀으면 새 상자의 자리(고른 노드를 감싸거나 보이는 가운데)를 잽니다. `collectMinimap` 뒤에 부릅니다. */
        void measureCommentRequest();
        /** @brief 캔버스 오른쪽 위 보기 막대(맞춤 · 정렬 · 미니맵)를 그립니다. */
        void drawViewBar();
        /** @brief 보기 막대의 정렬 메뉴입니다. 고른 노드가 모자란 항목은 끕니다. */
        void drawAlignMenu();
        /** @brief 캔버스 오른쪽 아래 미니맵을 그립니다. 누르면 가까운 노드로 보기를 옮깁니다. */
        void drawMinimap();
        /**
         * @brief 핀을 끌어 만든 노드가 이번 프레임에 나타났으면 그 노드의 맞는 핀과 잇는 링크를 만듭니다.
         * @return 이었으면 true 이며 나가는 핀과 들어오는 핀 순서로 반환합니다.
         */
        bool takeAutoLink( const Delegate<bool( int32, EditorGraphPinInfo& )>& findPin, int32& outFromPin, int32& outToPin );

        void ensureContext( const utf8* pSettingsFileName );
        void destroyContext();

    private:
        ax::NodeEditor::EditorContext*         _pEditor;
        string                                 _settingsPath;
        string                                 _markScope;          ///< 이번 캔버스의 레이블 앞부분(beginCanvas 가 정한다)
        float2                                 _previousCanvasSize; ///< 앞 프레임의 캔버스 크기(beginCanvas 가 측정한다)
        float2                                 _canvasSize;         ///< 이번 프레임의 캔버스 크기
        vector<Color>                          _listPinTypeColor;   ///< 핀 타입 번호에 대응하는 색
        vector<EditorGraphNodeIssue>           _listNodeIssue;      ///< 이번 프레임의 문제 노드
        fixed_string<constant::buffer::kMax64> _addNodeFilter;      ///< 노드 검색어
        float2                                 _addNodePosition;    ///< 노드 검색 팝업을 연 위치(캔버스 좌표)
        float2                                 _canvasOrigin;       ///< 이번 프레임 캔버스의 화면 왼쪽 위(beginCanvas 가 잰다)
        vector<int32>                          _listFrameNode;      ///< 이번 프레임에 연 노드(beginNode)
        vector<FramePin>                       _listFramePin;       ///< 이번 프레임에 연 핀과 그 노드(beginPin)
        vector<EditorGraphNodeRect>            _listMinimapRect;    ///< 미니맵에 그릴 노드(캔버스 좌표, collectMinimap)
        vector<int32>                          _listSelectedNode;   ///< 고른 노드(collectMinimap)
        vector<int32>                          _listAutoLinkBefore; ///< 핀에서 노드를 고른 순간의 노드 — 새로 나타난 노드를 가린다
        float2                                 _viewMin;            ///< 보이는 범위(캔버스 좌표)
        float2                                 _viewMax;
        float2                                 _viewBarSize;        ///< 앞 프레임 보기 막대 크기 — 오른쪽 끝에 붙인다
        EditorGraphAlign                       _pendingAlign;       ///< 다음 프레임에 할 정렬(`Count` 면 없다)
        int32                                  _currentNodeID;      ///< 지금 연 노드(beginNode ~ endNode)
        optional<int32>                        _pendingFocusNodeID; ///< 다음 프레임에 화면 가운데로 옮길 노드(미니맵). 비어 있으면 없다
        int32                                  _pinDragSource;      ///< 빈 곳에 놓은 끌기의 시작 핀. 노드 검색 팝업이 열려 있는 동안 남는다
        int32                                  _autoLinkPin;        ///< 새 노드에 이을 핀. 0 이면 없다
        uint32                                 _autoLinkFramesLeft; ///< 새 노드를 기다릴 남은 프레임 — 노드를 넣지 못했으면 요청을 버린다
        uint8                                  _bNeedsContentFit  : 1;
        uint8                                  _bPendingNavigate  : 1; ///< 다음 프레임에 보기를 맞춘다(고른 노드가 있으면 그 노드에)
        uint8                                  _bOpenAddFromPin   : 1; ///< 다음 `drawAddNodePopup` 이 노드 검색 팝업을 연다(핀 끌기)
        uint8                                  _bMinimapVisible   : 1;
        uint8                                  _bAutoLayout       : 1; ///< 노드 위치를 그래프가 정한다 — 정렬 메뉴를 감춘다
        uint8                                  _bClearSelection   : 1; ///< 다음 캔버스를 열 때 고른 노드를 비운다(붙여 넣기)
        uint8                                  _bCommentsEnabled  : 1; ///< 보기 막대에 주석 단추를 둔다
        uint8                                  _bCommentRequested : 1; ///< 주석 단추를 눌렀다 — 다음 캔버스를 닫기 전에 자리를 잰다
        uint8                                  _bCommentReady     : 1; ///< 잰 자리가 있다(takeCommentRequest 가 가져간다)
        float2                                 _commentPosition;       ///< 새 주석 상자의 왼쪽 위(캔버스 좌표)
        float2                                 _commentSize;           ///< 새 주석 상자의 영역 크기
        int32                                  _renameCommentID;       ///< 이름을 바꾸는 주석 id. 0 이면 없다
        fixed_string<constant::buffer::kMax64> _renameText;            ///< 이름 바꾸기 칸의 글
        vector<int32>                          _listSeededComment;     ///< 자리 · 크기를 캔버스에 넣은 주석 id
    };
} // namespace sw::editor
