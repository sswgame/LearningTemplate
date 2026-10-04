/**
 * @file DestructionReplication.h
 * @brief 파괴 네트워킹 — 권위 서버가 만든 피해 사건을 신뢰 · 순서 채널로 보내고(서버 틱 · 사건 번호를 붙여), 덩어리 자세를 낮은 빈도로 보내 클라이언트가
 *        보간하고, 늦은 참가 · 어긋남은 상태 스냅숏 하나로 맞춥니다. 키트 `GF_NetDestruction` — 자기 메시지 영역(`NetKitMessageRange::kDestruction`)을 쓴다.
 *        파괴를 쓰지 않는 게임은 링크하지 않고, 권위 방식(복제 서버 · 리슨 서버 · MMO)과 상관없이 `NetHost` 위에 얹는다.
 * @details 파괴의 설계(`Source/Engine/Destruction/README.md` 3 · 4 절)를 그대로 따릅니다.
 *          - **구조는 사건으로.** 같은 그래프 · 표 · 앵커에 같은 순서의 사건이면 같은 상태다(`DestructionState::computeStateHash`). 그래서 보내는 것은
 *            변환이 아니라 메시 공간 사건(`DestructionDamageEvent`, 실수는 비트 그대로)이고, 사건마다 번호(= 서버 상태의 사건 수)를 붙인다. 클라이언트는
 *            번호 순으로만 적용한다 — 앞 번호는 버리고(스냅숏이 이미 담았다), 뒤 번호는 기다린다.
 *          - **권한.** 부딪힘 사건은 서버 물리에서만 나온다 — 클라이언트 컴포넌트는 `setAuthority( false )`.
 *          - **크기로 나눈다(`DestructionProfile` 표의 `keepCollisionVolume`).** 덩어리(이상): 서버가 자세를 `<Network poseRate>` 빈도로 보내고(비신뢰 — 받는 쪽이 틱으로 줄 세운다),
 *            클라이언트는 보간 지연만큼 과거를 그려 그 바디를 키네마틱으로 옮긴다. 멈추면 마지막 자세를 신뢰 채널로 한 번 확정한다(비트 그대로). 파편(미만):
 *            클라이언트가 각자 시뮬레이션하는 꾸밈 — Debris 레이어라 캐릭터와 부딪히지 않는다(`physicssettings.xml`).
 *          - **늦은 참가.** 연결되면 사건이 하나라도 있는 오브젝트마다 스냅숏(끊긴 노드 · 연결 · 앵커 비트, 0 아닌 변형, 그룹, 떨어진 그룹 자세)을 보낸다
 *            (`FractureComponentBase::makeNetworkSnapshot`). 메시지 하나(1024 바이트)를 넘으면 조각으로 나눠 보낸다.
 *          - **어긋남.** 서버가 주기적으로(사건 수, 해시)를 보내고, 클라이언트가 같은 사건 수에서 해시를 비교한다. 다르면 스냅숏을 청해 바로잡는다.
 *
 *          조각의 물리 자세(파편)는 기계마다 다르다 — 꾸밈이다. 롤백(상태 저장 · 되돌리기)은 하지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Network/NetMessage.h"
#include "Core/Network/NetTypes.h"

#include "Engine/Destruction/DestructionDamage.h"
#include "Engine/Destruction/FractureComponentBase.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Network/NetKitMessageRange.h"

namespace sw
{
    class GameObjectManager;
    class NetHost;

    /** @brief 파괴 메시지 종류입니다. */
    struct NetDestructionMessage
    {
        static constexpr uint8 kEvent           = NetKitMessageRange::kDestruction + 0; ///< 서버 → 사건 하나(신뢰 순서)
        static constexpr uint8 kSnapshotPart    = NetKitMessageRange::kDestruction + 1; ///< 서버 → 스냅숏 조각(신뢰 순서)
        static constexpr uint8 kPose            = NetKitMessageRange::kDestruction + 2; ///< 서버 → 덩어리 자세(움직이는 중은 비신뢰, 멈춤은 신뢰)
        static constexpr uint8 kHash            = NetKitMessageRange::kDestruction + 3; ///< 서버 → (사건 수, 상태 해시)(신뢰 순서)
        static constexpr uint8 kSnapshotRequest = NetKitMessageRange::kDestruction + 4; ///< 클라이언트 → 스냅숏을 청한다(신뢰)
        static_assert( NetMessageRange::isInRange( kSnapshotRequest, NetKitMessageRange::kDestruction ), "message kinds must stay inside the kit's range" );
    };
} // namespace sw

