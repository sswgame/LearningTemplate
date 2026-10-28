/**
 * @file ProfilerPanel.h
 * @brief 엔진 프로파일러의 실시간 구간 테이블, GPU 패스 테이블, 프레임 그래프, 메모리, "Tracy 열기" 를 보여 주는 프로파일링 창입니다.
 * @details 상태와 집계는 ImGui 에 의존하지 않는 `ProfilerScopeHistory` 가 맡고(EditorTest), 이 창은 그리기만 한다. 엔진 테이블만 읽으므로 Tracy 가 없는 빌드에서도 같게 동작한다.
 *          Timeline 탭은 최근 몇 프레임의 계측 구간을 스레드별로 펼친다(`ProfilerTimeline`, 배치는 ImGui 에 의존하지 않는 `ProfilerTimelineLayout`). 빠른 확인용이다.
 *          긴 시간축 분석(GPU 큐, 락, 컨텍스트 전환)은 외부 Tracy 뷰어가 맡는다(`EditorTracyLauncher`).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/Dev/EditorTracyLauncher.h"
#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/Profiler/Model/ProfilerCallTree.h"
#include "Editor/Panels/Profiler/Model/ProfilerScopeHistory.h"
#include "Editor/Panels/Profiler/Model/ProfilerTimelineLayout.h"

#include "Engine/Profiling/ProfilerTraceExport.h"

namespace sw
{
    class FrameProfiler;
} // namespace sw

namespace sw::editor
{
    class EditorPanelManager;

    /** @brief 메모리와 성능 프로파일러 탭을 보여 주는 에디터 도구 창입니다. */
    class ProfilerPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "profiler";

        /** @brief 프로파일러 창을 만듭니다. */
        ProfilerPanel();
        /** @brief 해제할 GPU 리소스가 없으므로 아무것도 하지 않습니다. */
        virtual ~ProfilerPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel: 제목과 그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8*    getPanelTitle() const override { return "Profiler"; }
        EditorDockSlot getDefaultDockSlot() const override { return EditorDockSlot::Center; }
        /** @brief 프로파일러 UI를 그립니다. */
        void drawContent() override;
        /** @brief 필요할 때 여는 도구이므로 닫힌 채로 시작합니다. */
        bool isToolPanel() const override { return true; }

    private:
        /** @brief 메모리 프로파일 탭을 그립니다. */
        void drawMemoryTab();
        /** @brief GPU 메모리 탭을 그립니다. `-gv_profileFrames` 보고의 GPU 테이블과 같은 수치(장부 항목별, 드라이버, 엔진 밖)를 보여 줍니다. */
        void drawGPUMemoryTab();
        /** @brief 실시간 FPS 와 씬 성능 진단 탭을 그립니다. */
        void drawPerformanceTab();
        /** @brief 구간 테이블 탭을 그립니다. CPU 구간과 카운터 또는 GPU 패스를 보여 주며, 위쪽에 프레임 그래프와 계측, Tracy 행이 있습니다. */
        void drawScopeTab( ProfilerScopeKind kind );
        /** @brief 계측 켜기, 창 크기, Tracy 상태, "Open Tracy" 버튼이 있는 행을 그립니다. */
        void drawCaptureControls();
        /** @brief 계측 켜기 · 지우기 · 정지 · 스파이크 · 캡처 저장 · 불러오기 단추입니다. */
        void drawCaptureHistoryControls();
        /** @brief Tracy 상태와 RenderDoc 캡처 단추입니다. */
        void drawExternalProfilerControls();
        /** @brief 최근 N 프레임의 GT.Frame · RT.Frame · GPU.Frame 그래프를 그립니다. */
        void drawFrameGraph();
        /** @brief 현재 조건으로 구간 테이블을 그립니다. 헤더를 눌러 정렬을 바꿉니다. */
        void drawScopeTable( ProfilerScopeKind kind );
        /** @brief 스레드 미니 타임라인 탭을 그립니다. 녹화, 최근 프레임 수, Freeze, 휠 확대, 드래그 이동, 툴팁, 프레임 경계선을 지원합니다. */
        void drawTimelineTab();
        /** @brief 타임라인 탭의 위쪽 막대(녹화 · 프레임 수 · Freeze · Fit · 트레이스 저장)입니다. */
        void drawTimelineToolbar( FrameProfiler& profiler, ProfilerTimeline& timeline, bool bRecording );
        /**
         * @brief 타임라인 캔버스를 그립니다. 스레드 행, 사각형, 프레임 경계선, 툴팁이 포함됩니다.
         * @param listThread 그릴 스레드 목록입니다. 살아 있는 기록에 불러온 트레이스를 겹친 것입니다.
         * @param pLoadedSlotName 불러온 트레이스의 구간 이름 테이블입니다. 없으면 nullptr 이며, `kLoadedSlotBase` 이상인 슬롯이 이 테이블을 가리킵니다.
         */
        void drawTimelineCanvas( const vector<ProfilerTimelineThread>& listThread, const vector<string>* pLoadedSlotName );
        /** @brief 타임라인 캔버스 한 번 그리기의 크기와 위치(화면 좌표)입니다. */
        struct TimelineGeometry;
        /** @brief 캔버스 크기를 정하고 이벤트 사각형을 배치합니다(배치 결과 멤버와 스레드별 레인 수 멤버를 채운다). */
        TimelineGeometry layoutTimeline( const vector<ProfilerTimelineThread>& listThread );
        /** @brief 캔버스 위 휠 확대와 드래그 이동을 처리합니다. 캔버스 위에 마우스가 있으면 true 입니다. */
        bool handleTimelineInput( const TimelineGeometry& geometry );
        /** @brief 스레드 행의 배경과 이름을 그리고 행마다 위쪽 y 를 돌려줍니다. */
        vector<float32> drawTimelineRows( const vector<ProfilerTimelineThread>& listThread, const TimelineGeometry& geometry );
        /** @brief 이벤트 사각형과 프레임 경계선을 그리고, 마우스 아래의 사각형(없으면 nullptr)을 돌려줍니다. */
        const ProfilerTimelineRect* drawTimelineEvents( const vector<ProfilerTimelineThread>& listThread, const TimelineGeometry& geometry, const vector<float32>& listRowTop,
                                                        const vector<string>* pLoadedSlotName, bool bHovered );
        /** @brief 마우스 아래 구간의 이름 · 길이 · 같은 구간의 횟수 · 합계 · 최대를 툴팁으로 보입니다. */
        void drawTimelineTooltip( const vector<ProfilerTimelineThread>& listThread, const ProfilerTimelineRect& hoveredRect, const vector<string>* pLoadedSlotName );
