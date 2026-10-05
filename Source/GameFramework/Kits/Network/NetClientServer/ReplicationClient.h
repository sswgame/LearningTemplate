/**
 * @file ReplicationClient.h
 * @brief 클라이언트 쪽 — 스냅샷을 기준 위에 풀어 쌓고 확인을 돌려주며, 보간 지연만큼 과거를 그리게 렌더 시각을 맞추고, 입력은 서버가 확인한 다음 틱부터 보냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Replication/NetClock.h"
#include "Core/Network/Replication/NetInputWindow.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Network/NetClientServer/NetSnapshot.h"
#include "GameFramework/Kits/Network/NetKitMessageRange.h"

namespace sw
{
    class NetHost;

    /** @brief 클라이언트 설정입니다. */
    struct ReplicationClientSettings
    {
        float32 _tickInterval{ 1.0f / 30.0f }; ///< 서버 틱 간격 — 스냅숏도 틱마다 와서 표본 간격이기도 하다
        float32 _interpolationDelay{ 0.1f };   ///< 렌더 지연의 최소값 — 실제는 이것과 (틱 간격 × `NetClock::kSampleIntervalsBehind`) 중 큰 것(스냅숏 하나를 잃어도 끊기지 않게)
        int32   _historySize{ 64 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReplicationClient
     * @brief 받은 스냅샷을 틱 순으로 들고, `sampleEntity` 가 렌더 시각을 사이에 둔 두 스냅샷의 그 엔티티와 비율을 줍니다(게임이 위치 · 회전을 섞는다).
     */
    class SW_GF_API ReplicationClient : public INetMessageHandler
    {
    public:
        ReplicationClient();

        void            initialize( NetHost* pHost, const ReplicationClientSettings& settings );
        uint8           getMessageRangeBase() const override { return NetKitMessageRange::kClientServer; }
        uint16          getMessageKindMask() const override { return 1u << ( NetClientServerMessage::kSnapshot - NetKitMessageRange::kClientServer ); }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 서버에 (다시) 연결됐다 — 받은 스냅샷 · 렌더 시각을 새로 시작한다(새 서버의 틱은 옛 것보다 작을 수 있다). */
        void onConnectionOpened( int32 connectionId ) override;
        /** @brief 렌더 시각을 흘립니다. */
        void update( float32 deltaTime );
        /**
         * @brief 이 틱의 입력을 넣고, 서버가 확인하지 않은 입력을 오래된 것부터 메시지 상한 안에서 보냅니다(못 실은 새 것은 다음 메시지가 싣는다).
         *        입력이 `NetClientServerMessage::kMaxInputBytes` 를 넘으면 넣지도 보내지도 않고 false 입니다. 같은 틱을 다시 내면 처음 값이 남는다.
         */
        bool sendInput( uint32 tick, const vector<uint8>& listInput );

        /**
         * @brief 렌더 시각의 엔티티 — 앞뒤 스냅샷의 상태와 그 사이 비율(0..1)입니다. 앞 스냅샷에만 있으면 그것 하나(@p pOutTo 도 같은 것).
         * @return 렌더 시각 근처에 그 엔티티가 없으면 false.
         */
        bool sampleEntity( uint32 entityId, const NetEntityState*& pOutFrom, const NetEntityState*& pOutTo, float32& outAlpha ) const;
        /** @brief 렌더 시각에 보이는 엔티티 id(앞 스냅샷 기준)입니다. */
        void collectVisibleEntities( vector<uint32>& outListEntity ) const;

        const NetSnapshot* getLatest() const;
        /** @brief 그리는 서버 틱(보간 지연만큼 과거, 소수 — 서버의 랙 보정이 쓴다)입니다. 스냅숏을 받기 전에는 0 입니다. */
        float32 getRenderTick() const { return _clock.hasServerTick() ? _clock.getRenderTick() : 0.0f; }
        uint64  getDecodeFailureCount() const { return _decodeFailureCount; }
        bool    hasSnapshot() const { return _listSnapshot.hasNewest(); }
        /** @brief 서버가 아직 확인하지 않은 내 입력 수(다음 메시지가 다시 싣는다)입니다. */
        int32 getPendingInputCount() const { return _inputWindow.getPendingCount(); }

    private:
        void resetHistory();
        void findBracket( const NetSnapshot*& pOutFrom, const NetSnapshot*& pOutTo, float32& outAlpha ) const;

        TickRingBuffer<NetSnapshot> _listSnapshot;  ///< 받은 스냅숏 — 델타의 기준 · 보간 구간을 틱으로 찾는다. 가장 새 틱 = 마지막으로 받은 것
        NetSnapshot                 _decodeScratch; ///< 받은 델타를 푸는 자리 — 고리 자리와 맞바꿔 밀려난 스냅숏의 버퍼를 다음 해독이 쓴다(기준 자리를 덮지 않는다)
        NetInputSendWindow          _inputWindow;   ///< 내 입력 — 서버가 스냅숏에 실어 돌려준 확인의 다음 틱부터 싣는다
        ReplicationClientSettings   _settings;
        NetHost*                    _pHost;
        NetClock                    _clock; ///< 렌더 틱 — 받은 스냅숏 틱의 하한 + 흐른 시간 − 지연(받은 가장 새 틱까지, 되돌아가지 않는다)
        uint64                      _decodeFailureCount;
        NetMessageWriter            _messageWriter; ///< 보낼 메시지 — 버퍼를 다시 쓴다
    };
} // namespace sw
