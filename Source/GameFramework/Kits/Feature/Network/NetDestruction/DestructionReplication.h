/**
 * @file DestructionReplication.h
 * @brief 권위 서버가 만든 파괴 피해 이벤트를 클라이언트에 복제합니다.
 * @details 서버는 피해 이벤트를 신뢰성 있고 순서 없는 채널로 보내고, 덩어리 자세를 낮은 빈도로 보낸다.
 *          클라이언트는 덩어리 자세를 보간하고, 늦은 참가나 상태 불일치는 상태 스냅숏 하나로 맞춘다.
 *          이 기능은 키트 `GF_NetDestruction` 이 제공하며 자기 메시지 영역(`NetKitMessageRange::kDestruction`)을 쓴다.
 *          파괴를 쓰지 않는 게임은 링크하지 않는다. 복제 서버, 리슨 서버, MMO 같은 권위 방식과 상관없이 `NetEndpoint` 위에서 동작한다.
 *
 *          파괴의 설계(`Source/Engine/Destruction/README.md` 3절, 4절)를 그대로 따른다.
 *          - **구조 변경은 이벤트로 보낸다.** 같은 그래프, 테이블, 앵커에 같은 순서로 이벤트를 적용하면 같은 상태가 된다(`DestructionState::computeStateHash`).
 *            그래서 변환 결과가 아니라 메시 공간의 이벤트(`DestructionDamageEvent`, 실수는 비트 그대로)를 보낸다.
 *            이벤트마다 서버 상태의 이벤트 수를 번호로 붙이고, 클라이언트는 번호 순서로만 적용한다.
 *            스냅숏이 이미 담은 앞 번호는 버리고, 아직 오지 않은 앞 번호가 있으면 뒤 번호를 기다린다.
 *            이벤트는 순서 없는 채널로 가므로 앞 이벤트를 잃어도 뒤 이벤트가 기다리지 않는다. 순서 채널로 가는 스냅숏과도 도착 순서가 바뀔 수 있다.
 *            그래서 요청한 스냅숏을 기다리는 동안은 이벤트를 모두 쌓아 두었다가 스냅숏을 적용한 뒤 번호 순서로 이어서 적용한다.
 *          - **권한.** 충돌 이벤트는 서버 물리에서만 나온다. 클라이언트 컴포넌트는 `setAuthority( false )` 로 둔다.
 *          - **크기로 나눈다.** 기준은 `DestructionProfile` 테이블의 `keepCollisionVolume` 이다.
 *            덩어리(기준 이상)는 서버가 자세를 `<Network poseRate>` 빈도로 비신뢰 전송하고, 받는 쪽이 틱으로 순서를 맞춘다.
 *            클라이언트는 보간 지연만큼 과거 시점을 그려 그 바디를 키네마틱으로 옮긴다. 멈추면 마지막 자세를 비트 그대로 신뢰 채널로 한 번 확정한다.
 *            파편(기준 미만)은 클라이언트가 각자 시뮬레이션하는 장식이다. Debris 레이어(`physicssettings.xml`)라서 캐릭터와 부딪히지 않는다.
 *          - **늦은 참가.** 연결되면 이벤트가 하나라도 있는 오브젝트마다 스냅숏을 보낸다(`FractureComponentBase::makeNetworkSnapshot`).
 *            스냅숏에는 끊긴 노드, 연결, 앵커 비트, 0이 아닌 변형, 그룹, 떨어진 그룹의 자세가 들어간다.
 *            신뢰 순서 메시지 하나로 보내며 Core 가 64 KB까지 조각으로 나눠 전송한다. 이를 넘으면 오류를 로그로 남긴다. 신뢰 창이 차 있으면 다음 `update` 에서 다시 보낸다.
 *          - **불일치 복구.** 서버가 주기적으로 (이벤트 수, 해시)를 보내고, 클라이언트는 같은 이벤트 수에서 해시를 비교한다. 다르면 스냅숏을 요청해 바로잡는다.
 *
 *          파편의 물리 자세는 기계마다 다르다. 장식이므로 롤백(상태 저장과 되돌리기)은 하지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Replication/InterpolationBuffer.h"
#include "Core/Network/Replication/NetClock.h"

#include "Engine/Destruction/DestructionDamage.h"
#include "Engine/Destruction/Fracture/FractureComponentBase.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class GameObjectManager;
    class INetPeers;

    /** @brief 파괴 메시지 종류입니다. */
    struct NetDestructionMessage
    {
        static constexpr uint8 kEvent           = NetKitMessageRange::kDestruction + 0; ///< 서버 → 이벤트 하나. 신뢰, 순서 없음이며 받는 쪽이 번호로 순서를 맞춘다
        static constexpr uint8 kSnapshot        = NetKitMessageRange::kDestruction + 1; ///< 서버 → 오브젝트 하나의 상태 스냅숏. 신뢰, 순서 있음이며 64 KB까지 보낸다
        static constexpr uint8 kPose            = NetKitMessageRange::kDestruction + 2; ///< 서버 → 덩어리 자세. 움직이는 중에는 비신뢰, 멈추면 신뢰로 보낸다
        static constexpr uint8 kHash            = NetKitMessageRange::kDestruction + 3; ///< 서버 → (이벤트 수, 상태 해시). 신뢰, 순서 있음
        static constexpr uint8 kSnapshotRequest = NetKitMessageRange::kDestruction + 4; ///< 클라이언트 → 서버. 스냅숏을 요청한다(신뢰)
        static_assert( NetMessageRange::isInRange( kSnapshotRequest, NetKitMessageRange::kDestruction ), "message kinds must stay inside the kit's range" );
    };
} // namespace sw