#if SW_PROFILER_TRACE_ENABLED
        /** @brief 저장한 트레이스를 불러오고 지우는 행과 구간 이름 검색란을 그립니다. */
        void drawTraceControls();
        /** @brief 불러온 트레이스를 살아 있는 기록 아래에 겹쳐 그릴 목록으로 만듭니다. 첫 시각을 맞춰 겹칩니다. */
        void composeTimelineDraw();
#endif

        /** @brief 호출 트리 탭을 그립니다. 타임라인 녹화의 이벤트를 경로별로 합쳐 총 시간, 자기 시간, 호출 횟수를 보여 줍니다(유니티의 Profiler Hierarchy 에 해당). */
        void drawCallTreeTab();
        /** @brief 호출 트리 노드 하나와 그 자식을 테이블 행으로 그립니다. */
        void drawCallTreeNode( uint32 nodeIndex );
        /** @brief 새로 수집한 프레임의 GT.Frame 이 임계값을 넘었으면 수집을 멈추고 플레이를 일시정지합니다(Pause on spike). */
        void pauseOnSpike();
        /** @brief 캡처 파일 경로(`Saved/Profiler/ProfilerCapture.txt`)입니다. */
        static string getCaptureFilePath();

    public:
        /** @brief 프로브용입니다. 테이블이 보여 주는 프레임을 반환합니다. 0 이 가장 최근입니다. */
        uint32 getSelectedFrameOffset() const { return _rowQuery._frameOffset; }
        /** @brief 프로브용입니다. 수집을 멈췄으면 true 입니다. 스파이크를 잡았거나 캡처 파일을 열었을 때 멈춥니다. */
        bool isCapturePaused() const { return _bCapturePaused == SW_TRUE; }
        /** @brief 프로브용입니다. 수집한 프레임 수를 반환합니다. */
        uint64 getCapturedFrameCount() const { return _scopeHistory.getCapturedFrameCount(); }
        /** @brief 프로브용입니다. 마지막으로 그린 호출 트리의 노드 수를 반환합니다. */
        uint32 getCallTreeNodeCount() const { return _callTree.countLiveNodes(); }
