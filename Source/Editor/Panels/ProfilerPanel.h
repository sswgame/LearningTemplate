/**
 * @file ProfilerPanel.h
 * @brief 프로파일링 창입니다 — 엔진 프로파일러의 실시간 구간 표 · GPU 패스 표 · 프레임 그래프, 메모리, "Tracy 열기".
 * @details 상태 · 집계는 ImGui 없는 `ProfilerScopeHistory` 가 맡고(EditorTest), 여기는 그리기만 합니다. 엔진 표만 읽으므로 Tracy 가 없는 빌드에서도 같습니다.
 *          Timeline 탭은 최근 몇 프레임의 계측 구간을 스레드마다 펼칩니다(`ProfilerTimeline`, 배치는 ImGui 없는 `ProfilerTimelineLayout`) — 빠른 확인용이고,
 *          긴 시간축 분석(GPU 큐 · 잠금 · 컨텍스트 전환)은 외부 Tracy 뷰어가 맡습니다(`EditorTracyLauncher`).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/Commands/EditorTracyLauncher.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/ProfilerScopeHistory.h"
#include "Editor/Panels/ProfilerTimelineLayout.h"

namespace sw::editor
{
    /** @brief 메모리 · 성능 프로파일러 탭을 보여 주는 에디터 도구 창입니다. */
    class ProfilerPanel : public IEditorPanel
    {
    public:
        /** @brief 프로파일러 창을 만듭니다. */
        ProfilerPanel();
        /** @brief 추가 해제할 GPU 리소스는 없습니다. */
        virtual ~ProfilerPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Profiler"; }
        /** @brief 프로파일러 UI를 그립니다. */
        void drawContent() override;
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */
        bool isToolPanel() const override { return true; }

    private:
        /** @brief 메모리 프로파일 탭을 그립니다. */
        void drawMemoryTab();
        /** @brief GPU 메모리 탭을 그립니다 — `-gv_profileFrames` 보고의 GPU 표와 같은 숫자(장부 줄별 · 드라이버 · 엔진 밖)입니다. */
        void drawGPUMemoryTab();
        /** @brief 실시간 FPS 및 씬 성능 진단 탭을 그립니다. */
        void drawPerformanceTab();
        /** @brief 구간 표 탭입니다(CPU 구간 · 카운터 또는 GPU 패스). 위에 프레임 그래프와 계측 · Tracy 줄이 있습니다. */
        void drawScopeTab( ProfilerScopeKind kind );
        /** @brief 계측 켜기 · 창 크기 · Tracy 상태와 "Open Tracy" 버튼 줄을 그립니다. */
        void drawCaptureControls();
        /** @brief 최근 N 프레임의 GT.Frame · RT.Frame · GPU.Frame 그래프를 그립니다. */
        void drawFrameGraph();
        /** @brief 지금 조건으로 구간 표를 그립니다(정렬은 머리줄을 눌러 바꾼다). */
        void drawScopeTable( ProfilerScopeKind kind );
        /** @brief 스레드 미니 타임라인 탭입니다 — 녹화 · 최근 프레임 수 · Freeze · 확대(휠) · 이동(끌기) · 툴팁 · 프레임 경계선. */
        void drawTimelineTab();
        /** @brief 타임라인 캔버스(스레드 줄 · 사각형 · 프레임 경계선 · 툴팁)를 그립니다. */
        void drawTimelineCanvas();

        /** @brief 활성 씬의 컴포넌트 분포 섹션을 그립니다. */
        void drawSceneDistributionSection();

    private:
        float32                        _arrFrameTimeHistory[120];
        uint32                         _historyOffset;
        EditorResourceCatalogJob       _catalogJob;
        EditorResourceCatalogCounts    _catalogCounts;
        ProfilerScopeHistory           _scopeHistory;      ///< 최근 N 프레임 고리(ImGui 없음)
        ProfilerRowQuery               _rowQuery;          ///< 검색어 · 정렬(종류는 탭이 정한다)
        vector<ProfilerScopeRow>       _listScratchRow;    ///< 표 줄(프레임마다 다시 쓴다)
        vector<float32>                _listScratchSeries; ///< 그래프 값(프레임마다 다시 쓴다)
        utf8                           _arrFilter[constant::kMaxBuffer128];
        EditorTracyLaunchResult        _lastTracyResult;        ///< 마지막 "Open Tracy" 결과(패널에 한 줄로 보인다)
        vector<ProfilerTimelineThread> _listTimelineThread;     ///< 타임라인 사건(Freeze 면 그대로 둔다)
        vector<ProfilerTimelineRect>   _listTimelineRect;       ///< 배치 결과(프레임마다 다시 쓴다)
        vector<uint16>                 _listTimelineLaneCount;  ///< 스레드마다 겹 수
        vector<uint64>                 _listTimelineFrameBegin; ///< 보이는 범위의 프레임 경계
        uint64                         _timelineWindowBegin;    ///< 모은 구간(최근 N 프레임)
        uint64                         _timelineWindowEnd;
        uint64                         _timelineViewBegin; ///< 보이는 범위(확대 · 이동)
        uint64                         _timelineViewEnd;
        uint32                         _timelineFrameCount; ///< 최근 프레임 수(1 · 4 · 16)
        uint8                          _bCatalogDirty   : 1;
        uint8                          _bCollect        : 1; ///< 엔진 프로파일러를 켜 두고 담는다
        uint8                          _bTracyTried     : 1; ///< "Open Tracy" 를 한 번이라도 눌렀다
        uint8                          _bTimelineFrozen : 1; ///< Freeze — 녹화는 계속하고 보기만 멈춘다
        uint8                          _bTimelineZoomed : 1; ///< 사용자가 확대 · 이동했다(아니면 보이는 범위가 모은 구간을 따라간다)
        [[maybe_unused]] uint8         _reserved        : 3;
    };
} // namespace sw::editor
