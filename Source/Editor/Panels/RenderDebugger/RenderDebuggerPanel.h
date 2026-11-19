/**
 * @file RenderDebuggerPanel.h
 * @brief 렌더 디버거 창 — 프레임 디버거 · 렌더 그래프 그림 · 버퍼 보기 · 가림과 스트리밍을 탭으로 봅니다(유니티 Frame Debugger · Rendering Debugger, 언리얼 RDG Insights 자리).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Common/Widgets/EditorNodeGraph.h"
#include "Editor/Panels/RenderDebugger/Model/RenderDebuggerViewModel.h"

namespace sw::editor
{
    /**
     * @class RenderDebuggerPanel
     * @brief 파일 머리말 참고. 값과 명령은 `RenderDebuggerViewModel` 이 맡고 이 창은 그리기만 한다(MVVM 계약).
     * @details 프레임 디버거가 멈춘 지점의 그림은 렌더 타깃 창(`RenderTargetPanel`)과 게임 뷰가 보인다 — 렌더러가 그 지점 뒤의 씬 드로우를 내지 않는다.
     */
    class RenderDebuggerPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다(등록 · 조회 · `panel.focus`). */
        static constexpr const utf8* kPanelID        = "render_debugger";
        static constexpr int32       kMenuOrder      = 910;    ///< 도구 메뉴 차례 — 렌더 타깃 창(900) 바로 뒤
        static constexpr float32     kInitialColumns = 120.0f; ///< 처음 창 폭(글자 칸) — 기록 목록과 바인딩 글이 나란히 들어간다
        static constexpr float32     kInitialRows    = 34.0f;  ///< 처음 창 높이(글자 줄)
        static constexpr int32       kNoStopEvent    = -1;     ///< `getStopEventIndex` 의 "멈춤 없음"

        /** @brief 도구 창이라 닫힌 채로 시작합니다. */
        RenderDebuggerPanel();
        ~RenderDebuggerPanel() override = default;

        const utf8*      getPanelTitle() const override { return "Render Debugger"; }
        void             drawContent() override;
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( kInitialColumns, kInitialRows ); }
        /** @brief 노드 편집기 문맥을 놓습니다. 프레임 디버거는 끈다 — 창이 닫혀도 렌더러가 드로우를 건너뛰면 안 된다. */
        void shutdown() override;
        /** @brief 창을 닫으면 프레임 디버거를 끈다 — 보이지 않는 창이 게임 뷰의 드로우를 건너뛰게 두지 않는다. */
        void onPanelClosed() override { _viewModel.setFrameDebuggerEnabled( false ); }

        // ------------------------------------------------------------------------------
        // 테스트 감시 값(`RenderDebuggerWatches.cpp`)
        // ------------------------------------------------------------------------------
        /** @brief 지금 탭입니다. */
        RenderDebuggerTab getTab() const { return _pState != nullptr ? _pState->_tab : RenderDebuggerTab::FrameDebugger; }
        /** @brief 프레임 디버거 기록 수입니다. */
        uint32 getEventCount() const { return _pState != nullptr ? static_cast<uint32>( _pState->_listEvent.size() ) : 0u; }
        /** @brief 멈춤 지점의 기록 번호입니다(없으면 -1 — 전체 프레임 · 꺼짐). */
        int32 getStopEventIndex() const;
        /** @brief 고른 그래프의 패스 수입니다. */
        uint32 getGraphPassCount() const;
        /** @brief 지난 프레임에 캔버스에 그린 패스 노드 수입니다. */
        uint32 getDrawnGraphNodeCount() const { return _drawnGraphNodeCount; }
        /** @brief 버퍼 목록 수입니다. */
        uint32 getBufferCount() const { return _pState != nullptr ? static_cast<uint32>( _pState->_listBuffer.size() ) : 0u; }
        /** @brief 고른 버퍼를 읽어 표가 있으면 행 수, 아니면 0 입니다. */
        uint32 getBufferRowCount() const;

    private:
        void drawFrameDebuggerTab();
        void drawEventDetail( const FrameDebugDrawEvent& event );
        void drawRenderGraphTab();
        void drawGraphCanvas( const RenderGraphSnapshot& graph );
        void drawBuffersTab();
        void drawOcclusionTab();
        /** @brief 탭 하나를 열고(고른 탭이면 내용을 그린다) 그 탭이 바뀌었으면 ViewModel 에 알립니다. */
        bool beginTab( const utf8* pLabel, const utf8* pMark, RenderDebuggerTab tab );

    private:
        RenderDebuggerViewModel    _viewModel;
        const RenderDebuggerState* _pState; ///< 이번 프레임 `gather` 의 값(ViewModel 이 든다 — 다음 gather 까지 유효)
        EditorNodeGraph            _nodeGraph;
        uint32                     _placedLayoutSerial;  ///< 노드를 마지막으로 놓은 배치 번호(`RenderDebuggerState::_layoutSerial`) — 바뀌면 다시 놓는다
        uint32                     _drawnGraphNodeCount; ///< 지난 프레임에 그린 노드 수
        uint32                     _selectedEvent;       ///< 자세히 보는 기록 번호
    };
} // namespace sw::editor
