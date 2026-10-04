/**
 * @file ProfilerPanel.h
 * @brief 프로파일링 창입니다 — 엔진 프로파일러의 실시간 구간 표 · GPU 패스 표 · 프레임 그래프, 메모리, "Tracy 열기".
 * @details 상태 · 집계는 ImGui 없는 `ProfilerScopeHistory` 가 맡고(EditorTest), 여기는 그리기만 합니다. 엔진 표만 읽으므로 Tracy 가 없는 빌드에서도 같습니다.
 *          시간축 분석(스레드 타임라인 · GPU 큐 · 잠금)은 외부 Tracy 뷰어가 맡습니다(`EditorTracyLauncher`).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Commands/EditorBackgroundIo.h"
#include "Editor/Common/Commands/EditorTracyLauncher.h"
#include "Editor/Common/Gui/IEditorPanel.h"
#include "Editor/Panels/ProfilerScopeHistory.h"

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
        void drawGpuMemoryTab();
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

        /** @brief 활성 씬의 컴포넌트 분포 섹션을 그립니다. */
        void drawSceneDistributionSection();

    private:
        float32                     _arrFrameTimeHistory[120];
        uint32                      _historyOffset;
        EditorResourceCatalogJob    _catalogJob;
        EditorResourceCatalogCounts _catalogCounts;
        ProfilerScopeHistory        _scopeHistory;      ///< 최근 N 프레임 고리(ImGui 없음)
        ProfilerRowQuery            _rowQuery;          ///< 검색어 · 정렬(종류는 탭이 정한다)
        vector<ProfilerScopeRow>    _listScratchRow;    ///< 표 줄(프레임마다 다시 쓴다)
        vector<float32>             _listScratchSeries; ///< 그래프 값(프레임마다 다시 쓴다)
        utf8                        _arrFilter[constant::kMaxBuffer128];
        EditorTracyLaunchResult     _lastTracyResult; ///< 마지막 "Open Tracy" 결과(패널에 한 줄로 보인다)
        uint8                       _bCatalogDirty : 1;
        uint8                       _bCollect      : 1; ///< 엔진 프로파일러를 켜 두고 담는다
        uint8                       _bTracyTried   : 1; ///< "Open Tracy" 를 한 번이라도 눌렀다
        [[maybe_unused]] uint8      _reserved      : 5;
    };
} // namespace sw::editor