namespace sw
{
    /** @brief 파괴 복제 설정입니다. */
    struct DestructionReplicationSettings
    {
        float32 _tickInterval{ 1.0f / 60.0f }; ///< 서버 틱 간격(자세 시각)
        float32 _hashInterval{ 1.0f };         ///< 서버가 상태 해시를 보내는 간격(초)
        float32 _interpolationDelay{ 0.1f };   ///< 클라이언트 — 덩어리 자세를 이만큼 과거로 그린다(자세 둘 사이)
        int32   _snapshotPartBytes{ 900 };     ///< 스냅숏 조각 하나의 바이트(메시지 한도 1024 안)
        float32 _positionResolution{ 0.001f }; ///< 움직이는 덩어리 자리 양자화(미터)
        float32 _positionRange{ 1024.0f };     ///< 자리 범위(±미터)
    };
} // namespace sw

namespace sw
{
    /** @brief 보낸 · 받은 양과 일어난 일입니다(대역폭 · 시험). */
    struct DestructionReplicationStats
    {
        uint64 _eventMessageCount{ 0 };
        uint64 _poseMessageCount{ 0 };
        uint64 _restPoseCount{ 0 }; ///< 멈춤 확정(신뢰)
        uint64 _snapshotCount{ 0 };
        uint64 _snapshotPartCount{ 0 };
        uint64 _hashMessageCount{ 0 };
        uint64 _sentBytes{ 0 };         ///< 메시지 몸 바이트(호스트 헤더 · 확인 제외)
        uint32 _hashMatchCount{ 0 };    ///< 클라이언트 — 비교해 같았던 수
        uint32 _hashMismatchCount{ 0 }; ///< 클라이언트 — 어긋나 스냅숏을 청한 수
        uint32 _snapshotAppliedCount{ 0 };
        uint32 _skippedEventCount{ 0 }; ///< 클라이언트 — 일부러 뺀 사건(`skipNextEvent`)
        uint32 _staleEventCount{ 0 };   ///< 클라이언트 — 스냅숏이 이미 담은 앞 번호 사건
        uint32 _sendRejectedCount{ 0 }; ///< 서버 — 신뢰 창이 차 연결이 거절한 메시지 수(그 클라이언트는 해시 비교 → 스냅숏으로 맞춘다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class DestructionReplicationServer
     * @brief 서버 — 등록한 파괴 오브젝트의 새 사건 · 덩어리 자세 · 해시 · 청한(또는 늦게 온 클라이언트의) 스냅숏을 틱마다 보냅니다.
     * @code
     *     server.initialize( &host, &manager, settings );
     *     server.registerObject( netId, *pFracture );           // 서버 · 클라이언트가 같은 번호(씬 엔티티 순서 등)
     *     router.addHandler( &server );                         // 연결 사건도 라우터가 넘긴다(늦은 참가 스냅숏)
     *     server.update( serverTick );                          // 월드 틱 뒤
     * @endcode
     */
    class SW_GF_API DestructionReplicationServer : public INetMessageHandler
    {
    public:
        DestructionReplicationServer();

        void initialize( NetHost* pHost, GameObjectManager* pManager, const DestructionReplicationSettings& settings );
        /** @brief 파괴 오브젝트를 번호로 등록합니다. 서버 쪽은 권한을 켠다. 같은 번호면 바꾼다. */
        void registerObject( uint32 netId, FractureComponentBase& component );
        /** @brief 월드 틱 뒤에 부릅니다 — 새 사건 → 스냅숏 → 자세 → 해시 순으로 보낸다. */
        void update( uint32 serverTick );

