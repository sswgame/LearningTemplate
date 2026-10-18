/**
 * @file ReplicationClient.h
 * @brief 클라이언트 쪽 복제기입니다. 받은 델타를 기준 스냅샷 위에 복원해 쌓고 확인을 돌려줍니다.
 * @details 보간 지연만큼 과거를 그리도록 렌더 시각을 맞추고, 입력은 서버가 확인한 다음 틱부터 보낸다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Replication/NetClock.h"
#include "Core/Network/Replication/NetInputWindow.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/NetSnapshot.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class NetHost;

    /**
     * @brief 클라이언트 설정입니다. 범위는 칸의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `ReplicationClient::initialize` 가 오류로 거절합니다(`ConfigRangeUtil`).
     * @details 예전에는 기록 수를 쓸 때 조용히 4 로 올렸다(폴백 금지 원칙).
     */
    REFLECT()
    struct SW_KIT_API ReplicationClientConfig
    {
        REFLECT_BODY();
        PROPERTY( Min = 0.001, Units = s )
        float32 _tickInterval{ 1.0f / 30.0f }; ///< 서버 틱 간격. 스냅샷이 틱마다 오므로 표본 간격이기도 하다
        PROPERTY( Min = 0, Units = s )
        float32 _interpolationDelay{ 0.1f }; ///< 렌더 지연의 최솟값. 실제 값은 이 값과 (틱 간격 × `NetClock::kSampleIntervalsBehind`) 중 큰 쪽이며, 스냅샷 하나를 잃어도 끊기지 않게 하려는 것이다
        PROPERTY( Min = 4 )
        int32 _historySize{ 64 }; ///< 받아 두는 스냅샷 수. 델타의 기준과 보간 구간을 이 안에서 찾는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReplicationClient
     * @brief 받은 스냅샷을 틱 순서로 보관하고, `sampleEntity` 로 렌더 시각을 사이에 둔 두 스냅샷의 엔티티와 그 사이 비율을 돌려줍니다.
     * @details 위치와 회전을 섞는 일은 게임이 한다.
     */
    class SW_KIT_API ReplicationClient : public INetMessageHandler
    {
    public:
        ReplicationClient();

        /**
         * @brief 호스트와 설정을 받아 시작합니다. 설정이 범위 밖이면 오류를 남기고 false 를 반환합니다.
         * @details 거절된 클라이언트는 호스트가 없는 클라이언트와 같다. 입력을 보내지 않는다.
         */
        [[nodiscard]] bool initialize( NetHost* pHost, const ReplicationClientConfig& settings );
        uint8              getMessageRangeBase() const override { return NetKitMessageRange::kClientServer; }
        uint16             getMessageKindMask() const override { return 1u << ( NetClientServerMessage::kSnapshot - NetKitMessageRange::kClientServer ); }
        NetHandleResult    handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 서버에 (다시) 연결되면 호출합니다. 받은 스냅샷과 렌더 시각을 초기화하며, 새 서버의 틱은 이전 서버보다 작을 수 있기 때문입니다. */
        void onConnectionOpened( int32 connectionID ) override;
        /** @brief 렌더 시각을 진행합니다. */
        void update( float32 deltaTime );
        /**
         * @brief 이 틱의 입력을 넣고, 서버가 확인하지 않은 입력을 오래된 것부터 메시지 상한 안에서 보냅니다.
         * @details 못 실은 새 입력은 다음 메시지가 싣는다.
         *          입력이 `NetClientServerMessage::kMaxInputBytes` 를 넘으면 넣지도 보내지도 않고 false 를 반환한다. 같은 틱을 다시 내면 처음 값이 남는다.
         */
        bool sendInput( uint32 tick, const vector<uint8>& listInput );

        /**
         * @brief 렌더 시각의 엔티티를 앞뒤 스냅샷의 상태와 그 사이 비율(0~1)로 돌려줍니다. 앞 스냅샷에만 있으면 그것 하나만 돌려주고 @p pOutTo 도 같은 것을 가리킵니다.
         * @return 렌더 시각 근처에 그 엔티티가 없으면 false 입니다.
         */
        bool sampleEntity( uint32 entityID, const NetEntityState*& pOutFrom, const NetEntityState*& pOutTo, float32& outAlpha ) const;
        /** @brief 렌더 시각에 보이는 엔티티 id 를 모읍니다. 앞 스냅샷을 기준으로 합니다. */
        void collectVisibleEntities( vector<uint32>& outListEntity ) const;

        const NetSnapshot* getLatest() const;
        /** @brief 가장 새 스냅샷에 실린 내 엔티티의 상태와 그 입력 틱입니다. 클라이언트 예측의 `reconcile` 에 넘깁니다. 서버가 아직 정하지 않았으면 nullptr 입니다. */
        const NetOwnerState* findOwnerState() const;
        /** @brief 그리는 서버 틱입니다. 보간 지연만큼 과거이며 소수이고, 서버의 랙 보정이 씁니다. 스냅샷을 받기 전에는 0 입니다. */
        float32 getRenderTick() const { return _clock.hasServerTick() ? _clock.getRenderTick() : 0.0f; }
        uint64  getDecodeFailureCount() const { return _decodeFailureCount; }
        bool    hasSnapshot() const { return _listSnapshot.hasNewest(); }
        /** @brief 서버가 아직 확인하지 않은 내 입력 수입니다. 다음 메시지가 다시 싣습니다. */
        int32 getPendingInputCount() const { return _inputWindow.getPendingCount(); }

    private:
        void resetHistory();
        void findBracket( const NetSnapshot*& pOutFrom, const NetSnapshot*& pOutTo, float32& outAlpha ) const;

        TickRingBuffer<NetSnapshot> _listSnapshot;  ///< 받은 스냅샷. 델타의 기준과 보간 구간을 틱으로 찾는다. 가장 새 틱이 마지막으로 받은 것이다
        NetSnapshot                 _decodeScratch; ///< 받은 델타를 복원하는 버퍼. 링 버퍼 슬롯과 맞바꿔 밀려난 스냅샷의 버퍼를 다음 복원이 재사용하므로 기준 슬롯을 덮어쓰지 않는다
        NetInputSendWindow          _inputWindow;   ///< 내 입력. 서버가 스냅샷에 실어 돌려준 확인 틱의 다음 틱부터 싣는다
        ReplicationClientConfig     _settings;
        NetHost*                    _pHost;
        NetClock                    _clock; ///< 렌더 틱의 시계. 받은 스냅샷 틱의 하한에 흐른 시간을 더하고 지연을 뺀 값이며, 받은 가장 새 틱까지만 가고 되돌아가지 않는다
        uint64                      _decodeFailureCount;
        NetMessageWriter            _messageWriter; ///< 보낼 메시지. 버퍼를 재사용한다
    };
} // namespace sw
