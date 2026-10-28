/**
 * @file NetEmulation.h
 * @brief 네트워크 흉내(에뮬레이션)를 제공합니다. 어느 전송(UDP, 루프백) 위에나 씌워 연결마다 지연, 흔들림, 손실, 중복, 깨짐, 순서 뒤바뀜, 대역폭 상한을 적용합니다.
 * @details 나쁜 회선을 흉내 내는 곳은 이 파일 하나다. 루프백 네트워크는 배달만 한다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/DeterministicRandom.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/NetTransport.h"

namespace sw
{
    /**
     * @brief 에뮬레이션 필터입니다. true 를 반환한 패킷은 버립니다(손실 난수와 별개이며 난수를 쓰지 않습니다). 특정 패킷만 잃는 테스트(핸드셰이크 `Accepted` 하나,
     *        위조 주소로 가는 응답 전부)에 씁니다. 에뮬레이션의 잠금 안에서 호출되므로 전송을 다시 부르지 않습니다.
     */
    using NetEmulationDropFilter = bool ( * )( const NetAddress& to, const uint8* pData, int32 size, void* pContext );
} // namespace sw

namespace sw
{
    /**
     * @struct NetEmulationConditions
     * @brief 한 방향(보내는 쪽) 회선의 나쁜 정도를 정합니다. 양쪽에 씌우면 왕복이 됩니다.
     */
    struct SW_API NetEmulationConditions
    {
        float64                _latency{ 0.0 };               ///< 한쪽 방향 지연(초).
        float64                _jitter{ 0.0 };                ///< ± 흔들림(초). 지연보다 크면 순서도 바뀐다.
        float64                _reorderDelay{ 0.05 };         ///< 순서를 뒤바꿀 때 그 패킷에 더하는 지연(초).
        float32                _lossRate{ 0.0f };             ///< 0~1.
        float32                _duplicateRate{ 0.0f };        ///< 0~1. 같은 패킷이 한 번 더 간다.
        float32                _reorderRate{ 0.0f };          ///< 0~1. 그 패킷을 `_reorderDelay` 만큼 늦춰 뒤 패킷이 앞지르게 한다.
        float32                _corruptRate{ 0.0f };          ///< 0~1. 그 패킷의 한 바이트를 뒤집는다(받는 쪽 체크섬이 걸러 내야 한다).
        int32                  _bandwidthBytesPerSecond{ 0 }; ///< 0보다 크면 회선 속도다. 넘치는 만큼 줄을 선다.
        int32                  _maxQueuedBytes{ 256 * 1024 }; ///< 대역폭 큐가 이만큼을 넘으면 새 패킷을 버린다(라우터 큐 넘침).
        NetEmulationDropFilter _pDropFilter{ nullptr };       ///< 있으면 보낼 때마다 먼저 묻는다. true 면 버린다.
        void*                  _pDropFilterContext{ nullptr };

        /** @brief 회선을 나쁘게 하는 조건이 하나라도 켜져 있으면 true 를 반환합니다(꺼져 있으면 에뮬레이션은 그대로 통과시킵니다). 필터는 별개이며, 지연 없이 거르기만 합니다. */
        bool isActive() const;
        /**
         * @brief 전역 변수에서 읽습니다. `-gv_netEmuLatencyMs`, `-gv_netEmuJitterMs`, `-gv_netEmuLossPercent`, `-gv_netEmuDuplicatePercent`,
         *        `-gv_netEmuReorderPercent`, `-gv_netEmuBandwidthKilobytesPerSecond` 가 해당하며, 언리얼의 `PktLag`, `PktLagVariance`, `PktLoss`, `PktDup`, `PktOrder` 에 대응합니다.
         * @details 이름 있는 회선 프리셋(`-gv_netEmuPreset`)은 Engine 의 프리셋 표(`NetEmulationPresetTable`)가 풀어 이 값 대신 씁니다.
         */
        static NetEmulationConditions makeFromGlobalVariables();