        uint8           getMessageRangeBase() const override { return NetKitMessageRange::kDestruction; }
        uint16          getMessageKindMask() const override { return 1u << ( NetDestructionMessage::kSnapshotRequest - NetKitMessageRange::kDestruction ); }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 클라이언트가 들어왔다 — 사건이 있는 오브젝트마다 스냅숏을 보낸다(다음 `update`). */
        void onConnectionOpened( int32 connectionId ) override;
        /** @brief 그 연결에 쌓인 스냅숏 요청을 지운다. */
        void onConnectionClosed( int32 connectionId, NetDisconnectReason reason ) override;

        const DestructionReplicationStats& getStats() const { return _stats; }

    private:
        /** @brief 덩어리 하나의 마지막으로 보낸 상태입니다. */
        struct ChunkSendState
        {
            uint32 _groupId{ 0 };
            uint8  _bRestSent{ SW_FALSE };
            uint8  _restRepeatLeft{ 0 }; ///< 멈춘 뒤에도 비신뢰 자세로 몇 번 더 보낸다(신뢰 확정이 밀린 회선에서 늦게 와도 그 자리에 먼저 닿게)
        };

        struct Entry
        {
            vector<ChunkSendState> _listChunk{};
            ComponentHandle        _component{};
            uint32                 _netId{ 0 };
            uint32                 _sentEventCount{ 0 };
            uint32                 _snapshotSerial{ 0 };
            float32                _poseTime{ 0.0f };
        };

        struct Request
        {
            int32  _connectionId{ -1 };
            uint32 _netId{ 0 };
        };

        FractureComponentBase* resolve( const Entry& entry ) const;
        Entry*                 findEntry( uint32 netId );
        void                   sendEvents( Entry& entry, FractureComponentBase& component, uint32 serverTick );
        void                   sendSnapshot( Entry& entry, FractureComponentBase& component, int32 connectionId );
        void                   sendPoses( Entry& entry, FractureComponentBase& component, uint32 serverTick );
        void                   sendHash( const Entry& entry, const FractureComponentBase& component );
        int32                  broadcast( NetChannelType channel );

        vector<Entry>                  _listEntry;
        vector<Request>                _listRequest;
        DestructionReplicationSettings _settings;
        DestructionReplicationStats    _stats;
        NetMessageWriter               _writer;
        NetHost*                       _pHost;
        GameObjectManager*             _pManager;
        float32                        _hashTime;
    };
} // namespace sw

namespace sw
{
    /**
     * @class DestructionReplicationClient
     * @brief 클라이언트 — 사건을 번호 순으로 컴포넌트에 넘기고, 스냅숏 조각을 모아 적용하고, 덩어리 자세를 보간해 몰고, 해시가 어긋나면 스냅숏을 청합니다.
     * @code
     *     client.initialize( &host, &manager, settings );
     *     client.registerObject( netId, *pFracture );          // 권한을 끈다
     *     router.addHandler( &client );
     *     client.update( deltaTime );                          // 월드 틱 앞(받은 것을 나눠 준 뒤)
     * @endcode
     */
    class SW_GF_API DestructionReplicationClient : public INetMessageHandler
    {
    public:
        DestructionReplicationClient();

