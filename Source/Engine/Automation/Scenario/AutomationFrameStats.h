/**
 * @file AutomationFrameStats.h
 * @brief 시나리오 구간의 프레임 시간 기록 — 내장 벤치마크(`<BeginFrameStats/>` … `<EndFrameStats file="bench.json"/>`)입니다.
 * @details Source · id Tech 의 `timedemo` 와 게임 내장 벤치마크(정해진 카메라 경로 + 프레임 통계)의 자리다. 고정 프레임 시간으로 도는 시나리오도 실제로 걸린 **벽시계** 시간을 잰다.
 *          실행기가 프레임 끝마다 `onFrameEnd` 를 부르고, 기록은 시작 단계와 끝 단계 사이만 쌓인다. 결과 JSON 은 산출물 폴더에 쓴다(`resolveOutputPath`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @struct FrameStatsSummary @brief 프레임 시간(밀리초) 묶음의 요약입니다. */
    struct FrameStatsSummary
    {
        uint32  _frameCount{ 0 };
        float64 _averageMilliseconds{ 0.0 };
        float64 _minMilliseconds{ 0.0 };
        float64 _maxMilliseconds{ 0.0 };
        float64 _p50Milliseconds{ 0.0 };
        float64 _p95Milliseconds{ 0.0 };
        float64 _p99Milliseconds{ 0.0 };
        float64 _averageFramesPerSecond{ 0.0 };
    };

    /** @class AutomationFrameStats @brief 프레임 시간 기록기입니다. 게임 스레드에서만 쓴다. */
    class SW_API AutomationFrameStats
    {
    public:
        /** @brief 이 프레임부터 기록합니다(앞 기록은 버린다). */
        void begin();
        /** @brief 기록을 멈추고 요약합니다. 기록 중이 아니었으면 false 입니다. */
        [[nodiscard]] bool end( FrameStatsSummary& outSummary );
        /** @brief 실행기가 프레임 끝마다 부릅니다 — 기록 중이면 앞 프레임 끝에서 지금까지의 시간을 쌓는다. */
        void onFrameEnd();
        bool isRecording() const { return _bRecording; }

        /** @brief 프레임 시간(밀리초) 목록을 요약합니다(백분위는 가장 가까운 순위 — 정렬한 목록의 ceil(p × n) 번째). 빈 목록이면 모두 0 입니다. */
        static FrameStatsSummary summarize( vector<float64> listMilliseconds );
        /** @brief 요약을 JSON 글로 씁니다(보고 · 비교 도구가 읽는다). */
        static string toJSON( const FrameStatsSummary& summary, string_view scenarioName );

    private:
        vector<float64> _listFrameMilliseconds{};
        int64           _lastFrameEndNanoseconds{ 0 };
        bool            _bRecording{ false };
    };
} // namespace sw