        /**
         * @brief `latencyMs=80 jitterMs=20 lossPercent=5 duplicatePercent=1 reorderPercent=2 bandwidthKBps=256` 같은 덮어쓰기를 @p inoutConditions 에 적용합니다.
         * @details 키는 전역 변수(`gv_netEmu*`)와 같은 단위입니다. 빈칸으로 나누며, 모르는 키나 음수 · 숫자가 아닌 값이면 false 와 @p outError 이고 아무것도 바꾸지 않습니다.
         */
        [[nodiscard]] static bool parseOverrides( string_view text, NetEmulationConditions& inoutConditions, string& outError );
        /**
         * @brief `parseOverrides` 가 읽는 글을 만듭니다(`latencyMs=35 jitterMs=15 lossPercent=1 duplicatePercent=0 reorderPercent=0.5 bandwidthKBps=1024`).
         * @details 키 여섯을 모두 쓰므로 받는 쪽은 앞 상태와 상관없이 이 조건이 된다. 값은 0.01 단위로 반올림한다. 에디터 회선 조절이 자식 프로세스에 보내는 명령이 이 글이다.
         */
        string makeOverrideText() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NetEmulationReport
     * @brief 프로세스 하나의 회선 흉내 · 연결 수치를 한 줄로 주고받는 형식입니다(개발 명령 `net.emulation stats` 의 답).
     * @details 쓰는 쪽(게임 세션)과 읽는 쪽(에디터 Multiplayer 창 · 감시 값)이 이 정의 하나를 씁니다. 줄은 `kLinePrefix` 뒤에 `key=value` 를 빈칸으로 잇습니다.
     */
    struct SW_API NetEmulationReport
    {
        /** @brief 줄의 머리입니다. 로그 줄 안 어디에 있어도 찾습니다(앞에 로그 태그 · 명령 에코가 붙는다). */
        static constexpr const utf8* kLinePrefix = "Net emulation stats";

        uint64  _sentPacketCount{ 0 };        ///< 흉내가 안쪽 전송에 넘긴 패킷
        uint64  _droppedPacketCount{ 0 };     ///< 흉내가 버린 패킷(보내는 쪽 손실 · 큐 넘침 · 끊김과 받는 쪽 손실)
        uint64  _resentMessageCount{ 0 };     ///< 연결들이 다시 보낸 신뢰 메시지의 합(`NetConnectionStats::_resentMessageCount`)
        float32 _measuredLossPercent{ 0.0f }; ///< 연결들이 측정한 손실 중 가장 큰 값(%) — 확인이 안 온 패킷으로 추정한 값이다
        float32 _rttMilliseconds{ 0.0f };     ///< 연결들의 평활 RTT 중 가장 큰 값(ms)
        int32   _connectionCount{ 0 };        ///< 붙어 있는 연결 수