        void initialize( NetHost* pHost, GameObjectManager* pManager, const DestructionReplicationSettings& settings );
        /** @brief 파괴 오브젝트를 번호로 등록합니다. 권한을 끈다(부딪힘이 사건을 만들지 않는다). */
        void registerObject( uint32 netId, FractureComponentBase& component );
        /** @brief 월드 틱 앞에 부릅니다 — 지난 틱에 적용된 상태로 해시를 비교하고, 렌더 틱을 흘려 덩어리 자세를 몬다. */
        void update( float32 deltaTime );
        /** @brief 시험 · 결함 주입 — 그 오브젝트의 다음 사건 하나를 적용하지 않고 넘깁니다(해시 어긋남 → 스냅숏 복구를 본다). */
        void skipNextEvent( uint32 netId );

        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kDestruction; }
        /** @brief 사건 · 스냅숏 조각 · 자세 · 해시 — 같은 영역의 스냅숏 요청은 서버가 맡는다. */
        uint16 getMessageKindMask() const override
        {
            constexpr uint8 kBase = NetKitMessageRange::kDestruction;
            return static_cast<uint16>( ( 1u << ( NetDestructionMessage::kEvent - kBase ) ) | ( 1u << ( NetDestructionMessage::kSnapshotPart - kBase ) ) |
                                        ( 1u << ( NetDestructionMessage::kPose - kBase ) ) | ( 1u << ( NetDestructionMessage::kHash - kBase ) ) );
        }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;

        /** @brief 덩어리를 그리는 서버 틱(보간 지연만큼 과거, 소수)입니다. 자세를 하나도 받지 않았으면 음수입니다. */
        float32                            getRenderTick() const { return _renderTick; }
        const DestructionReplicationStats& getStats() const { return _stats; }

    private:
        struct PoseSample
        {
            float3     _position{};
            quaternion _rotation{};
            uint32     _tick{ 0 };
            uint8      _bRest{ SW_FALSE };
        };

        struct ChunkTrack
        {
            deque<PoseSample> _listSample{}; ///< 틱 오름차순
            uint32            _groupId{ 0 };
        };

        struct HashCheck
        {
            uint64 _hash{ 0 };
            uint32 _eventCount{ 0 };
        };

        struct BufferedEvent
        {
            FractureGroupPose      _groupPose{}; ///< 맞은 떨어진 덩어리의 서버 자세(`_bHasPose`)
            DestructionDamageEvent _event{};
            uint32                 _index{ 0 };
            uint8                  _bHasPose{ SW_FALSE };
        };

        struct Entry
        {
            vector<BufferedEvent> _listFutureEvent{}; ///< 번호가 앞서 온 사건(스냅숏을 기다린다)
            vector<HashCheck>     _listHashCheck{};
            vector<ChunkTrack>    _listChunk{};
            vector<uint8>         _snapshotBytes{}; ///< 모으는 중인 스냅숏
            ComponentHandle       _component{};
            uint32                _netId{ 0 };
            uint32                _nextEventIndex{ 0 };
            uint32                _snapshotSerial{ 0 };
            uint32                _snapshotEventCount{ 0 };
            uint32                _snapshotPartCount{ 0 };
            uint32                _snapshotNextPart{ 0 };
            uint32                _skipCount{ 0 };
            uint8                 _bAwaitingSnapshot{ SW_FALSE };
        };

        FractureComponentBase* resolve( const Entry& entry ) const;
        Entry*                 findEntry( uint32 netId );
        void                   handleEvent( Entry& entry, const BufferedEvent& received );
        static void            applyToComponent( FractureComponentBase& component, const BufferedEvent& buffered );
        void                   handleSnapshotPart( Entry& entry, BitReader& reader );
        void                   handlePose( BitReader& reader, bool bRest );
        void                   compareHashes( Entry& entry, FractureComponentBase& component );
        void                   driveChunks( Entry& entry, FractureComponentBase& component );
        void                   requestSnapshot( Entry& entry );
        void                   observeServerTick( uint32 serverTick );

        vector<Entry>                  _listEntry;
        DestructionReplicationSettings _settings;
        DestructionReplicationStats    _stats;
        NetMessageWriter               _writer;
        NetHost*                       _pHost;
        GameObjectManager*             _pManager;
        float32                        _renderTick;
        float32                        _serverTickEstimate; ///< 받은 서버 틱 중 가장 늦은 것 + 그 뒤 흐른 틱(받는 순간의 서버 시각 추정)
        uint8                          _bHasServerTick;
    };
} // namespace sw
