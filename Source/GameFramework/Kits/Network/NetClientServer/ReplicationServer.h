/**
 * @file ReplicationServer.h
 * @brief 권위 서버 쪽 — 매 틱 월드 상태를 모아 클라이언트마다 (관련 · 우선도 정책을 거쳐) 확인받은 기준 대비 델타로 보내고, 중복으로 온 입력을 틱 순으로 꺼내 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Replication/NetParallel.h"
#include "Core/Network/Replication/NetPrioritizer.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Network/NetClientServer/NetSnapshot.h"
#include "GameFramework/Kits/Network/NetKitMessageRange.h"

namespace sw
{
    class NetHost;

    /**
     * @class IReplicationPolicy
     * @brief 무엇을 누구에게 얼마나 먼저 보낼지 — 게임마다 바꿉니다(배틀로얄은 거리, 비대칭 대전은 시야 · 역할, 기체 대전은 전부).
     * @warning 서버에 `setTaskManager` 를 주면 두 함수가 **여러 스레드에서 동시에** 불린다 — 상태를 바꾸지 말고(읽기만), 읽는 게임 상태는 스냅샷을
     *          보내는 동안 바뀌지 않아야 한다(보통 틱 끝에 보내니 그렇다).
     */
    class SW_GF_API IReplicationPolicy
    {
    public:
        IReplicationPolicy()          = default;
        virtual ~IReplicationPolicy() = default;

        IReplicationPolicy( const IReplicationPolicy& )            = default;
        IReplicationPolicy& operator=( const IReplicationPolicy& ) = default;

        /** @brief 이 클라이언트에게 이 엔티티를 보내는가입니다(안 보내면 클라이언트에서 사라진다). */
        virtual bool isRelevant( int32 connectionId, const NetEntityState& entity ) const
        {
            (void)connectionId;
            (void)entity;
            return true;
        }
        /**
         * @brief 스냅샷마다 쌓는 우선도입니다(클수록 자주). 쌓인 것이 큰 엔티티부터 예산에 싣고, 실으면 0 으로 돌린다 — 예산이 늘 차도 낮은 우선도가
         *        쌓여 언젠가 간다(`NetPrioritizer`). 우선도 1 은 우선도 10 이 예산을 채운 틱 열 번에 한 번쯤 간다.
         */
        virtual float32 computePriority( int32 connectionId, const NetEntityState& entity ) const
        {
            (void)connectionId;
            (void)entity;
            return 1.0f;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 설정입니다. */
    struct ReplicationServerSettings
    {
        int32 _snapshotBudgetBytes{ 1000 }; ///< 클라이언트 · 틱마다 스냅샷 메시지 상한(종류 바이트 · 머리 · 사라진 목록 포함, `NetConnection::kMaxMessageSize` 로 잘린다)
        int32 _historySize{ 64 };           ///< 클라이언트마다 기억하는 보낸 스냅샷(기준 후보) 수
        int32 _inputBufferSize{ 64 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReplicationServer
     * @code
     *     server.beginTick( tick );
     *     for ( each entity ) server.setEntity( id, typeId, bytes );
     *     server.endTick();
     *     server.sendSnapshots();
     *     // 받은 메시지 · 연결 사건: router.addHandler( &server ) 뒤 매 틱 router.pump( host ) — 손으로는 server.handleMessage( connectionId, bytes ),
     *     //                        server.onConnectionOpened / onConnectionClosed
     *     // 시뮬레이션: server.popInput( connectionId, tick, inputBytes ); ... server.setLastProcessedInputTick( connectionId, tick );
     * @endcode
     */
    class SW_GF_API ReplicationServer : public INetMessageHandler
    {
    public:
        ReplicationServer();

        void initialize( NetHost* pHost, const ReplicationServerSettings& settings, const IReplicationPolicy* pPolicy = nullptr );
        void beginTick( uint32 tick );
        /** @brief 이 틱의 엔티티입니다. 상태가 `NetSnapshot::kMaxEntityBytes` 를 넘으면 복제하지 않는다(처음 한 번 경고, 그 뒤로는 센다). */
        void setEntity( uint32 entityId, uint32 typeId, const vector<uint8>& buffer );
        void endTick();
        /**
         * @brief 연결된 클라이언트마다 스냅샷을 보냅니다. `setTaskManager` 를 줬으면 클라이언트들을 작업 스레드에 나눠 만든다(클라이언트마다 독립 —
         *        결과는 한 스레드로 돌 때와 같다).
         */
        void sendSnapshots();
        /** @brief 스냅샷 만들기를 나눌 작업 스레드 풀입니다(게임은 `&engine::getTaskManager()`). nullptr 이면 지금 스레드가 돈다. */
        void            setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold = NetParallelFor::kDefaultSerialThreshold );
        uint8           getMessageRangeBase() const override { return NetKitMessageRange::kClientServer; }
        uint16          getMessageKindMask() const override;
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 연결마다의 상태(확인 틱 · 입력 줄 · 처리한 입력 틱)를 새로 시작합니다 — 같은 자리에 새로 온 클라이언트가 옛 상태를 이어 쓰지 않게. */
        void onConnectionOpened( int32 connectionId ) override;
        void onConnectionClosed( int32 connectionId, NetDisconnectReason reason ) override;

        /**
         * @brief 그 틱의 입력을 꺼냅니다. 아직 안 왔으면 가장 최근 입력을 되풀이합니다(@p outbExact false). 받은 입력이 하나도 없으면 false 입니다.
         */
        [[nodiscard]] bool popInput( int32 connectionId, uint32 tick, vector<uint8>& outInputBuffer, bool& outbExact );
        void               setLastProcessedInputTick( int32 connectionId, uint32 tick );
        /** @brief 클라이언트가 보고 있는 틱(보간 지연만큼 과거, 소수 — 랙 보정 되감기)입니다. */
        float32            getClientViewTick( int32 connectionId ) const;
        uint32             getAckedTick( int32 connectionId ) const;
        const NetSnapshot& getWorldSnapshot() const { return _world; }
        /** @brief 상한을 넘어 복제하지 않은 `setEntity` 수입니다. */
        uint64 getOversizedEntityCount() const;

    private:
        struct InputEntry
        {
            vector<uint8> _buffer{};
            uint32        _tick{ 0 };
        };

        struct ClientState
        {
            NetPrioritizer      _prioritizer{}; ///< 관련 엔티티마다 쌓인 우선도
            vector<NetSnapshot> _listSent{};    ///< 틱 % 크기 자리 — 보낸(재구성된) 스냅샷
            deque<InputEntry>   _listInput{};   ///< 틱 오름차순
            vector<uint8>       _lastInput{};
            float32             _viewTick{ 0.0f };
            uint32              _ackedTick{ 0 };
            uint32              _lastProcessedInputTick{ 0 };
            uint8               _bHasAck{ SW_FALSE };
            uint8               _bHasInput{ SW_FALSE };
            uint8               _bActive{ SW_FALSE };
        };

        /** @brief 작업 스레드 하나가 스냅샷 하나를 만드는 데 쓰는 자리입니다(틱마다 다시 쓴다). */
        struct SnapshotScratch
        {
            NetSnapshot      _filtered{}; ///< 클라이언트 하나의 관련 엔티티
            vector<uint32>   _listOrderEntity{};
            vector<int32>    _listOrder{};   ///< 위 순서를 `_filtered` 자리로
            vector<uint8>    _listCurrent{}; ///< `_filtered` 자리마다 받는 쪽이 지금 상태를 갖게 됐나
            NetMessageWriter _messageWriter{};
        };

        ClientState&       acquireClient( int32 connectionId );
        void               sendSnapshotRange( uint32 start, uint32 end );
        void               sendSnapshot( int32 connectionId, ClientState& client, SnapshotScratch& scratch );
        [[nodiscard]] bool handleInput( ClientState& client, BitReader& reader );
        void               resetClient( int32 connectionId );

        vector<ClientState>                 _listClient;
        NetSnapshot                         _world;
        ReplicationServerSettings           _settings;
        NetHost*                            _pHost;
        const IReplicationPolicy*           _pPolicy;
        IReplicationPolicy                  _defaultPolicy;
        NetParallelFor                      _parallel;
        NetParallelScratch<SnapshotScratch> _snapshotScratch;       ///< 스레드마다 하나
        vector<int32>                       _listConnectionScratch; ///< 이번 틱에 보낼 연결
        vector<ClientState*>                _listClientScratch;     ///< 위 연결의 상태 — 나누기 전에 모두 잡아 둔다(나누는 중에 목록이 자라지 않게)
        uint64                              _oversizedEntityCount;
        const int32*                        _pRangeConnection; ///< 나눈 본문이 읽는 `_listConnectionScratch.data()` — 워커는 컨테이너를 만지지 않는다
        ClientState* const*                 _ppRangeClient;    ///< 나눈 본문이 읽는 `_listClientScratch.data()`
    };
} // namespace sw