#if SW_PROFILER_TRACE_ENABLED
        /** @brief 프로브용입니다. 불러온 트레이스의 스레드 수를 반환합니다. 없으면 0 입니다. */
        uint32 getLoadedTraceThreadCount() const { return _bLoadedTrace == SW_TRUE ? static_cast<uint32>( _loadedTrace._listThread.size() ) : 0; }
        /** @brief 프로브용입니다. 마지막으로 그린 타임라인에서 검색어와 일치한 이벤트 수를 반환합니다. 검색어가 없으면 모든 이벤트입니다. */
        uint32 getTraceSearchMatchCount() const { return _traceSearchMatched; }
        /** @brief 저장 폴더의 가장 최근 트레이스를 불러옵니다. 성공하면 true 입니다. */
        [[nodiscard]] bool loadLatestTrace();
        /** @brief 지정한 파일을 불러옵니다. 성공하면 true 입니다. "Load Trace" 단추와 `openTraceInTimeline` 이 같이 쓰는 한 길입니다. */
        [[nodiscard]] bool loadTraceFile( string_view path );
        /**
         * @brief Profiler 창을 열고 @p path 를 불러와 다음 그리기에서 Timeline 탭을 고릅니다. 창이 없거나 불러오지 못하면 false 입니다(이유는 로그 · 상태 줄에 남는다).
         * @details 멀티 프로세스 재생의 Stop 뒤에 합친 트레이스를 저절로 여는 곳이다(`EditorNetPlaySession::takeUnopenedMergedTrace`, 이 창의 프레임 단계가 부른다 — 창이 닫혀 있어도 돈다).
         */
        [[nodiscard]] static bool openTraceInTimeline( EditorPanelManager& panelManager, string_view path );
