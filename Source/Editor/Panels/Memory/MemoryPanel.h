/**
 * @file MemoryPanel.h
 * @brief Memory 창입니다 — 메모리 스냅숏을 찍고(이 프로세스 · 원격 서버) 요약(CPU · GPU 합 · 태그 막대)과 시트 표(태그 · 할당 사이트 · 에셋 · GPU 자원 · 형식 · 캐시)를 보이며,
 *        두 스냅숏을 비교하고(늘어난 것 순) 표 · 스냅숏을 CSV 로 내보냅니다. 에셋 줄은 콘텐츠 브라우저 · 인스펙터로 보낸다.
 * @details 값은 `MemoryViewModel::gather` 가 짓고 명령도 그쪽이 받는다. 이 패널은 이름 거르기 편집 버퍼 · 고른 줄 · 그리기만 든다(MVVM 계약).
 *          상용 엔진 비교: 유니티 Memory Profiler 창, 언리얼 Memory Insights. 디자인: 강조색은 데이터(태그 막대 · 늘어남/줄어듦) · 고른 줄에만, 숫자 칸은 오른쪽 정렬.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Panels/Memory/Model/MemoryViewModel.h"

namespace sw::editor
{
    /** @brief 메모리 스냅숏 · 비교 · CSV 내보내기 도구 창입니다. */
    class MemoryPanel : public EditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL`, `-gv_editorOpenPanel`, `panel.focus memory`). */
        static constexpr const utf8* kPanelID = "memory";
        /** @brief 창 메뉴의 순서입니다(Bug Report 바로 뒤 — 진단 도구 묶음). */
        static constexpr int32 kMenuOrder = 2046;

        /** @brief Memory 창을 만듭니다(닫힌 채 시작한다). */
        MemoryPanel();

        /** @brief 창 제목입니다. */
        const utf8* getPanelTitle() const override { return "Memory"; }
        /** @brief 단추 줄 · 요약 · 시트 탭 · 표를 그립니다. */
        void drawContent() override;

        /** @brief 들고 있는 스냅숏 수입니다(감시 값 `Editor.MemorySnapshotCount`). */
        uint32 getSnapshotCount() const { return _viewModel.getSnapshotCount(); }
        uint32 getFinishedExportCount() const { return _viewModel.getFinishedExportCount(); }
        /** @brief 마지막으로 그린 표의 줄 수입니다(감시 값 `Editor.MemoryTableRows`). */
        uint32 getVisibleRowCount() const { return _viewModel.getVisibleRowCount(); }
        /** @brief 비교 중이면 true 입니다(감시 값 `Editor.MemoryCompareMode`). */
        bool isCompareMode() const { return _viewModel.isCompareMode(); }

    private:
        void drawToolbar();
        void drawSummary();
        void drawSheetTabs();
        void drawFilterRow();
        void drawTable();
        /** @brief 고른 에셋을 콘텐츠 브라우저에서 보이고 인스펙터 포커스를 옮깁니다. */
        void revealSelectedAsset();

    private:
        MemoryViewModel                         _viewModel;
        MemoryViewState                         _viewState;         ///< 프레임마다 다시 채운다
        fixed_string<constant::buffer::kMax128> _nameFilter;        ///< 이름 거르기 편집 버퍼
        string                                  _selectedAssetPath; ///< 고른 에셋 줄의 경로(없으면 비어 있다)
    };
} // namespace sw::editor