namespace sw
{
    /** @brief 파괴 복제 설정입니다. */
    struct DestructionReplicationConfig
    {
        float32 _tickInterval{ 1.0f / 60.0f }; ///< 서버 틱 간격. 자세의 시각 계산에 쓴다
        float32 _hashInterval{ 1.0f };         ///< 서버가 상태 해시를 보내는 간격(초)
        float32 _interpolationDelay{ 0.1f };   ///< 클라이언트가 덩어리 자세를 그리는 과거 시점의 최솟값. 실제 값은 이 값과 (가장 긴 자세 간격 × `NetClock::kSampleIntervalsBehind`) 중 큰 쪽이다
        float32 _positionResolution{ 0.001f }; ///< 움직이는 덩어리 위치의 양자화 단위(미터)
        float32 _positionRange{ 1024.0f };     ///< 위치 범위(±미터)
    };
} // namespace sw

namespace sw
{
    /** @brief 보낸 양, 받은 양, 일어난 일의 횟수입니다. 대역폭 확인과 테스트에 씁니다. */
    struct DestructionReplicationStats
    {
        uint64 _eventMessageCount{ 0 };
        uint64 _poseMessageCount{ 0 };
        uint64 _restPoseCount{ 0 }; ///< 멈춤을 확정한 자세(신뢰)
        uint64 _snapshotCount{ 0 };
        uint64 _hashMessageCount{ 0 };
        uint64 _sentBytes{ 0 };         ///< 메시지 본문 바이트. 호스트 헤더와 확인 응답은 제외한다
        uint32 _hashMatchCount{ 0 };    ///< 클라이언트가 해시를 비교해 같았던 횟수
        uint32 _hashMismatchCount{ 0 }; ///< 클라이언트가 해시가 달라 스냅숏을 요청한 횟수
        uint32 _snapshotAppliedCount{ 0 };
        uint32 _skippedEventCount{ 0 };   ///< 클라이언트가 일부러 건너뛴 이벤트 수(`skipNextEvent`)
        uint32 _staleEventCount{ 0 };     ///< 클라이언트가 버린 이벤트 수. 스냅숏이 이미 담은 앞 번호의 이벤트이다
        uint32 _sendRejectedCount{ 0 };   ///< 서버의 메시지를 연결이 거절한 횟수. 신뢰 창이 찼거나 스냅숏이 64 KB를 넘은 경우이다. 이벤트는 해시 비교와 스냅숏으로 복구하고, 스냅숏은 다음 update 에서 다시 보낸다
        uint32 _outOfRangePoseCount{ 0 }; ///< 움직이는 자세가 양자화 범위(`_positionRange`) 밖이라 보내지 않은 횟수. 멈추면 비트 그대로 보낸다. 0 이 아니면 범위를 넓힌다
    };
} // namespace sw

namespace sw
{
    /**
     * @class DestructionReplicationServer
     * @brief 서버 쪽에서 등록한 파괴 오브젝트의 새 이벤트, 덩어리 자세, 해시를 틱마다 보내고, 요청받거나 늦게 들어온 클라이언트에는 스냅숏을 보냅니다.
     * @code
     *     server.initialize( &host, &manager, settings );
     *     server.registerObject( netID, *pFracture );           // 서버와 클라이언트가 같은 번호를 쓴다(씬 엔티티 순서 등)
     *     _subscription = router.registerHandler( &server );         // 라우터가 연결 이벤트도 넘겨 준다(늦은 참가 스냅숏). 구독이 사라지면 뗀다
     *     server.update( serverTick );                          // 월드 틱 뒤에 부른다
     * @endcode
     */
    class SW_KIT_API DestructionReplicationServer : public INetMessageHandler
    {
    public:
        DestructionReplicationServer();

