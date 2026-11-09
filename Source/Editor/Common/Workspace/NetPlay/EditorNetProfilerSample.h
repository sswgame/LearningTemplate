/**
 * @file EditorNetProfilerSample.h
 * @brief Multiplayer 창 Profiler 탭(서버 디버깅 S5)의 표본 값 형식입니다 — 채널 열거, 표본 하나, 메시지 종류별 비중, 복제 셈의 초당 값과 그 계산. ImGui 에 의존하지 않습니다.
 * @details 시계열 · 세션을 모르는 값 헤더라 Profiler 탭의 ViewState 가 이 형식을 그대로 든다(`EditorNetProfilerHistory.h` 가 시계열을 들고 이 헤더를 include 한다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetStatsReport.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /** @brief 표본 하나의 필드입니다. 그래프 하나에 해당합니다. */
    ENUM( Count = Count )
    enum class EditorNetProfilerChannel : uint8
    {
        SentBytesPerSecond = 0,   ///< 최근 1 초 보낸 바이트(연결 대역폭의 합)
        ReceivedBytesPerSecond,   ///< 최근 1 초 받은 바이트
        SentPacketsPerSecond,     ///< 두 보고 사이에 보낸 패킷 / 초
        ReceivedPacketsPerSecond, ///< 두 보고 사이에 받은 패킷 / 초
        ResentMessagesPerSecond,  ///< 두 보고 사이에 다시 보낸 신뢰 메시지 / 초
        RttMilliseconds,          ///< 연결들의 평활 RTT 중 가장 큰 값
        LossPercent,              ///< 연결들이 측정한 손실 중 가장 큰 값
        Count
    };
} // namespace sw::editor

namespace sw::editor
{
    struct EditorNetKindShare;
    struct EditorNetReplicaRate;

    /** @brief 시계열의 표본 하나입니다. */
    struct EditorNetProfilerSample
    {
        float64 _timeSeconds{ 0.0 }; ///< 보고를 받은 시각(단조 시계, 초)
        float32 _arrValue[static_cast<size_t>( EditorNetProfilerChannel::Count )]{};

        // 시계열과 비중의 계산입니다. 탭과 테스트가 같이 씁니다.
        /** @brief 자식 프로세스에 보고를 묻는 간격(초)입니다. 응답 줄이 자식 로그에도 남으므로 느리게 묻습니다. */
        static constexpr float64 kQueryIntervalSeconds = 0.5;
        /** @brief 자식 프로세스에 보고를 묻는 명령입니다(`GameNetSession` 의 개발 명령). */
        static constexpr const utf8* kStatsCommand = "net.stats";

        /**
         * @brief 누적 값 둘의 차를 초당 값으로 바꿉니다. 줄었거나(연결 통계가 새로 시작) 시간이 흐르지 않았으면 0 입니다.
         * @details 패킷 · 재전송 · 복제 셈이 모두 이 하나로 초당 값을 낸다(누적 값을 읽는 약속이 둘로 갈라지지 않게).
         */
        static float32 computeRate( uint64 previous, uint64 current, float64 elapsedSeconds );
        /**
         * @brief 보고 하나를 표본으로 바꿉니다. 초당 값은 @p pPrevious 와의 차를 시간으로 나눈 값이고, 앞 보고가 없거나 시간이 흐르지 않았거나
         *        누적 값이 줄었으면(다시 연결해 연결 통계가 새로 시작) 0 입니다.
         */
        static EditorNetProfilerSample makeSample( const NetStatsReport* pPrevious, float64 previousTimeSeconds, const NetStatsReport& report, float64 timeSeconds );
        /** @brief 여러 보고의 종류별 집계를 종류마다 합치고 바이트 비율을 계산해, 바이트가 큰 순서로 @p outListShare 에 다시 씁니다. */
        static void computeKindShares( const vector<NetStatsKindEntry>& listEntry, vector<EditorNetKindShare>& outListShare );
        /**
         * @brief 복제 셈(누적)의 두 보고 차를 초당 값으로 바꿔 초당 바이트가 큰 순서로 @p outListRate 에 다시 씁니다.
         * @details 앞 보고에 없던 이름(상위 목록에 새로 든 것)과 줄어든 값은 0 입니다 — 기준선이 없는 값을 누적 전체로 읽지 않는다. 시간이 흐르지 않았으면 비웁니다.
         */
        static void computeReplicaRates( const vector<NetStatsReplicaEntry>& listPrevious, const vector<NetStatsReplicaEntry>& listCurrent, float64 elapsedSeconds,
                                         vector<EditorNetReplicaRate>& outListRate );
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 메시지 종류 하나의 비중입니다(받은 바이트 기준). */
    struct EditorNetKindShare
    {
        uint64  _messageCount{ 0 };
        uint64  _byteCount{ 0 };
        float32 _share{ 0.0f }; ///< 모든 종류의 바이트 합에 대한 비율(0~1)
        uint8   _kind{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 복제 셈 하나의 초당 값입니다(`replicas=` 의 누적 값 두 보고의 차). */
    struct EditorNetReplicaRate
    {
        string  _name{};
        float32 _updatesPerSecond{ 0.0f };
        float32 _bytesPerSecond{ 0.0f };
        float32 _share{ 0.0f }; ///< 모든 이름의 초당 바이트 합에 대한 비율(0~1)
    };
} // namespace sw::editor
