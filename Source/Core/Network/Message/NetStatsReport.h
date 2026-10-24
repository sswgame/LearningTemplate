/**
 * @file NetStatsReport.h
 * @brief 프로세스 하나의 연결 · 트래픽 수치를 한 줄로 주고받는 형식입니다(개발 명령 `net.stats` 의 답, 에디터 Multiplayer 창 Profiler 탭이 읽는다).
 * @details 쓰는 쪽(게임 세션)과 읽는 쪽(에디터 세션 · 탐침)이 이 정의 하나를 쓴다. 값은 이미 있는 셈을 옮길 뿐이다 —
 *          연결 통계(`NetConnectionStats`: 패킷 수 · 최근 1 초 대역폭 · RTT · 손실 · 재전송)와 라우터의 종류별 셈(`NetMessageKindTally`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class NetMessageKindTallyTable;

    /** @brief 메시지 종류 하나의 셈입니다(보고 줄의 `kinds=`(받은 쪽) · `sentKinds=`(보낸 쪽) 칸 한 항목). */
    struct NetStatsKindEntry
    {
        uint64 _messageCount{ 0 };
        uint64 _byteCount{ 0 };
        uint8  _kind{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 메시지 종류별 셈의 방향입니다. */
    enum class NetStatsKindDirection : uint8
    {
        Received = 0, ///< 라우터가 꺼낸 메시지(`kinds=`)
        Sent          ///< 호스트가 보내기 큐에 넣은 메시지(`sentKinds=`)
    };

    /**
     * @brief 복제 키트가 올리는 셈 하나입니다(보고 줄의 `replicas=<이름>:<갱신 수>:<바이트>,…` 한 항목).
     * @details 공통 원천이 없는 값(스냅숏 크기 · 오브젝트별 복제 빈도)을 키트가 이름 붙여 싣는 창구다. 이름은 `[a-z0-9_.-]` 글자만이다(줄 형식의 구분자를 쓰지 못하게).
     */
    struct NetStatsReplicaEntry
    {
        string _name{};
        uint64 _updateCount{ 0 }; ///< 보낸 갱신(스냅숏 · 델타)의 누적 수
        uint64 _byteCount{ 0 };   ///< 그 갱신들의 누적 바이트
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NetStatsReport
     * @brief `kLinePrefix connections=… sentPackets=… receivedPackets=… sentBps=… receivedBps=… rttMs=… lossPercent=… resent=… kinds=<종류>:<수>:<바이트>,… sentKinds=…` 한 줄입니다.
     * @details 패킷 수 · 재전송 · 종류별 셈은 누적값이고(읽는 쪽이 두 보고의 차로 비율을 낸다), 바이트는 연결이 이미 세는 최근 1 초 대역폭(초당 바이트)이다.
     *          RTT · 손실은 연결들 가운데 가장 큰 값이다.
     */
    struct SW_API NetStatsReport
    {
        /** @brief 줄의 머리입니다. 로그 줄 안 어디에 있어도 찾습니다(앞에 로그 꼬리표 · 명령 메아리가 붙는다). */
        static constexpr const utf8* kLinePrefix = "Net traffic stats";
        /** @brief 줄에 싣는 메시지 종류의 상한입니다. 바이트가 큰 종류부터 싣습니다. */
        static constexpr int32 kMaxKindEntry = 8;
        /** @brief 줄에 싣는 복제 셈의 상한입니다. */
        static constexpr int32 kMaxReplicaEntry = 8;
        /** @brief 복제 셈의 이름으로 쓸 수 있는가(비어 있지 않고 `[a-z0-9_.-]` 만)입니다. */
        [[nodiscard]] static bool isValidReplicaName( string_view name );

        vector<NetStatsKindEntry>    _listKindEntry{};            ///< 받은 메시지 — 바이트가 큰 순서
        vector<NetStatsKindEntry>    _listSentKindEntry{};        ///< 보낸 메시지(큐에 넣은 것) — 바이트가 큰 순서
        vector<NetStatsReplicaEntry> _listReplicaEntry{};         ///< 복제 키트가 올린 셈 — 바이트가 큰 순서, 최대 `kMaxReplicaEntry` 개
        uint64                       _sentPacketCount{ 0 };       ///< 연결들이 보낸 패킷의 합(누적)
        uint64                       _receivedPacketCount{ 0 };   ///< 연결들이 받은 패킷의 합(누적)
        uint64                       _resentMessageCount{ 0 };    ///< 연결들이 다시 보낸 신뢰 메시지의 합(누적)
        float32                      _sentBytesPerSecond{ 0.0f }; ///< 연결들의 최근 1 초 보낸 바이트의 합
        float32                      _receivedBytesPerSecond{ 0.0f };
        float32                      _rttMilliseconds{ 0.0f }; ///< 연결들의 평활 RTT 중 가장 큰 값(ms)
        float32                      _lossPercent{ 0.0f };     ///< 연결들이 측정한 손실 중 가장 큰 값(%)
        int32                        _connectionCount{ 0 };

        /**
         * @brief 받은 쪽(라우터) · 보낸 쪽(호스트) 종류별 셈에서 바이트가 큰 종류 `kMaxKindEntry` 개씩으로 `_listKindEntry` · `_listSentKindEntry` 를 채웁니다.
         *        한 번도 세지 않은 종류는 뺍니다.
         */
        void populateKinds( const NetMessageKindTallyTable& receivedTally, const NetMessageKindTallyTable& sentTally );
        /** @brief 한 방향의 종류별 셈입니다. */
        const vector<NetStatsKindEntry>& getKindEntries( NetStatsKindDirection direction ) const
        {
            return direction == NetStatsKindDirection::Sent ? _listSentKindEntry : _listKindEntry;
        }
        /** @brief 보고 한 줄을 만듭니다. */
        string makeLine() const;
        /** @brief @p line 에서 `kLinePrefix` 를 찾아 뒤의 값을 읽습니다. 머리가 없거나 아는 키의 값이 숫자가 아니면 false 이고 @p outReport 를 바꾸지 않습니다. 모르는 키는 건너뜁니다. */
        [[nodiscard]] static bool tryParseLine( string_view line, NetStatsReport& outReport );
    };
} // namespace sw