        /** @brief `kLinePrefix connections=1 sent=… dropped=… resent=… lossPercent=… rttMs=…` 한 줄을 만듭니다. 길이는 정해져 있지만 한 줄 답의 상한(`DebugReplyLine::kMaxBytes`)은 `DebugReplyLine::makeFixed` 가 강제한다. */
        string makeLine() const;
        /** @brief @p line 에서 `kLinePrefix` 를 찾아 뒤의 값을 읽습니다. 머리가 없거나 아는 키의 값이 숫자가 아니면 false 이고 @p outReport 를 바꾸지 않습니다. 모르는 키는 건너뜁니다. */
        [[nodiscard]] static bool tryParseLine( string_view line, NetEmulationReport& outReport );
    };
} // namespace sw

namespace sw
{
    /** @brief 에뮬레이션이 처리한 패킷 수입니다. */
    struct NetEmulationStats
    {
        uint64 _sentCount{ 0 };    ///< 안쪽 전송에 넘긴 패킷(중복 포함).
        uint64 _droppedCount{ 0 }; ///< 손실이나 큐 넘침으로 버린 패킷.
        uint64 _duplicatedCount{ 0 };
        uint64 _reorderedCount{ 0 };       ///< 일부러 늦춘 패킷.
        uint64 _queueDropCount{ 0 };       ///< 버린 패킷 중 대역폭 큐 넘침으로 버린 것.
        uint64 _filteredCount{ 0 };        ///< 버린 패킷 중 필터가 버린 것.
        uint64 _corruptedCount{ 0 };       ///< 한 바이트를 뒤집어 넘긴 패킷(버리지 않는다).
        uint64 _outageDroppedCount{ 0 };   ///< 끊김(`startOutage`) 동안 버린 패킷(보내기와 받기).
        uint64 _incomingDroppedCount{ 0 }; ///< 받는 쪽 조건의 손실로 버린 패킷.
        uint64 _incomingDelayedCount{ 0 }; ///< 받는 쪽 조건으로 늦춰 넘긴 패킷.
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetEmulationTransport
     * @brief 다른 전송을 감싸 보내는 쪽에서 회선을 흉내 냅니다. `NetEndpoint` 는 이것을 일반 전송으로 받습니다.
     * @details - 보내기(`send`)는 손실, 중복, 깨짐, 순서 바꿈을 정하고 전달할 시각을 적어 큐에 둔다. 전달 시각은 지연과 흔들림을 더한 값이며,
     *            대역폭이 있으면 회선이 빌 때까지 미룬다(목적지마다 "회선이 비는 시각"을 보관하므로 큰 스냅샷 하나가 뒤 패킷을 밀어낸다). 큐가 `_maxQueuedBytes` 를 넘으면 버린다.
     *          - `update( time )` 은 전달할 때가 된 패킷을 안쪽 전송에 넘기고 안쪽 `update` 를 부른다. 받기는 안쪽 전송이 그대로 처리한다.
     *          - 조건은 기본값 하나와 목적지별 덮어쓰기(`setConditions( to, … )`)로 정한다. 연결 하나만 나쁘게 할 수 있다.
     *          - 난수는 시드를 고정하므로 같은 시드면 같은 결과가 나온다(테스트와 재현용). 깨짐 난수는 깨짐을 켰을 때만 뽑으므로, 다른 조건만 쓰는 경우의 난수열은 그대로다.
     *            스레드 안전하다(`NetEndpointThread` 가 다른 스레드에서 돌린다).
     *          - 보내는 쪽 큐이므로 엔드포인트를 한 스레드에서 차례로 돌릴 때는 프레임마다 에뮬레이션을 모두 먼저 `update` 한다. 그러지 않으면 뒤에 도는 엔드포인트가 보낸 것이 한 프레임 늦는다.
     *          - 언리얼의 `PacketSimulationSettings`(PktLag, PktLoss, PktDup, PktOrder, PktIncomingLag), 유니티 Network Simulator(지연, 흔들림, 손실)에 대응한다.
     */
    class SW_API NetEmulationTransport final : public INetTransport
    {
    public:
        NetEmulationTransport( INetTransport* pInner, uint32 seed = 1u );

        NetEmulationTransport( const NetEmulationTransport& )            = delete;
        NetEmulationTransport& operator=( const NetEmulationTransport& ) = delete;

