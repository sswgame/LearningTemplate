/**
 * @file EditorNodeGraph.h
 * @brief imgui-node-editor 컨텍스트 수명주기 및 캔버스 Begin/End
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
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
     * @brief 노드 그래프 캔버스 호스트입니다(언리얼 SGraphEditor · 유니티 GraphView 의 자리). 패널 · 팝업 · 섹션이 멤버로 소유합니다.
     * @details 틀이 주는 것: 빈 곳 오른쪽 클릭 → 검색해 노드 넣기(`drawAddNodePopup`), 핀 타입 색과 맞지 않는 연결 거절(`queryNewLink`),
     *          문제 노드의 빨간 테두리 + 툴팁(`setNodeIssues` · `drawNodeIssues`). 판단은 ImGui 없는 `EditorNodeGraphRules` 가 한다.
     *          확장 모듈은 imgui-node-editor 사본을 따로 가지므로(정적 링크) 캔버스 안에서 `ax::NodeEditor` 를 직접 부르기 전에 자기 사본에도
     *          지금 편집기를 걸어야 한다 — `EditorGraphDocumentPanel::beginGraphCanvas` 가 한다.
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
         * @brief 컨텍스트를 준비하고 캔버스를 엽니다. false 면 endCanvas 를 부르지 마십시오.
         * @details 남은 영역이 거의 없으면(도킹 직후의 첫 프레임은 높이가 음수다) 캔버스를 열지 않고 false 입니다. 컨텍스트 자체가 없어서 false 인지는
         *          `hasContext` 로 가립니다. 캔버스가 창의 첫 그리기여도 배경과 노드가 잘리지 않게 같은 색 배경을 먼저 그립니다(구현 주석).
         */
        bool beginCanvas( const utf8* pCanvasID, const utf8* pSettingsFileName );
        /** @brief 노드 편집기 컨텍스트가 있는지 묻습니다(`beginCanvas` 가 false 일 때 실패와 "아직 그릴 자리가 없음" 을 가린다). */
        bool hasContext() const { return _pEditor != nullptr; }
        /** @brief beginCanvas()와 짝을 이룹니다. */
        void endCanvas();

        /** @brief Begin 없이 현재 에디터만 바인딩합니다(위치 조회용). */
        bool bind() const;
        /** @brief bind()와 짝을 이룹니다. */
        void unbind() const;

        /** @brief 컨텍스트가 만들어져 있으면 true입니다. */
        bool isReady() const { return _pEditor != nullptr; }
        /** @brief 노드 편집기 컨텍스트입니다(확장 모듈이 자기 imgui-node-editor 사본에 걸 때 쓴다). 없으면 nullptr 입니다. */
        ax::NodeEditor::EditorContext* getContext() const { return _pEditor; }

        /**
         * @brief 빈 곳 오른쪽 클릭이면 노드 찾아 넣기 팝업을 엽니다. 캔버스 안(`beginCanvas` ~ `endCanvas`)에서 프레임마다 부릅니다.
         * @details 팝업은 검색 칸(열 때 초점) + 묶음별 목록입니다. Enter 는 첫 줄을 고릅니다. 이름표: 캔버스 `graph.canvas`(endCanvas), 검색 칸 `graph.addNode.search`.
         * @return 골랐으면 true 이고 그 종류와, 오른쪽 클릭한 자리의 캔버스 좌표를 돌려줍니다.
         */
        bool drawAddNodePopup( const vector<EditorGraphNodeKind>& listKind, uint32& outKindID, float2& outCanvasPosition );
        /**
         * @brief 끌어서 잇는 새 링크를 받습니다(`ed::BeginCreate` ~ `EndCreate` 전체). 캔버스 안에서 부릅니다.
         * @param findPin 핀 번호 → 연결 정보. 모르는 핀이면 false 를 돌려준다(그 연결은 거절).
         * @details 방향 · 타입이 맞지 않으면 링크를 빨갛게 거절하고 이유를 그 자리 툴팁으로 보입니다(`EditorNodeGraphRules::canConnect`).
         * @return 받아들인 연결이면 true 이고 나가는 핀 → 들어오는 핀 순서로 돌려줍니다.
         */
        bool queryNewLink( const Delegate<bool( int32, EditorGraphPinInfo& )>& findPin, int32& outFromPin, int32& outToPin );
        /** @brief 핀 타입 색 표를 둡니다(핀 타입 번호 = 칸). 표 밖의 타입은 글자색입니다. */
        void setPinTypeColors( const vector<Color4>& listColor ) { _listPinTypeColor = listColor; }
        /** @brief 핀 타입의 색입니다. */
        Color4 getPinTypeColor( uint32 pinType ) const;
        /** @brief 핀 자리에 타입 색 동그라미를 그립니다(이어졌으면 채운다). `ed::BeginPin` ~ `EndPin` 안에서 부릅니다. */
        void drawPinIcon( uint32 pinType, bool bConnected ) const;
        /** @brief 이번 프레임의 문제 노드입니다. 패널이 검증한 결과를 넘깁니다. */
        void setNodeIssues( vector<EditorGraphNodeIssue> listIssue ) { _listNodeIssue = std::move( listIssue ); }
        /** @brief 문제 노드에 빨간 테두리를 두르고, 노드 위에 마우스가 있으면 이유를 툴팁으로 보입니다. 캔버스 안에서 노드를 그린 뒤 부릅니다. */
        void drawNodeIssues() const;
        /** @brief 지금 문제 노드 수입니다. */
        uint32 getNodeIssueCount() const { return static_cast<uint32>( _listNodeIssue.size() ); }

        /** @brief 이번 프레임에 그린 그래프의 노드 수를 적습니다(시나리오 탐침 `Editor.GraphNodeCount` 가 읽는다). */
        static void noteDrawnNodeCount( uint32 nodeCount );
        /** @brief 가장 최근에 그린 그래프의 노드 수입니다. */
        static uint32 getDrawnNodeCount();

        /** @brief 다음 Begin에서 노드 위치를 시드하고 뷰를 맞출지 여부입니다. */
        bool needsContentFit() const { return _bNeedsContentFit; }
        /** @brief 다음 캔버스에서 콘텐츠에 맞게 줌/팬하도록 요청합니다. */
        void requestContentFit() { _bNeedsContentFit = true; }
        /**
         * @brief 요청이 있으면 캔버스 안에서 한 번 NavigateToContent 를 호출합니다 — 캔버스 크기가 앞 프레임과 같을 때만(아니면 요청을 남겨 다음 프레임에).
         * @details 캔버스 크기가 막 정해진 프레임(처음 열기 · Reload 와 함께 옆 인스펙터가 붙거나 떨어짐)에 맞추면 노드 편집기가 옛 크기의 보이는
         *          사각형으로 맞춘다는 가설(패널 점검 D21 — Dialogue Graph 캔버스가 일부만 덮고 링크가 패널 밖에 그려짐)에 따른 것이다.
         */
        void applyContentFitIfNeeded();
        /** @brief 캔버스를 열어도 되는 크기인지 묻습니다. 한 변이라도 `kMinCanvasExtent` 보다 작으면 false 입니다. */
        static bool isCanvasRegionUsable( const float2& available ) { return available._x >= kMinCanvasExtent && available._y >= kMinCanvasExtent; }
        /** @brief 캔버스를 여는 최소 변 길이(픽셀)입니다. 노드 하나도 못 그리는 크기에서는 열지 않는다. */
        static constexpr float32 kMinCanvasExtent = 16.0f;
        /** @brief 맞추기를 해도 되는 크기인지입니다 — 0 이 아니고 앞 프레임과 같다. */
        static bool isCanvasSizeSettled( const float2& previousSize, const float2& currentSize )
        {
            return currentSize._x > 0.0f && currentSize._y > 0.0f && previousSize._x == currentSize._x && previousSize._y == currentSize._y;
        }

    private:
        void ensureContext( const utf8* pSettingsFileName );
        void destroyContext();

        ax::NodeEditor::EditorContext*       _pEditor;
        string                               _settingsPath;
        float2                               _previousCanvasSize; ///< 앞 프레임의 캔버스 크기(beginCanvas 가 잰다)
        float2                               _canvasSize;         ///< 이번 프레임의 캔버스 크기
        vector<Color4>                       _listPinTypeColor;   ///< 핀 타입 번호 → 색
        vector<EditorGraphNodeIssue>         _listNodeIssue;      ///< 이번 프레임의 문제 노드
        fixed_string<constant::kMaxBuffer64> _addNodeFilter;      ///< 찾아 넣기 검색어
        float2                               _addNodePosition;    ///< 찾아 넣기를 연 자리(캔버스 좌표)
        bool                                 _bNeedsContentFit;
    };
} // namespace sw::editor