        void initialize( INetPeers* pHost, GameObjectManager* pManager, const DestructionReplicationConfig& settings );
        /** @brief 파괴 오브젝트를 번호로 등록합니다. 서버 쪽은 권한을 켜며, 같은 번호가 이미 있으면 교체합니다. */
        void registerObject( uint32 netID, FractureComponentBase& component );
        /** @brief 월드 틱 뒤에 부릅니다. 새 이벤트, 스냅숏, 자세, 해시 순서로 보냅니다. */
        void update( uint32 serverTick );

        uint8           getMessageRangeBase() const override { return NetKitMessageRange::kDestruction; }
        uint16          getMessageKindMask() const override { return 1u << ( NetDestructionMessage::kSnapshotRequest - NetKitMessageRange::kDestruction ); }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 클라이언트가 들어오면 호출합니다. 이벤트가 있는 오브젝트마다 다음 `update` 에서 스냅숏을 보냅니다. */
        void onConnectionOpened( int32 connectionID ) override;
        /** @brief 그 연결에 쌓인 스냅숏 요청을 지웁니다. */
        void onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override;

        const DestructionReplicationStats& getStats() const { return _stats; }

    private:
        /** @brief 덩어리 하나에 대해 마지막으로 보낸 상태입니다. */
        struct ChunkSendState
        {
            uint32 _groupID{ 0 };
            uint8  _bRestSent{ SW_FALSE };
            uint8  _restRepeatLeft{ 0 }; ///< 멈춘 뒤에도 비신뢰 자세를 몇 번 더 보낸다. 신뢰 확정이 밀리는 회선에서도 클라이언트가 같은 위치를 먼저 받게 하려는 것이다
        };

        struct Entry
        {
            vector<ChunkSendState>                  _listChunk{};
            TComponentHandle<FractureComponentBase> _component{};
            uint32                                  _netID{ 0 };
            uint32                                  _sentEventCount{ 0 };
            float32                                 _poseTime{ 0.0f };
        };

        struct Request
        {
            int32  _connectionID{ -1 };
            uint32 _netID{ 0 };
        };

        FractureComponentBase* resolve( const Entry& entry ) const;
        Entry*                 findEntry( uint32 netID );
        void                   sendEvents( Entry& entry, FractureComponentBase& component, uint32 serverTick );
        /** @brief 스냅숏 메시지 하나를 보냅니다. 신뢰 창이 찼으면 false 를 반환하고, 호출한 쪽이 요청을 다음 `update` 로 미룹니다. */
        [[nodiscard]] bool sendSnapshot( Entry& entry, FractureComponentBase& component, int32 connectionID );
        void               sendPoses( Entry& entry, FractureComponentBase& component, uint32 serverTick );
        /**
         * @brief 자세 목록 하나를 메시지 예산(`kPoseMessageBudget`)에서 끊어 보냅니다.
         * @details 멈춘 자세(@p bRest)는 비트 그대로, 움직이는 자세는 양자화해서 쓴다. 하나라도 양자화하지 못한 메시지는 보내지 않는다.
         */
        void  sendPoseList( const Entry& entry, const vector<const FractureGroupPose*>& listPose, bool bRest, bool bReliable, uint32 serverTick );
        void  sendHash( const Entry& entry, const FractureComponentBase& component );
        int32 broadcast( NetChannelType channel );

        vector<Entry>                _listEntry;
        vector<Request>              _listRequest;
        DestructionReplicationConfig _settings;
        DestructionReplicationStats  _stats;
        NetMessageWriter             _writer;
        INetPeers*                   _pHost;
        GameObjectManager*           _pManager;
        float32                      _hashTime;
    };
} // namespace sw

namespace sw
{
    /**
     * @class DestructionReplicationClient
     * @brief 클라이언트 쪽에서 이벤트를 번호 순서로 컴포넌트에 적용하고, 스냅숏을 적용하고, 덩어리 자세를 보간하며, 해시가 어긋나면 스냅숏을 요청합니다.
     * @code
     *     client.initialize( &host, &manager, settings );
     *     client.registerObject( netID, *pFracture );          // 권한을 끈다
     *     _subscription = router.registerHandler( &client );
     *     client.update( deltaTime );                          // 월드 틱 앞에 부른다(받은 메시지를 분배한 뒤)
     * @endcode
     */
    class SW_KIT_API DestructionReplicationClient : public INetMessageHandler
    {
    public:
        DestructionReplicationClient();

