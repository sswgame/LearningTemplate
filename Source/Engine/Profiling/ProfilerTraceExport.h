/**
 * @file ProfilerTraceExport.h
 * @brief `ProfilerTimeline` 의 사건을 Chrome Trace Event 형식(JSON)으로 바꾸고 저장합니다. `chrome://tracing` 과 Perfetto UI 가 읽습니다.
 * @note Shipping 에서는 통째로 빠집니다(`SW_PROFILER_TRACE_ENABLED` 0 — 헤더는 비고 소스도 컴파일되지 않는다).
 * @details 변환은 저장하는 순간에만 합니다 — 링 버퍼를 복사해 문자열로 바꾸므로 프레임당 비용이 없습니다.
 *          구간은 완전 이벤트(`X`, 시작 · 길이 마이크로초), 스레드 · 프로세스 이름은 메타 이벤트(`M`), 프레임 경계는 인스턴트 이벤트(`i`)입니다.
 *          파일 머리말(`otherData`)에 저장 시점의 벽시계와 단조 시계 쌍을 남겨, 프로세스마다 저장한 파일을 시간축으로 맞출 수 있게 합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Profiling/ProfilerTimeline.h"

#if SW_PROFILER_TRACE_ENABLED

namespace sw
{
    class FrameProfiler;

    /** @brief 트레이스 파일 머리말에 들어가는 프로세스 정보입니다. */
    struct ProfilerTraceMeta
    {
        string _processName;
        int32  _processID{ 0 };
        int64  _wallUnixMilliseconds{ 0 }; ///< 저장 시점의 UTC 유닉스 시각 — 아래 단조 시계와 한 쌍입니다
        uint64 _monotonicNanoseconds{ 0 }; ///< 저장 시점의 단조 시계(`MonotonicClock::nowNanoseconds`)
        bool   _bTruncated{ false };       ///< 긴 캡처가 예산을 넘어 뒤를 담지 못했다 — 머리말과 전역 표식으로 알린다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ProfilerTraceExport
     * @brief 타임라인 사건 → Trace Event JSON 변환기와 저장 창구입니다(전부 static).
     */
    class SW_API ProfilerTraceExport
    {
    public:
        /** @brief 기본 파일 확장자입니다. */
        static constexpr const utf8* kFileExtension = ".trace.json";

        /**
         * @brief 모은 스레드 사건을 Trace Event JSON 문자열로 바꿉니다(순수 함수).
         * @param listThread 스레드마다 사건(`ProfilerTimeline::collectRecentFrames` 결과)
         * @param listFrameBegin 프레임 시작 시각(나노초) 목록
         * @param listSlotName 슬롯 번호로 찾는 구간 이름. 범위 밖이거나 빈 이름이면 `slot <번호>` 로 냅니다
         * @param meta 프로세스 이름 · 시계 쌍
         * @return JSON 문자열. 시각은 마이크로초(소수 셋째 자리까지)이고 단조 시계 값을 그대로 씁니다.
         */
        static string makeTraceJSON( const vector<ProfilerTimelineThread>& listThread, const vector<uint64>& listFrameBegin,
                                     const vector<string>& listSlotName, const ProfilerTraceMeta& meta );

        /** @brief 긴 캡처의 스레드당 사건 수 기본 예산입니다(사건 24 B — 스레드마다 약 24 MB). */
        static constexpr uint32 kDefaultEventBudgetPerThread = 1000000;

        /**
         * @brief @p seconds 초 동안 처음부터 모아 끝나면 저장하는 긴 캡처를 시작합니다(링 버퍼의 최근 몇 프레임과 따로).
         * @param budget 스레드당 사건 수 상한(0 이면 기본값). 넘으면 거기서 멈추고 잘림 표지를 남깁니다 — 오래된 것을 버리지 않습니다.
         * @details 끝은 `tickRecording` 이 알아챕니다(`EngineLoop::endFrame` 이 프레임마다 부른다). 이미 진행 중이면 거절합니다.
         * @return 시작했으면 true
         */
        [[nodiscard]] static bool startRecording( FrameProfiler& profiler, float64 seconds, string_view path, string_view processName, uint32 budget,
                                                  string& outReply );
        /** @brief 긴 캡처가 진행 중이면 true 입니다. */
        static bool isRecording();
        /** @brief 긴 캡처의 시간이 다 됐으면 멈추고 저장합니다. 진행 중이 아니면 atomic 하나 읽고 돌아옵니다. 게임 스레드가 부릅니다. */
        static void tickRecording( FrameProfiler& profiler );

        /**
         * @brief 최근 @p frameCount 프레임을 모아 @p path 에 저장합니다.
         * @param path 비면 `<프로젝트>/Saved/Traces/trace_<유닉스 밀리초>.trace.json`
         * @param outSavedPath 실제로 쓴 경로
         * @param outReply 실패 이유 또는 사건 · 스레드 수 요약
         * @return 쓰기에 성공하면 true. 기록된 프레임이 둘 미만이면 false(타임라인 기록이 꺼져 있었다).
         */
        [[nodiscard]] static bool saveRecentFrames( const FrameProfiler& profiler, uint32 frameCount, string_view path, string_view processName,
                                                    string& outSavedPath, string& outReply );
    };
} // namespace sw

#endif // SW_PROFILER_TRACE_ENABLED
