/**
 * @file ReplicationClient.h
 * @brief 클라이언트 쪽 — 스냅샷을 기준 위에 풀어 쌓고 확인을 돌려주며, 보간 지연만큼 과거를 그리게 렌더 시각을 맞추고, 입력을 겹쳐 보냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Network/NetMessage.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Network/NetClientServer/NetSnapshot.h"
#include "GameFramework/Kits/Network/NetKitMessageRange.h"

namespace sw
{
    class NetHost;

    /** @brief 클라이언트 설정입니다. */
    struct ReplicationClientSettings
    {
        float32 _tickInterval{ 1.0f / 30.0f }; ///< 서버 틱 간격
        float32 _interpolationDelay{ 0.1f };   ///< 이만큼 과거를 그린다(스냅샷 두 개 사이 — 하나를 잃어도 끊기지 않게)
        float32 _clockCorrection{ 0.1f };      ///< 렌더 시각이 목표에서 벗어나면 초당 이 몫만큼 빠르게 · 느리게
        int32   _historySize{ 64 };
        int32   _inputRedundancy{ 4 }; ///< 입력 메시지마다 지난 입력을 몇 개까지 겹쳐 싣나(손실 대비, 1..`NetClientServerMessage::kMaxRedundantInputCount`, 메시지 상한 안에서)
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
        /** @brief 이 틱의 입력을 보냅니다(지난 입력 몇 개와 함께). 입력이 `NetClientServerMessage::kMaxInputBytes` 를 넘으면 보내지 않고 false 입니다. */
        bool sendInput( uint32 tick, const vector<uint8>& listInput );

        /**
         * @brief 렌더 시각의 엔티티 — 앞뒤 스냅샷의 상태와 그 사이 비율(0..1)입니다. 앞 스냅샷에만 있으면 그것 하나(@p pOutTo 도 같은 것).
         * @return 렌더 시각 근처에 그 엔티티가 없으면 false.
         */
        bool sampleEntity( uint32 entityId, const NetEntityState*& pOutFrom, const NetEntityState*& pOutTo, float32& outAlpha ) const;
        /** @brief 렌더 시각에 보이는 엔티티 id(앞 스냅샷 기준)입니다. */
        void collectVisibleEntities( vector<uint32>& outListEntity ) const;

        const NetSnapshot* getLatest() const;
        float32            getRenderTime() const { return _renderTime; }
        /** @brief 렌더 시각을 틱으로(서버의 랙 보정이 쓴다)입니다. */
        float32 getRenderTick() const { return _renderTime / _settings._tickInterval; }
        uint64  getDecodeFailureCount() const { return _decodeFailureCount; }
        bool    hasSnapshot() const { return _bHasSnapshot != SW_FALSE; }

    private:
        void               resetHistory();
        const NetSnapshot* findSnapshot( uint32 tick ) const;
        void               findBracket( const NetSnapshot*& pOutFrom, const NetSnapshot*& pOutTo, float32& outAlpha ) const;

        vector<NetSnapshot>       _listSnapshot; ///< 틱 % 크기
        deque<vector<uint8>>      _listRecentInput;
        ReplicationClientSettings _settings;
        NetHost*                  _pHost;
        float32                   _renderTime;
        uint64                    _decodeFailureCount;
        uint32                    _latestTick;
        uint32                    _latestInputTick;
        uint8                     _bHasSnapshot;
        NetMessageWriter          _messageWriter; ///< 보낼 메시지 — 버퍼를 다시 쓴다
    };
} // namespace sw
