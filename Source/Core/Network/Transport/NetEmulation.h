/**
 * @file NetEmulation.h
 * @brief 네트워크 흉내 — 어느 전송(UDP · 루프백) 위에나 씌워 연결마다 지연 · 흔들림 · 손실 · 중복 · 순서 뒤바뀜 · 대역폭 상한을 겁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/NetTransport.h"

namespace sw
{
    /**
     * @brief 흉내 거르개 — true 를 돌려준 패킷은 버립니다(손실 난수와 별개, 난수를 쓰지 않는다). 특정 패킷만 잃는 시험(핸드셰이크 `Accepted` 하나 ·
     *        위조 주소로 간 답 전부)에 씁니다. 흉내의 잠금 안에서 불리므로 전송을 다시 부르지 않는다.
     */
    using NetEmulationDropFilter = bool ( * )( const NetAddress& to, const uint8* pData, int32 size, void* pContext );
} // namespace sw

namespace sw
{
    /**
     * @struct NetEmulationConditions
     * @brief 한 방향(보내는 쪽) 회선의 나쁨입니다. 양쪽에 씌우면 왕복이 된다.
     */
    struct SW_API NetEmulationConditions
    {
        float64                _latency{ 0.0 };               ///< 한쪽 지연(초)
        float64                _jitter{ 0.0 };                ///< ± 흔들림(초) — 지연보다 크면 순서도 바뀐다
        float64                _reorderDelay{ 0.05 };         ///< 순서를 뒤바꿀 때 그 패킷에 더하는 지연(초)
        float32                _lossRate{ 0.0f };             ///< 0..1
        float32                _duplicateRate{ 0.0f };        ///< 0..1 — 같은 패킷이 한 번 더 간다
        float32                _reorderRate{ 0.0f };          ///< 0..1 — 그 패킷을 `_reorderDelay` 만큼 늦춰 뒤 패킷이 앞지른다
        int32                  _bandwidthBytesPerSecond{ 0 }; ///< 0 보다 크면 회선 속도 — 넘치는 만큼 줄을 선다
        int32                  _maxQueuedBytes{ 256 * 1024 }; ///< 대역폭 줄이 이만큼을 넘으면 새 패킷을 버린다(라우터 큐 넘침)
        NetEmulationDropFilter _pDropFilter{ nullptr };       ///< 있으면 보내기마다 먼저 묻는다 — true 면 버린다
        void*                  _pDropFilterContext{ nullptr };

        /** @brief 회선을 나쁘게 하는 것이 무엇이라도 켜져 있으면 true 입니다(꺼져 있으면 흉내는 그대로 넘긴다). 거르개는 따로 — 지연 없이 거르기만 한다. */
        bool isActive() const;
        /**
         * @brief 전역 변수에서 읽습니다 — `-gv_netEmuLatencyMs` · `-gv_netEmuJitterMs` · `-gv_netEmuLossPercent` · `-gv_netEmuDuplicatePercent` ·
         *        `-gv_netEmuReorderPercent` · `-gv_netEmuBandwidthKilobytesPerSecond`(언리얼 `PktLag` · `PktLagVariance` · `PktLoss` · `PktDup` · `PktOrder` 의 자리).
         */
        static NetEmulationConditions makeFromGlobalVariables();
    };
} // namespace sw

namespace sw
{
    /** @brief 흉내가 한 일의 수입니다. */
    struct NetEmulationStats
    {
        uint64 _sentCount{ 0 };    ///< 안쪽 전송에 넘긴 패킷(중복 포함)
        uint64 _droppedCount{ 0 }; ///< 손실 · 큐 넘침으로 버린 패킷
        uint64 _duplicatedCount{ 0 };
        uint64 _reorderedCount{ 0 }; ///< 일부러 늦춘 패킷
        uint64 _queueDropCount{ 0 }; ///< 그중 대역폭 큐 넘침
        uint64 _filteredCount{ 0 };  ///< 그중 거르개가 버린 것
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetEmulationTransport
     * @brief 다른 전송을 감싸 보내는 쪽에서 회선을 흉내 냅니다. `NetHost` 는 이것을 그냥 전송으로 받는다.
     * @details - 보내기(`send`)는 손실 · 중복 · 순서 바꿈을 정하고 전할 때를 적어 줄에 둔다. 전할 때는 지연 + 흔들림, 대역폭이 있으면 회선이 빌 때까지 미룬다
     *            (목적지마다 "회선이 빌 때" 를 든다 — 큰 스냅샷 하나가 뒤 패킷을 밀어낸다). 큐가 `_maxQueuedBytes` 를 넘으면 버린다.
     *          - `update( time )` 이 때가 된 것을 안쪽 전송에 넘기고 안쪽 `update` 를 부른다. 받기는 안쪽 그대로다.
     *          - 조건은 기본값 하나와 목적지별 덮어쓰기(`setConditions( to, … )`) — 연결 하나만 나쁘게 할 수 있다.
     *          - 난수는 씨앗 고정이라 같은 씨앗이면 같은 결과다(시험 · 재현). 스레드 안전합니다(`NetHostThread` 가 다른 스레드에서 돌린다).
     *          언리얼 `PacketSimulationSettings`(PktLag · PktLoss · PktDup · PktOrder · PktIncomingLag) · 유니티 Network Simulator(지연 · 흔들림 · 손실)의 자리입니다.
     */
    class SW_API NetEmulationTransport final : public INetTransport
    {
    public:
        NetEmulationTransport( INetTransport* pInner, uint32 seed = 1u );

        NetEmulationTransport( const NetEmulationTransport& )            = delete;
        NetEmulationTransport& operator=( const NetEmulationTransport& ) = delete;

        void setDefaultConditions( const NetEmulationConditions& conditions );
        /** @brief 목적지 하나의 조건을 둡니다(기본값 대신). */
        void setConditions( const NetAddress& to, const NetEmulationConditions& conditions );
        void clearConditions( const NetAddress& to );
        /** @brief 목적지에 쓰는 조건입니다. */
        NetEmulationConditions findConditions( const NetAddress& to ) const;

        [[nodiscard]] bool send( const NetAddress& to, const uint8* pData, int32 size ) override;
        [[nodiscard]] bool receive( NetAddress& outFrom, vector<uint8>& outBuffer ) override;
        NetAddress         getLocalAddress() const override;
        void               update( float64 time ) override;
        bool               waitForReceive( float64 timeoutSeconds ) override;

        NetEmulationStats getStats() const;
        /** @brief 아직 넘기지 않은 패킷 수 · 바이트입니다. */
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
            float64                _busyUntil{ 0.0 }; ///< 대역폭 회선이 빌 때
            uint64                 _queuedBytes{ 0 };
            uint8                  _bOverride{ SW_FALSE };
        };

        LinkState& findOrAddLink( const NetAddress& to );
        float32    nextRandom();

        mutable mutex          _mutex;
        INetTransport*         _pInner;
        vector<Pending>        _listPending;
        vector<LinkState>      _listLink;
        NetEmulationConditions _defaultConditions;
        NetEmulationStats      _stats;
        float64                _time;
        uint64                 _order;
        uint32                 _randomState;
    };
} // namespace sw
