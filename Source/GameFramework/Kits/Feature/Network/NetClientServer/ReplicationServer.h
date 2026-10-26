/**
 * @file ReplicationServer.h
 * @brief 권위 서버 쪽 복제기입니다. 매 틱 월드 상태를 모아, 클라이언트마다 확인받은 스냅샷을 기준으로 한 델타를 보냅니다.
 * @details 어떤 엔티티를 보낼지는 관련성 정책과 우선도 정책으로 정한다.
 *          받은 입력은 틱 순서로 꺼내 주고, "빈틈없이 받은 다음 틱" 을 스냅샷에 실어 돌려준다. 클라이언트는 그 틱부터 입력을 다시 보낸다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Replication/NetInputWindow.h"
#include "Core/Network/Replication/NetParallel.h"
#include "Core/Network/Replication/NetPrioritizer.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/NetSnapshot.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    struct ControlIntent;

    class INetPeers;

    /**
     * @class IReplicationPolicy
     * @brief 무엇을 누구에게 얼마나 먼저 보낼지 정하는 정책입니다. 게임마다 교체합니다.
     * @details 배틀로얄은 거리, 비대칭 대전은 시야와 역할을 기준으로 하고, 기체 대전은 전부 보낸다.
     * @warning 서버에 `setTaskManager` 를 지정하면 두 함수가 **여러 스레드에서 동시에** 불린다. 상태를 바꾸지 말고 읽기만 해야 한다.
     *          읽는 게임 상태는 스냅샷을 보내는 동안 바뀌지 않아야 하며, 보통 틱 끝에 보내므로 이 조건이 지켜진다.
     */
    class SW_KIT_API IReplicationPolicy
    {
    public:
        IReplicationPolicy()          = default;
        virtual ~IReplicationPolicy() = default;

        IReplicationPolicy( const IReplicationPolicy& )            = default;
        IReplicationPolicy& operator=( const IReplicationPolicy& ) = default;

        /** @brief 이 클라이언트에게 이 엔티티를 보내야 하면 true 를 반환합니다. 보내지 않으면 클라이언트에서 그 엔티티가 사라집니다. */
        virtual bool isRelevant( int32 connectionID, const NetEntityState& entity ) const
        {
            (void)connectionID;
            (void)entity;
            return true;
        }
        /**
         * @brief 스냅샷마다 쌓는 우선도를 반환합니다. 클수록 자주 보냅니다.
         * @details 쌓인 우선도가 큰 엔티티부터 예산에 싣고, 실으면 0으로 되돌린다.
         *          예산이 항상 차 있어도 낮은 우선도가 계속 쌓이므로 언젠가는 전송된다(`NetPrioritizer`).
         *          우선도 1인 엔티티는 우선도 10인 엔티티가 예산을 채우는 틱 열 번에 한 번쯤 전송된다.
         */
        virtual float32 computePriority( int32 connectionID, const NetEntityState& entity ) const
        {
            (void)connectionID;
            (void)entity;
            return 1.0f;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 서버 설정입니다. 범위는 칸의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `ReplicationServer::initialize` 가 오류로 거절합니다(`ConfigRangeUtil`).
     * @details 예전에는 기록 수와 입력 버퍼 크기를 쓸 때 조용히 2 · 1 로 올렸다(폴백 금지 원칙).
     *          예산의 `Max` 는 `NetConnection::kMaxSingleMessageSize` 상수 이름이라 연결의 한 메시지 상한과 어긋나지 않는다.
     */
    REFLECT()
    struct SW_KIT_API ReplicationServerConfig
    {
        REFLECT_BODY();
        PROPERTY( Min = 1, Max = NetConnection::kMaxSingleMessageSize )
        int32 _snapshotBudgetBytes{ 1000 }; ///< 클라이언트 한 명당 틱마다 보내는 스냅샷 메시지의 상한 바이트(1 ~ `NetConnection::kMaxSingleMessageSize`). 종류 바이트, 헤더, 사라진 목록을 포함한다.
                                            ///< 연결 상한에서 나온 할당량(`NetSendBudget::computeTickBudget`)이 더 작으면 그 값을 쓴다
        PROPERTY( Min = 0.001, Units = s )
        float32 _tickInterval{ 1.0f / 30.0f }; ///< `sendSnapshots` 를 부르는 간격(초). 연결 상한에서 스냅샷 하나의 할당량을 계산할 때 쓰며, 클라이언트 설정의 같은 이름과 맞춘다
        PROPERTY( Min = 2 )
        int32 _historySize{ 64 }; ///< 클라이언트마다 기억하는 보낸 스냅샷(기준 후보)의 수
        PROPERTY( Min = 1 )
        int32 _inputBufferSize{ 64 }; ///< 클라이언트마다 받아 두는 입력 틱 수. 받은 가장 새 틱에서 이만큼 뒤까지 보관하고 더 오래된 입력은 버린다
        PROPERTY( Min = 0, Max = 256 )
        int32 _maxViewLagTicks{ 32 }; ///< 입력 스탬프의 보던 틱을 꺼내는 틱에서 이만큼 뒤까지만 받는다(서버 클램프). 랙 보정 기록(`LagCompensationHistory`)보다 길게 잡지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReplicationServer
     * @code
     *     server.beginTick( tick );
     *     for ( each entity ) server.setEntity( id, typeID, bytes );
     *     server.endTick();
     *     server.sendSnapshots();
     *     // 받은 메시지와 연결 이벤트: router.registerHandler( &server ) 가 돌려준 구독을 쥔 뒤 매 틱 router.pump( host ) 를 부른다.
     *     //                           직접 처리하려면 server.handleMessage( connectionID, bytes ), server.onConnectionOpened / onConnectionClosed 를 쓴다
     *     // 시뮬레이션: server.popInput( connectionID, tick, inputBytes ); ... (void)server.setOwnerState( connectionID, tick, pawnStateBytes );
     * @endcode
     */
    class SW_KIT_API ReplicationServer : public INetMessageHandler
    {
    public:
        ReplicationServer();

        /**
         * @brief 복제를 시작합니다. 설정이 범위 밖이면 오류를 남기고 false 를 반환합니다.
         * @details 거절된 서버는 호스트가 없는 서버와 같다. `sendSnapshots` 가 아무것도 보내지 않는다.
         */
        [[nodiscard]] bool initialize( INetPeers* pHost, const ReplicationServerConfig& settings, const IReplicationPolicy* pPolicy = nullptr );
        void               beginTick( uint32 tick );
        /** @brief 이 틱의 엔티티를 등록합니다. 상태가 `NetSnapshot::kMaxEntityBytes` 를 넘으면 복제하지 않으며, 처음 한 번만 경고하고 그 뒤로는 횟수만 셉니다. */
        void setEntity( uint32 entityID, uint32 typeID, const vector<uint8>& buffer );
        /** @brief 이 틱의 엔티티를 확정하고 id 순으로 정렬합니다. 그 전까지 월드에는 지난 틱의 엔티티가 남아 있으므로 `sendSnapshots` 와 `getWorldSnapshot` 은 이 함수 뒤에 씁니다. */
        void endTick();
        /**
         * @brief 연결된 클라이언트마다 스냅샷을 보냅니다.
         * @details `setTaskManager` 를 지정했으면 클라이언트를 작업 스레드에 나눠 스냅샷을 만든다. 클라이언트마다 독립적이므로 결과는 한 스레드로 돌릴 때와 같다.
         */
        void sendSnapshots();
        /** @brief 스냅샷 생성을 나눠 처리할 작업 스레드 풀을 지정합니다. 게임은 `&engine::getSubsystem<TaskManager>()` 를 넘깁니다. nullptr 이면 호출한 스레드에서 처리합니다. */
        void            setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold = NetParallelFor::kDefaultSerialThreshold );
        uint8           getMessageRangeBase() const override { return NetKitMessageRange::kClientServer; }
        uint16          getMessageKindMask() const override;
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 연결별 상태(확인 틱, 입력 큐, 처리한 입력 틱)를 초기화합니다. 같은 슬롯에 새로 온 클라이언트가 이전 상태를 물려받지 않게 합니다. */
        void onConnectionOpened( int32 connectionID ) override;
        void onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override;

        /**
         * @brief 그 틱의 입력을 꺼냅니다. 아직 도착하지 않았으면 가장 최근 입력을 반복하고 @p outbExact 를 false 로 둡니다. 받은 입력이 하나도 없으면 false 를 반환합니다.
         * @details 그 틱까지는 처리한 것으로 본다. 그 틱 이전의 입력은 더 받지 않고, 스냅샷의 확인 틱도 다음 틱으로 넘어간다.
         */
        [[nodiscard]] bool popInput( int32 connectionID, uint32 tick, vector<uint8>& outInputBuffer, bool& outbExact );
        /**
         * @brief `popInput` 의 바이트를 의도(`ControlIntent::read`)로 읽습니다. 클라이언트의 `ReplicationClient::sendIntent` 와 짝입니다.
         * @details 되풀이한 입력(@p outbExact 가 false)이면 발동 비트를 지웁니다. 받은 입력이 없거나 깨졌으면 false 이고 @p outIntent 는 그대로입니다.
         */
        [[nodiscard]] bool popIntent( int32 connectionID, uint32 tick, ControlIntent& outIntent, bool& outbExact );
        /**
         * @brief 이 클라이언트가 조종하는 엔티티의 상태를, 그 상태를 만든 마지막 입력 틱과 함께 정합니다. 다음 스냅샷부터 예산과 관련 정책에 상관없이 통째로 싣습니다.
         * @details 틱과 상태는 늘 한 번에 정한다. 상태가 `NetSnapshot::kMaxEntityBytes` 를 넘으면 오류를 남기고 정하지 않으며 false 를 반환한다.
         */
        [[nodiscard]] bool setOwnerState( int32 connectionID, uint32 inputTick, const vector<uint8>& buffer );
        /**
         * @brief 마지막으로 꺼낸 입력(`popInput`)을 만들 때 클라이언트가 보던 틱입니다. 보간 지연만큼 과거이며 소수이고, 랙 보정에서 되감을 때 씁니다.
         * @details 입력 항목마다 온 스탬프이므로 다시 보낸 옛 입력은 그 입력의 시점이다. 꺼내는 틱 이후와 `_maxViewLagTicks` 보다 오래된 값은 잘라 낸다.
         */
        float32            getClientViewTick( int32 connectionID ) const;
        uint32             getAckedTick( int32 connectionID ) const;
        const NetSnapshot& getWorldSnapshot() const { return _world; }
        /** @brief 상한을 넘어서 복제하지 않은 `setEntity` 호출 수입니다. */
        uint64 getOversizedEntityCount() const;

    private:
        struct ClientState
        {
            NetPrioritizer              _prioritizer{}; ///< 관련 엔티티마다 쌓인 우선도
            TickRingBuffer<NetSnapshot> _listSent{};    ///< 보낸 스냅샷(재구성한 것). 클라이언트가 확인한 틱의 스냅샷을 델타의 기준으로 찾는다
            NetInputReceiveBuffer       _input{};       ///< 받은 입력. 받은 가장 새 틱을 따라가는 창이며 꺼낸 틱은 버린다
            vector<uint8>               _lastInput{};
            NetOwnerState               _ownerState{}; ///< 이 클라이언트가 조종하는 엔티티의 상태와 그 입력 틱. 스냅샷마다 싣는다
            float32                     _viewTick{ 0.0f };
            uint32                      _ackedTick{ 0 };
            uint8                       _bHasAck{ SW_FALSE };
            uint8                       _bHasInput{ SW_FALSE };
            uint8                       _bActive{ SW_FALSE };
        };

        /** @brief 작업 스레드 하나가 스냅샷 하나를 만드는 데 쓰는 스크래치 버퍼입니다. 틱마다 다시 사용합니다. */
        struct SnapshotScratch
        {
            NetSnapshot      _filtered{}; ///< 클라이언트 하나에 관련된 엔티티
            vector<uint32>   _listOrderEntity{};
            vector<int32>    _listOrder{};   ///< 위 순서를 `_filtered` 의 인덱스로 바꾼 것
            vector<uint8>    _listCurrent{}; ///< `_filtered` 의 항목마다 `writeDelta` 가 낸 결과(`NetSnapshot::kEntity*`)
            NetMessageWriter _messageWriter{};
        };

        ClientState& acquireClient( int32 connectionID );
        void         sendSnapshotRange( uint32 start, uint32 end );
        void         sendSnapshot( int32 connectionID, ClientState& client, SnapshotScratch& scratch );
        /** @brief 이 클라이언트가 확인을 기다리는 전송을 판정합니다. 확인한 틱이 보낸 틱을 지났는데 확인된 스냅샷에 그때 보낸 상태가 없으면 잃은 것으로 보고 우선도를 되돌립니다. */
        void               resolveUnconfirmedSends( ClientState& client, const NetSnapshot& filtered ) const;
        [[nodiscard]] bool handleInput( ClientState& client, BitReader& reader );
        void               resetClient( int32 connectionID );

        vector<ClientState>                 _listClient;
        NetSnapshot                         _world;
        ReplicationServerConfig             _settings;
        INetPeers*                          _pHost;
        const IReplicationPolicy*           _pPolicy;
        IReplicationPolicy                  _defaultPolicy;
        NetParallelFor                      _parallel;
        NetParallelScratch<SnapshotScratch> _snapshotScratch;       ///< 스레드마다 하나씩 둔다
        vector<int32>                       _listConnectionScratch; ///< 이번 틱에 보낼 연결
        vector<ClientState*>                _listClientScratch;     ///< 위 연결의 상태. 나눠 실행하기 전에 모두 찾아 두어, 실행 중에 목록이 커지지 않게 한다
        size_t                              _worldEntityCount;      ///< 이 틱에 `setEntity` 를 호출한 수. 월드 엔티티 버퍼는 틱을 넘어 재사용하므로 용량을 남겨 둔다
        uint64                              _oversizedEntityCount;
        int32                               _snapshotBudgetBytes; ///< 이번 `sendSnapshots` 의 예산. 나눠 실행하기 전에 정하며 워커는 읽기만 한다
        const int32*                        _pRangeConnection;    ///< 나눠 실행하는 본문이 읽는 `_listConnectionScratch.data()`. 워커는 컨테이너를 직접 만지지 않는다
        ClientState* const*                 _ppRangeClient;       ///< 나눠 실행하는 본문이 읽는 `_listClientScratch.data()`
    };
} // namespace sw