#endif

    private:
        /** @brief 활성 씬의 컴포넌트 분포 섹션을 그립니다. */
        void drawSceneDistributionSection();

    private:
        float32                        _arrFrameTimeHistory[120];
        uint32                         _historyOffset;
        Stopwatch                      _drawStopwatch; ///< 성능 탭을 마지막으로 그린 뒤의 벽시계 경과
        EditorResourceCatalogJob       _catalogJob;
        EditorResourceCatalogCounts    _catalogCounts;
        ProfilerScopeHistory           _scopeHistory;      ///< 최근 N 프레임을 담는 링 버퍼(ImGui 에 의존하지 않는다)
        ProfilerRowQuery               _rowQuery;          ///< 검색어와 정렬(종류는 탭이 정한다)
        vector<ProfilerScopeRow>       _listScratchRow;    ///< 테이블 행(프레임마다 다시 채운다)
        vector<float32>                _listScratchSeries; ///< 그래프 값(프레임마다 다시 채운다)
        utf8                           _arrFilter[constant::kMaxBuffer128];
        EditorTracyLaunchResult        _lastTracyResult;        ///< 마지막 "Open Tracy" 결과(패널에 한 줄로 보인다)
        vector<ProfilerTimelineThread> _listTimelineThread;     ///< 타임라인 이벤트(Freeze 면 그대로 둔다)
        vector<ProfilerTimelineRect>   _listTimelineRect;       ///< 배치 결과(프레임마다 다시 채운다)
        vector<uint16>                 _listTimelineLaneCount;  ///< 스레드마다 겹친 레인 수
        vector<uint64>                 _listTimelineFrameBegin; ///< 보이는 범위의 프레임 경계
        uint64                         _timelineWindowBegin;    ///< 수집한 구간(최근 N 프레임)
        uint64                         _timelineWindowEnd;
        uint64                         _timelineViewBegin; ///< 보이는 범위(확대와 이동의 결과)
        uint64                         _timelineViewEnd;
        uint32                         _timelineFrameCount; ///< 최근 프레임 수(1 · 4 · 16)
        ProfilerCallTreeWindow         _callTree;           ///< 최근 N 프레임의 호출 트리(새 프레임만 더하고 빠진 프레임은 뺀다, Freeze 면 그대로)
        vector<ProfilerTimelineThread> _listCallFrame;      ///< 호출 트리에 더할 한 프레임의 사건(다시 쓰는 버퍼)
        uint64                         _callTreeFrameEnd;   ///< 호출 트리에 마지막으로 더한 프레임의 끝(같으면 새 프레임이 없다)
        float64                        _callTreeOrderTime;  ///< 호출 트리의 정렬을 마지막으로 다시 지은 ImGui 시각(초)
        float32                        _spikeThresholdMs;   ///< Pause on spike 임계값(GT.Frame, ms)
        LeakWindowReport               _leakReport;         ///< Memory 탭이 마지막으로 닫은 누수 창의 보고서(`_bValid` 가 false 이면 아직 없다)
        uint8                          _bCatalogDirty   : 1;
        uint8                          _bCollect        : 1; ///< 엔진 프로파일러를 켜 두고 수집한다
        uint8                          _bTracyTried     : 1; ///< "Open Tracy" 를 한 번이라도 눌렀다
        uint8                          _bTimelineFrozen : 1; ///< Freeze 상태다. 녹화는 계속하고 화면만 멈춘다
        uint8                          _bTimelineZoomed : 1; ///< 사용자가 확대하거나 이동했다. 아니면 보이는 범위가 수집한 구간을 따라간다
        uint8                          _bPauseOnSpike   : 1; ///< GT.Frame 이 임계값을 넘으면 멈춘다
        uint8                          _bCapturePaused  : 1; ///< 수집을 멈췄다. 스파이크를 잡았거나 캡처 파일을 열었을 때다
        uint8                          _bCallTreeFrozen : 1; ///< 호출 트리를 다시 합치지 않는다
#if SW_PROFILER_TRACE_ENABLED
        ProfilerLoadedTrace            _loadedTrace;         ///< 불러온 트레이스. `_bLoadedTrace` 일 때만 의미가 있다
        vector<ProfilerTimelineThread> _listTimelineDraw;    ///< 이번 프레임에 그릴 스레드(살아 있는 것과 불러온 것, 프레임마다 다시 채운다)
        vector<uint64>                 _listLoadedFrameDraw; ///< 불러온 트레이스의 프레임 경계(살아 있는 기록의 시각으로 변환한 것)
        string                         _traceStatus;         ///< 마지막 불러오기 결과 한 줄
        utf8                           _arrTraceSearch[constant::kMaxBuffer128];
        uint32                         _traceSearchMatched;
        uint8                          _bLoadedTrace       : 1;
        uint8                          _bSelectTimelineTab : 1; ///< 다음 그리기에서 Timeline 탭을 고른다(`openTraceInTimeline`)
#endif
    };
} // namespace sw::editor
