/**
 * @file ProfilerTimelineModel.h
 * @brief 프로파일러 패널 Timeline · Call Tree 탭의 상태와 명령입니다 — 최근 프레임 모으기, Freeze, 보이는 범위(확대 · 이동), 배치, 트레이스 저장 · 불러오기 · 검색, 호출 트리(ImGui 없음).
 * @details 두 탭은 같은 녹화(`ProfilerTimeline`)와 같은 최근 프레임 수를 쓴다. 그리기와 입력 읽기는 `ProfilerPanel` 이 하고, 여기에는 화면 좌표가 아닌
 *          픽셀 폭 · 배율만 들어온다. 긴 시간축 분석은 외부 Tracy 뷰어가 맡는다(언리얼 Insights · 유니티 Profiler Timeline 의 빠른 확인용 자리).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/ProfilerTimeline.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"

#include "Editor/Panels/Profiler/Model/ProfilerCallTree.h"
#include "Editor/Panels/Profiler/Model/ProfilerTimelineLayout.h"

#include "Engine/Profiling/ProfilerTraceExport.h"

namespace sw
{
    class FrameProfiler;
} // namespace sw

namespace sw::editor
{
    /**
     * @class ProfilerTimelineModel
     * @brief 타임라인과 호출 트리가 보는 녹화 사건, 보이는 범위, 불러온 트레이스를 가집니다.
     */
    class ProfilerTimelineModel
    {
    public:
        /** @brief 탭에서 고를 수 있는 최근 프레임 수입니다. */
        static constexpr uint32 kArrFrameChoice[] = { 1, 4, 16 };
        /** @brief 불러온 트레이스 구간의 슬롯 번호가 시작하는 값입니다. 살아 있는 프로파일러 슬롯(`FrameProfiler::kMaxScope` 미만)과 겹치지 않습니다. */
        static constexpr uint32 kLoadedSlotBase = 1u << 20;
        /** @brief 호출 트리의 형제 정렬 · 루트 목록을 다시 짓는 주기(초)입니다. 합은 프레임마다 맞고 순서만 이 주기로 바뀐다(유니티 Hierarchy 처럼 줄이 매 프레임 튀지 않는다). */
        static constexpr float64 kCallTreeOrderSeconds = 0.25;

        ProfilerTimelineModel();

        /** @brief 슬롯에서 구간 이름을 찾습니다. 불러온 트레이스의 슬롯은 `kLoadedSlotBase` 이상이므로 그 표를 봅니다. 없으면 nullptr 입니다. */
        static const utf8* findSlotName( const FrameProfiler* pProfiler, const vector<string>* pLoadedSlotName, uint32 slot );

        // ------------------------------------------------------------------------------
        // 1) 최근 프레임 수 · 녹화 · Freeze
        // ------------------------------------------------------------------------------
        uint32 getFrameCount() const { return _frameCount; }
        /** @brief 최근 프레임 수를 바꾸고 보이는 범위를 수집 구간에 맞춥니다. */
        void setFrameCount( uint32 frameCount );
        /** @brief 타임라인 녹화를 새로 시작합니다. 지난 사건을 비우고 보이는 범위를 수집 구간에 맞춥니다. 계측 켜기는 부르는 쪽(수집 모델)이 합니다. */
        void beginTimelineRecording( ProfilerTimeline& timeline );
        bool isTimelineFrozen() const { return _bTimelineFrozen == SW_TRUE; }
        /** @brief Freeze 면 녹화는 계속하고 화면만 멈춥니다. */
        void setTimelineFrozen( bool bFrozen ) { _bTimelineFrozen = bFrozen ? SW_TRUE : SW_FALSE; }
        /** @brief 보이는 범위가 다시 수집 구간을 따라가게 합니다. */
        void fit() { _bTimelineZoomed = SW_FALSE; }

        // ------------------------------------------------------------------------------
        // 2) 이번 프레임에 그릴 것
        // ------------------------------------------------------------------------------
        /** @brief 최근 프레임 사건을 모으고(Freeze 가 아니면), 불러온 트레이스를 겹치고, 보이는 범위의 프레임 경계를 모읍니다. 그리기 전에 부릅니다. */
        void collect( const ProfilerTimeline& timeline );
        /** @brief 이번 프레임에 그릴 스레드입니다. 살아 있는 기록에 불러온 트레이스를 겹친 것입니다. */
        const vector<ProfilerTimelineThread>& getDrawThreads() const;
        /** @brief 불러온 트레이스의 구간 이름 표입니다. 없으면 nullptr 이며, `kLoadedSlotBase` 이상인 슬롯이 이 표를 가리킵니다. */
        const vector<string>* getLoadedSlotNames() const;
        /** @brief 보이는 범위의 프레임 경계입니다(`collect` 뒤). */
        const vector<uint64>& getFrameBegins() const { return _listFrameBegin; }
        uint64                getViewBegin() const { return _viewBegin; }
        uint64                getViewEnd() const { return _viewEnd; }

        /** @brief 보이는 범위의 사건을 폭 @p trackWidth 픽셀에 배치합니다. 결과는 `getRects` · `getLaneCounts` 입니다. */
        void                                layout( float32 trackWidth );
        const vector<ProfilerTimelineRect>& getRects() const { return _listRect; }
        /** @brief 스레드마다 겹친 레인 수입니다(`layout` 뒤, `getDrawThreads` 와 같은 순서). */
        const vector<uint16>& getLaneCounts() const { return _listLaneCount; }

        /** @brief 트랙 왼쪽에서 @p pivotX 픽셀인 점을 중심으로 @p factor 배 확대합니다(1 보다 작으면 확대). */
        void zoom( float32 pivotX, float32 trackWidth, float32 factor );
        /** @brief @p deltaPixels 만큼 보이는 범위를 옮깁니다. */
        void pan( float32 deltaPixels, float32 trackWidth );

        // ------------------------------------------------------------------------------
        // 3) 트레이스 저장 · 불러오기 · 검색
        // ------------------------------------------------------------------------------
#if SW_PROFILER_TRACE_ENABLED
        /** @brief 최근 프레임을 `Saved/Traces` 에 씁니다. 결과를 로그에 남깁니다. */
        void saveTrace( const FrameProfiler& profiler ) const;
        /** @brief 지정한 파일을 불러옵니다. 성공하면 true 이며, 결과 한 줄은 `getTraceStatus` 에 남습니다. */
        [[nodiscard]] bool loadTraceFile( string_view path );
        /** @brief 저장 폴더의 가장 최근 트레이스를 불러옵니다. 성공하면 true 입니다. */
        [[nodiscard]] bool loadLatestTrace();
        /** @brief 불러온 트레이스를 내립니다. */
        void unloadTrace();
        bool hasLoadedTrace() const { return _bLoadedTrace == SW_TRUE; }
        /** @brief 불러온 트레이스의 스레드 수입니다. 없으면 0 입니다. */
        uint32        getLoadedTraceThreadCount() const { return _bLoadedTrace == SW_TRUE ? static_cast<uint32>( _loadedTrace._listThread.size() ) : 0; }
        const string& getTraceStatus() const { return _traceStatus; }
        /** @brief 구간 이름 검색란의 버퍼입니다(검색란이 직접 고친다). */
        utf8*       editTraceSearch() { return _arrTraceSearch; }
        const utf8* getTraceSearch() const { return _arrTraceSearch; }
        /** @brief 검색어가 있고 @p pName 이 그것을 담지 않으면 false 입니다. 검색어가 없으면 모두 맞습니다. */
        bool matchesTraceSearch( const utf8* pName ) const;
        /** @brief 마지막으로 그린 타임라인에서 검색어와 맞은 사건 수입니다(그리는 쪽이 센다). */
        uint32 getTraceSearchMatchCount() const { return _traceSearchMatched; }
        void   setTraceSearchMatchCount( uint32 matchCount ) { _traceSearchMatched = matchCount; }
        /** @brief 다음 그리기에서 Timeline 탭을 고르게 합니다. */
        void requestTimelineTab() { _bSelectTimelineTab = SW_TRUE; }
        /** @brief Timeline 탭을 골라야 하면 true 를 한 번 반환합니다. */
        bool consumeTimelineTabRequest();
#endif

        // ------------------------------------------------------------------------------
        // 4) 호출 트리
        // ------------------------------------------------------------------------------
        /** @brief 호출 트리 녹화를 새로 시작합니다. 지난 사건과 합친 트리를 비웁니다. */
        void beginCallTreeRecording( ProfilerTimeline& timeline );
        bool isCallTreeFrozen() const { return _bCallTreeFrozen == SW_TRUE; }
        void setCallTreeFrozen( bool bFrozen ) { _bCallTreeFrozen = bFrozen ? SW_TRUE : SW_FALSE; }
        /**
         * @brief 끝난 마지막 프레임을 호출 트리에 더하고, 정렬 주기가 되었으면 순서를 다시 짓습니다. Freeze 면 프레임 수만 맞춥니다.
         * @param nowSeconds 지금 시각(초). 정렬 주기를 잰다.
         */
        void                          updateCallTree( const ProfilerTimeline& timeline, float64 nowSeconds );
        const ProfilerCallTreeWindow& getCallTree() const { return _callTree; }

    private:
#if SW_PROFILER_TRACE_ENABLED
        /** @brief 불러온 트레이스를 살아 있는 기록 아래에 겹쳐 그릴 목록으로 만듭니다. 첫 시각을 맞춰 겹칩니다. */
        void composeDraw();
#endif
        /** @brief 확대하지 않았으면 보이는 범위를 수집 구간에 맞춥니다. */
        void followWindow();

    private:
        vector<ProfilerTimelineThread> _listThread;     ///< 최근 프레임 사건(Freeze 면 그대로 둔다)
        vector<ProfilerTimelineRect>   _listRect;       ///< 배치 결과(프레임마다 다시 채운다)
        vector<uint16>                 _listLaneCount;  ///< 스레드마다 겹친 레인 수
        vector<uint64>                 _listFrameBegin; ///< 보이는 범위의 프레임 경계
        uint64                         _windowBegin;    ///< 수집한 구간(최근 N 프레임)
        uint64                         _windowEnd;
        uint64                         _viewBegin; ///< 보이는 범위(확대와 이동의 결과)
        uint64                         _viewEnd;
        uint32                         _frameCount;          ///< 최근 프레임 수(1 · 4 · 16)
        ProfilerCallTreeWindow         _callTree;            ///< 최근 N 프레임의 호출 트리(새 프레임만 더하고 빠진 프레임은 뺀다, Freeze 면 그대로)
        vector<ProfilerTimelineThread> _listCallFrame;       ///< 호출 트리에 더할 한 프레임의 사건(다시 쓰는 버퍼)
        uint64                         _callTreeFrameEnd;    ///< 호출 트리에 마지막으로 더한 프레임의 끝(같으면 새 프레임이 없다)
        float64                        _callTreeOrderTime;   ///< 호출 트리의 정렬을 마지막으로 다시 지은 시각(초)
        uint8                          _bTimelineFrozen : 1; ///< Freeze 상태다. 녹화는 계속하고 화면만 멈춘다
        uint8                          _bTimelineZoomed : 1; ///< 사용자가 확대하거나 이동했다. 아니면 보이는 범위가 수집한 구간을 따라간다
        uint8                          _bCallTreeFrozen : 1; ///< 호출 트리를 다시 합치지 않는다
#if SW_PROFILER_TRACE_ENABLED
        ProfilerLoadedTrace            _loadedTrace;         ///< 불러온 트레이스. `_bLoadedTrace` 일 때만 의미가 있다
        vector<ProfilerTimelineThread> _listDraw;            ///< 이번 프레임에 그릴 스레드(살아 있는 것과 불러온 것, 프레임마다 다시 채운다)
        vector<uint64>                 _listLoadedFrameDraw; ///< 불러온 트레이스의 프레임 경계(살아 있는 기록의 시각으로 변환한 것)
        string                         _traceStatus;         ///< 마지막 불러오기 결과 한 줄
        utf8                           _arrTraceSearch[constant::kMaxBuffer128];
        uint32                         _traceSearchMatched;
        uint8                          _bLoadedTrace       : 1;
        uint8                          _bSelectTimelineTab : 1; ///< 다음 그리기에서 Timeline 탭을 고른다(`ProfilerPanel::openTraceInTimeline`)
#endif
    };
} // namespace sw::editor
