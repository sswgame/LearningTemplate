/**
 * @file EditorNodeGraph.h
 * @brief imgui-node-editor 컨텍스트의 수명 주기와 캔버스의 Begin/End 를 관리합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorColor.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Widgets/EditorNodeGraphRules.h"

namespace ax::NodeEditor
{
    struct EditorContext;
} // namespace ax::NodeEditor

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
     */
    class SW_EDITOR_API EditorNodeGraph
    {
    public:
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
        /** @brief beginCanvas() 와 짝입니다. */
        void endCanvas();

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

        /** @brief 이번 프레임에 그린 그래프의 노드 수를 기록합니다. 시나리오 프로브 `Editor.GraphNodeCount` 가 읽습니다. */
        static void noteDrawnNodeCount( uint32 nodeCount );
        /** @brief 가장 최근에 그린 그래프의 노드 수입니다. */
        static uint32 getDrawnNodeCount();

        /** @brief 다음 Begin 에서 노드 위치를 초기화하고 뷰를 맞출지 정합니다. */
        bool needsContentFit() const { return _bNeedsContentFit; }
        /** @brief 다음 캔버스에서 콘텐츠에 맞게 줌과 팬을 하도록 요청합니다. */
        void requestContentFit() { _bNeedsContentFit = true; }
        /**
         * @brief 요청이 있으면 캔버스 안에서 NavigateToContent 를 한 번 호출합니다. 캔버스 크기가 앞 프레임과 같을 때만 호출하며, 아니면 요청을 남겨 다음 프레임에 처리합니다.
         * @details 캔버스 크기가 막 정해진 프레임(처음 열 때, Reload 와 함께 옆 인스펙터가 붙거나 떨어질 때)에 맞추면 노드 편집기가 이전 크기의 보이는
         *          사각형으로 맞춘다. 그렇게 맞추면 Dialogue Graph 캔버스가 패널 일부만 덮고 링크가 패널 밖에 그려진다.
         */
        void applyContentFitIfNeeded();
        /** @brief 캔버스를 열어도 되는 크기인지 확인합니다. 한 변이라도 `kMinCanvasExtent` 보다 작으면 false 입니다. */
        static bool isCanvasRegionUsable( const float2& available ) { return available._x >= kMinCanvasExtent && available._y >= kMinCanvasExtent; }
        /** @brief 캔버스를 여는 최소 변 길이(픽셀)입니다. 노드 하나도 그릴 수 없는 크기에서는 열지 않습니다. */
        static constexpr float32 kMinCanvasExtent = 16.0f;
        /** @brief 뷰 맞추기를 해도 되는 크기인지 확인합니다. 0 이 아니고 앞 프레임과 같아야 합니다. */
        static bool isCanvasSizeSettled( const float2& previousSize, const float2& currentSize )
        {
            return currentSize._x > 0.0f && currentSize._y > 0.0f && previousSize._x == currentSize._x && previousSize._y == currentSize._y;
        }

    private:
        void ensureContext( const utf8* pSettingsFileName );
        void destroyContext();

        ax::NodeEditor::EditorContext*       _pEditor;
        string                               _settingsPath;
        string                               _markScope;          ///< 이번 캔버스의 레이블 앞부분(beginCanvas 가 정한다)
        float2                               _previousCanvasSize; ///< 앞 프레임의 캔버스 크기(beginCanvas 가 측정한다)
        float2                               _canvasSize;         ///< 이번 프레임의 캔버스 크기
        vector<Color>                        _listPinTypeColor;   ///< 핀 타입 번호에 대응하는 색
        vector<EditorGraphNodeIssue>         _listNodeIssue;      ///< 이번 프레임의 문제 노드
        fixed_string<constant::kMaxBuffer64> _addNodeFilter;      ///< 노드 검색어
        float2                               _addNodePosition;    ///< 노드 검색 팝업을 연 위치(캔버스 좌표)
        bool                                 _bNeedsContentFit;
    };
} // namespace sw::editor
