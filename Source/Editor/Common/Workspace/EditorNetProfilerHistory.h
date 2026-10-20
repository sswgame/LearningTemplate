/**
 * @file EditorNetProfilerHistory.h
 * @brief Multiplayer 창 Profiler 탭(서버 디버깅 S5)의 시계열과 메시지 종류별 비중 계산입니다. ImGui 에 의존하지 않습니다.
 * @details 자식 프로세스마다 `net.stats` 를 0.5 초 간격으로 묻고, 응답(`NetStatsReport`)이 올 때마다 표본 하나를 링 버퍼에 넣는다.
 *          누적 값(패킷 수, 재전송)은 앞 보고와의 차이를 시간으로 나눠 초당 값으로 바꾸고, 대역폭과 RTT, 손실은 보고 값을 그대로 쓴다.
 *          프레임에 수집 비용을 더하지 않는다. 자식은 이미 세고 있는 연결 통계와 라우터 집계를 한 줄로 옮길 뿐이고, 묻는 일은 탭이 보일 때와
 *          `netplay.profile` 동안만 한다. 언리얼의 Network Insights, 유니티의 Netcode Runtime Stats Monitor 에 해당한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/NetStatsReport.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    class EditorNetPlaySession;

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
        LossPercent,              ///< 연결들이 잰 손실 중 가장 큰 값
        Count
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 시계열의 표본 하나입니다. */
    struct EditorNetProfilerSample
    {
        float64 _timeSeconds{ 0.0 }; ///< 보고를 받은 시각(단조 시계, 초)
        float32 _arrValue[static_cast<size_t>( EditorNetProfilerChannel::Count )]{};
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
    /**
     * @class EditorNetProfilerSeries
     * @brief 표본을 정해진 수만큼 보관하는 링 버퍼입니다. 가득 차면 가장 오래된 표본을 덮어씁니다.
     */
    class EditorNetProfilerSeries
    {
    public:
        /** @brief 기본 용량입니다. 0.5 초 간격이면 2 분 분량입니다. */
        static constexpr int32 kDefaultCapacity = 240;

        explicit EditorNetProfilerSeries( int32 capacity = kDefaultCapacity );

        void push( const EditorNetProfilerSample& sample );
        void clear();

        int32 getCount() const { return _count; }
        int32 getCapacity() const { return static_cast<int32>( _listSample.size() ); }
        /** @brief @p index 번째 표본입니다(0 = 가장 오래된 것). 범위 밖이면 assert 합니다. */
        const EditorNetProfilerSample& getSample( int32 index ) const;
        /** @brief 가장 최근 표본입니다. 비었으면 nullptr 입니다. */
        const EditorNetProfilerSample* findLatest() const;
        /** @brief 한 필드의 값을 오래된 것부터 @p outListValue 에 다시 씁니다. 그래프를 그릴 때 씁니다. */
        void copyChannel( EditorNetProfilerChannel channel, vector<float32>& outListValue ) const;
        /** @brief 한 필드의 가장 큰 값입니다. 비었으면 0 입니다. */
        float32 computePeak( EditorNetProfilerChannel channel ) const;

    private:
        vector<EditorNetProfilerSample> _listSample;
        int32                           _nextIndex; ///< 다음에 쓸 슬롯
        int32                           _count;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 프로세스 하나의 시계열과 마지막 보고입니다. */
    struct EditorNetProfilerTrack
    {
        EditorNetProfilerSeries _series{};
        NetStatsReport          _lastReport{};
        float64                 _lastTimeSeconds{ 0.0 };
        uint32                  _seenReportCount{ 0 }; ///< 세션이 받은 보고 수 가운데 이미 넣은 것(`EditorNetPlayProcessStatus::_statsReportCount`)
        bool                    _bHasReport{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 시계열과 비중의 계산입니다. 탭과 테스트가 같이 씁니다. */
    struct EditorNetProfilerUtil
    {
        /** @brief 자식 프로세스에 보고를 묻는 간격(초)입니다. 응답 줄이 자식 로그에도 남으므로 느리게 묻습니다. */
        static constexpr float64 kQueryIntervalSeconds = 0.5;
        /** @brief 자식 프로세스에 보고를 묻는 명령입니다(`GameNetSession` 의 개발 명령). */
        static constexpr const utf8* kStatsCommand = "net.stats";

        /**
         * @brief 보고 하나를 표본으로 바꿉니다. 초당 값은 @p pPrevious 와의 차를 시간으로 나눈 값이고, 앞 보고가 없거나 시간이 흐르지 않았거나
         *        누적 값이 줄었으면(다시 연결해 연결 통계가 새로 시작) 0 입니다.
         */
        static EditorNetProfilerSample makeSample( const NetStatsReport* pPrevious, float64 previousTimeSeconds, const NetStatsReport& report, float64 timeSeconds );
        /** @brief 여러 보고의 종류별 집계를 종류마다 합치고 바이트 비율을 계산해, 바이트가 큰 순서로 @p outListShare 에 다시 씁니다. */
        static void computeKindShares( const vector<NetStatsKindEntry>& listEntry, vector<EditorNetKindShare>& outListShare );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorNetProfilerHistory
     * @brief 멀티 프로세스 재생 세션의 프로세스마다 시계열을 듭니다. 게임(UI) 스레드에서만 씁니다.
     * @details 세션이 새로 서면(포트가 바뀌면) 지운다. 세션이 멈추면 마지막 시계열을 그대로 둔다(멈춘 뒤에도 본다).
     */
    class EditorNetProfilerHistory
    {
    public:
        EditorNetProfilerHistory();

        /** @brief 간격이 지났으면 돌고 있는 프로세스마다 `net.stats` 를 묻고, 세션에 새로 온 보고를 표본으로 넣습니다. */
        void update( EditorNetPlaySession& session, float64 nowSeconds );
        /** @brief 프로세스 @p processIndex 의 보고 하나를 표본으로 넣습니다. 그 프로세스의 슬롯이 없으면 늘립니다. */
        void ingest( int32 processIndex, const NetStatsReport& report, float64 timeSeconds );
        void clear();

        int32                         getTrackCount() const { return static_cast<int32>( _listTrack.size() ); }
        const EditorNetProfilerTrack* findTrack( int32 processIndex ) const;
        /** @brief 모든 프로세스의 표본 수 합입니다(탐침 `Editor.NetPlayProfilerSamples`). */
        int32 computeSampleCount() const;
        /** @brief 모든 프로세스 가운데 한 필드의 가장 큰 값입니다. */
        float32 computePeak( EditorNetProfilerChannel channel ) const;
        /** @brief 모든 프로세스의 마지막 보고의 종류별 셈을 @p outListEntry 뒤에 붙입니다(`computeKindShares` 의 입력). */
        void collectKindEntries( vector<NetStatsKindEntry>& outListEntry ) const;

    private:
        vector<EditorNetProfilerTrack> _listTrack;
        float64                        _lastQuerySeconds;
        uint16                         _sessionPort; ///< `_listTrack` 이 맞춰진 세션(포트)
        bool                           _bQueried;    ///< 이 세션에 한 번이라도 물었다
    };
} // namespace sw::editor