        void                   setDefaultConditions( const NetEmulationConditions& conditions );
        NetEmulationConditions getDefaultConditions() const;
        /** @brief 목적지 하나의 조건을 둡니다(기본값 대신). */
        void setConditions( const NetAddress& to, const NetEmulationConditions& conditions );
        void clearConditions( const NetAddress& to );
        /** @brief 목적지에 적용하는 조건을 반환합니다. */
        NetEmulationConditions findConditions( const NetAddress& to ) const;
        /**
         * @brief 받는 쪽(내려받기) 조건을 둡니다. 지연 · 흔들림 · 손실 · 순서 바꿈만 씁니다(대역폭 · 중복 · 깨짐은 보내는 쪽 조건이 담당).
         * @details 한 프로세스가 올림과 내림을 따로 나쁘게 하는 길입니다(언리얼 `PktIncomingLagMin` · `PktIncomingLoss`). 받은 패킷을 큐에 두고 때가 되면 `receive` 가 넘깁니다.
         */
        void                   setIncomingConditions( const NetEmulationConditions& conditions );
        NetEmulationConditions getIncomingConditions() const;
        /** @brief 지금(마지막 `update` 시각)부터 @p durationSeconds 동안 보내고 받는 것을 모두 버립니다. 회선 조절의 "스파이크"(터널 · 엘리베이터)입니다. 0 이하면 끝냅니다. */
        void startOutage( float64 durationSeconds );
        bool isInOutage() const;

        [[nodiscard]] bool send( const NetAddress& to, const uint8* pData, int32 size ) override;
        [[nodiscard]] bool receive( NetAddress& outFrom, vector<uint8>& outBuffer ) override;
        NetAddress         getLocalAddress() const override;
        void               update( float64 time ) override;
        bool               waitForReceive( float64 timeoutSeconds ) override;

        NetEmulationStats getStats() const;
        /** @brief 아직 넘기지 않은 패킷 수와 바이트 수를 반환합니다. */
        size_t getQueuedCount() const;
        uint64 getQueuedBytes() const;

    private:
        struct Pending
        {
            vector<uint8> _buffer{};
            NetAddress    _to{};
            float64       _deliverTime{ 0.0 };
            uint64        _order{ 0 };
        };

        struct LinkState
        {
            NetEmulationConditions _conditions{};
            NetAddress             _to{};
            float64                _busyUntil{ 0.0 }; ///< 대역폭 회선이 비는 시각.
            uint64                 _queuedBytes{ 0 };
            uint8                  _bOverride{ SW_FALSE };
        };

        /** @brief 받는 쪽 조건으로 늦춘 패킷 하나입니다. */
        struct Incoming
        {
            vector<uint8> _buffer{};
            NetAddress    _from{};
            float64       _deliverTime{ 0.0 };
            uint64        _order{ 0 };
        };

        LinkState& getOrCreateLink( const NetAddress& to );
        float32    nextRandom() SW_REQUIRES_LOCK( _mutex );
        /** @brief 잠금 안에서 부릅니다. 안쪽 전송에서 받은 것을 받는 쪽 조건으로 큐에 넣습니다. */
        void pullIncomingLocked() SW_REQUIRES_LOCK( _mutex );

        mutable mutex                              _mutex;
        INetTransport* const                       _pInner; ///< 생성 때 정하고 바뀌지 않는다(const — 잠금 없이 읽는다)
        vector<Pending> _listPending               SW_GUARDED_BY( _mutex );
        vector<Incoming> _listIncoming             SW_GUARDED_BY( _mutex ); ///< 받는 쪽 조건으로 늦춘 패킷(도착 순서)
        vector<uint8>                              _receivedBytes;
        vector<LinkState>                          _listLink;
        NetEmulationConditions                     _defaultConditions;
        NetEmulationConditions _incomingConditions SW_GUARDED_BY( _mutex );
        NetEmulationStats _stats                   SW_GUARDED_BY( _mutex );
        float64 _time                              SW_GUARDED_BY( _mutex );
        float64 _outageUntil                       SW_GUARDED_BY( _mutex ); ///< 이 시각 전까지 보내고 받는 것을 모두 버린다
        uint64 _order                              SW_GUARDED_BY( _mutex );
        DeterministicRandom32 _random              SW_GUARDED_BY( _mutex ); ///< 손실 · 지연 흔들림 난수(씨앗이 같으면 같은 수열 — 테스트 · 재현)
    };
} // namespace sw