        void initialize( INetPeers* pHost, GameObjectManager* pManager, const DestructionReplicationConfig& settings );
        /** @brief 파괴 오브젝트를 번호로 등록합니다. 권한을 끄므로 충돌이 이벤트를 만들지 않습니다. */
        void registerObject( uint32 netID, FractureComponentBase& component );
        /** @brief 월드 틱 앞에 부릅니다. 지난 틱에 적용된 상태로 해시를 비교하고, 렌더 틱을 진행해 덩어리 자세를 보간합니다. */
        void update( float32 deltaTime );
        /** @brief 테스트용 결함 주입입니다. 그 오브젝트의 다음 이벤트 하나를 적용하지 않고 건너뛰며, 해시 불일치에서 스냅숏 복구까지의 흐름을 확인할 때 씁니다. */
        void skipNextEvent( uint32 netID );

        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kDestruction; }
        /** @brief 이벤트, 스냅숏, 자세, 해시 메시지를 처리합니다. 같은 영역의 스냅숏 요청은 서버가 처리합니다. */
        uint16 getMessageKindMask() const override
        {
            constexpr uint8 kBase = NetKitMessageRange::kDestruction;
            return static_cast<uint16>( ( 1u << ( NetDestructionMessage::kEvent - kBase ) ) | ( 1u << ( NetDestructionMessage::kSnapshot - kBase ) ) |
                                        ( 1u << ( NetDestructionMessage::kPose - kBase ) ) | ( 1u << ( NetDestructionMessage::kHash - kBase ) ) );
        }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;

        /** @brief 덩어리를 그리는 서버 틱입니다. 보간 지연만큼 과거이며 소수입니다. 자세를 하나도 받지 않았으면 음수입니다. */
        float32                            getRenderTick() const { return _clock.getRenderTick(); }
        const DestructionReplicationStats& getStats() const { return _stats; }

    private:
        /** @brief 덩어리 자세 표본 하나입니다. 틱은 `InterpolationBuffer` 가 보관합니다. */
        struct ChunkPose
        {
            float3     _position{};
            quaternion _rotation{};
        };

        struct ChunkTrack
        {
            InterpolationBuffer<ChunkPose> _poseBuffer{}; ///< 틱 오름차순이다. 같은 틱이면 멈춤 확정 자세가 이긴다
            uint32                         _groupID{ 0 };
        };

        struct HashCheck
        {
            uint64 _hash{ 0 };
            uint32 _eventCount{ 0 };
        };

        struct BufferedEvent
        {
            FractureGroupPose      _groupPose{}; ///< 맞은 떨어진 덩어리의 서버 자세. `_bHasPose` 가 true 일 때만 유효하다
            DestructionDamageEvent _event{};
            uint32                 _index{ 0 };
            uint8                  _bHasPose{ SW_FALSE };
        };

        struct Entry
        {
            vector<BufferedEvent>                   _listFutureEvent{}; ///< 번호가 앞서 도착한 이벤트와 스냅숏을 기다리는 동안 도착한 이벤트. 번호 순서로 이어서 적용한다
            vector<HashCheck>                       _listHashCheck{};
            vector<ChunkTrack>                      _listChunk{};
            TComponentHandle<FractureComponentBase> _component{};
            uint32                                  _netID{ 0 };
            uint32                                  _nextEventIndex{ 0 };
            uint32                                  _skipCount{ 0 };
            uint8                                   _bAwaitingSnapshot{ SW_FALSE };
        };

        FractureComponentBase* resolve( const Entry& entry ) const;
        Entry*                 findEntry( uint32 netID );
        void                   handleEvent( Entry& entry, const BufferedEvent& received );
        /** @brief 번호가 앞선 이벤트를 쌓아 둡니다. 같은 번호는 한 번만 보관합니다. */
        static void bufferEvent( Entry& entry, const BufferedEvent& received );
        static void applyToComponent( FractureComponentBase& component, const BufferedEvent& buffered );
        /** @brief 스냅숏 하나를 적용합니다. 깨졌으면 false 입니다. */
        [[nodiscard]] bool handleSnapshot( Entry& entry, BitReader& reader );
        void               handlePose( BitReader& reader, bool bRest );
        void               compareHashes( Entry& entry, FractureComponentBase& component );
        void               driveChunks( Entry& entry, FractureComponentBase& component );
        void               requestSnapshot( Entry& entry );

        vector<Entry>                _listEntry;
        DestructionReplicationConfig _settings;
        DestructionReplicationStats  _stats;
        NetMessageWriter             _writer;
        INetPeers*                   _pHost;
        GameObjectManager*           _pManager;
        NetClock                     _clock; ///< 덩어리를 그리는 서버 틱의 시계. 흐른 시간으로 서버 틱을 추정하고(받은 가장 새 틱 + 지연까지) 보간 지연을 뺀 값이며 뒤로 가지 않는다. 표본 간격은 등록한 오브젝트 중 가장 긴 자세 간격이다
    };
} // namespace sw
