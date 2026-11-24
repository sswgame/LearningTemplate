/**
 * @file ReplicationServer.h
 * @brief 권위 서버 쪽 복제기입니다. 매 틱 월드 상태를 모아, 클라이언트마다 확인받은 스냅샷을 기준으로 한 델타를 보냅니다.
 * @details 어떤 엔티티를 보낼지는 관련성 정책과 우선도 정책으로 정한다.
 *          **휴면**: `_dormancyTicks` 동안 판이 오르지 않은 엔티티는 연결마다의 관련성 · 우선도 평가에서 빠진다(언리얼 Dormancy 자리). 연결은 휴면 직전의 판단을 들고 가며,
 *          받는 쪽이 이미 지금 판을 가진 휴면 엔티티는 기준에서 그대로 둔다(평가 · 우선도 · 델타를 모두 건너뛴다). 깨우는 자리는 판을 올리는 `endTick` 과
 *          정책의 관련 여부 바뀜 알림(`IReplicationPolicy::collectWokenEntities`) 둘뿐이다 — 상태를 쓰는 쪽은 깨우는 함수를 따로 부르지 않는다.
 *          새 연결은 첫 스냅샷에서 휴면 엔티티까지 한 번 평가한다(언리얼 `HandleConnectionAdded` 가 휴면 목록을 되살리는 자리).
 *          받은 입력은 틱 순서로 꺼내 주고, "빈틈없이 받은 다음 틱" 을 스냅샷에 실어 돌려준다. 클라이언트는 그 틱부터 입력을 다시 보낸다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
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
    struct MeleeHitClaim;

    class INetPeers;

    /** @brief 연결 하나에 보낼 엔티티 하나 — 월드 목록(`NetSnapshot::_listEntity`, id 순) 안의 번호와 이번 스냅샷의 우선도입니다. */
    struct NetRelevantEntity
    {
        uint32  _worldIndex{ 0 };
        float32 _priority{ 0.0f };
    };

    /**
     * @class IReplicationPolicy
     * @brief 무엇을 누구에게 얼마나 먼저 보낼지 정하는 정책입니다. 게임마다 교체합니다.
     * @details 배틀로얄은 거리, 비대칭 대전은 시야와 역할을 기준으로 하고, 기체 대전은 전부 보낸다.
     * @warning 서버에 `setTaskManager` 를 지정하면 const 함수들이 **여러 스레드에서 동시에** 불린다. 상태를 바꾸지 말고 읽기만 해야 한다.
     *          상태를 바꾸는 준비는 `prepareSnapshots`(게임 스레드, 나눠 돌기 전 한 번)에서만 한다.
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
        /**
         * @brief 관련 여부와 우선도를 함께 구합니다. 답은 `isRelevant` · `computePriority` 와 같아야 합니다.
         * @details 엔티티 × 연결마다 불리므로 두 질문이 같은 표를 찾는 정책은 한 번에 답하게 재정의한다(`InterestReplicationPolicy`).
         * @return 관련 있으면 true 이고 @p outPriority 를 채웁니다.
         */
        virtual bool evaluateEntity( int32 connectionID, const NetEntityState& entity, float32& outPriority ) const
        {
            if ( isRelevant( connectionID, entity ) == false )
                return false;
            outPriority = computePriority( connectionID, entity );
            return true;
        }
        /**
         * @brief 이번 틱 스냅샷을 보내기 전에 한 번 불립니다(게임 스레드, `ReplicationServer::sendSnapshots` 가 연결을 나눠 돌기 전). 연결이 함께 쓰는 표를 세운다.
         * @param listWorldEntity 이번 틱 월드 엔티티(id 오름차순, `ReplicationServer::endTick` 이 정렬했다)
         */
        virtual void prepareSnapshots( const vector<NetEntityState>& listWorldEntity ) { (void)listWorldEntity; }
        /**
         * @brief 엔티티 상태 밖의 까닭(관찰자 이동 · 자리 지움 등)으로 관련 여부가 바뀐 엔티티 id 를 @p outListEntityID 에 더하고 잊습니다(게임 스레드, `prepareSnapshots` 뒤 한 번).
         * @details 서버는 이 엔티티를 휴면에서 깨워 연결마다 다시 평가한다. 관련 여부가 엔티티 상태와 이 알림 말고 다른 것으로 바뀌는 정책은 휴면 엔티티의 판단이 굳는다.
         */
        virtual void collectWokenEntities( vector<uint32>& outListEntityID ) { (void)outListEntityID; }
        /**
         * @brief 연결 하나에 보낼 엔티티를 @p listCandidate 안에서 골라 월드 목록 차례(번호 오름차순)로 @p outList 에 지우고 채웁니다.
         * @param listCandidate 평가할 엔티티의 월드 번호(오름차순)입니다. 보통 깨어 있는 엔티티이고, 새 연결의 첫 스냅샷에는 전부다. 밖의 엔티티는 묻지 않는다(휴면).
         * @details 기본은 후보마다 `evaluateEntity` 를 묻는다(후보 × 연결). 관찰자마다 보이는 목록을 아는 정책은 그 목록만 돌게 재정의한다(`InterestReplicationPolicy`).
         *          재정의한 결과는 기본 구현과 같아야 한다 — 차례 · 우선도가 같으면 스냅샷 바이트가 같다(테스트 `InterestReplicationPolicyTest` 가 바이트로 대조한다).
         */
        virtual void collectRelevantEntities( int32 connectionID, const vector<NetEntityState>& listWorldEntity, const vector<uint32>& listCandidate,
                                              vector<NetRelevantEntity>& outList ) const;
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
        int32 _historySize{ 64 }; ///< 클라이언트마다 기억하는 보낸 스냅샷 기록(기준 후보)의 수. 기록은 엔티티마다 (id, 판)만 들므로 메모리는 엔티티 수 × 이 값에 비례한다
        PROPERTY( Min = 1 )
        int32 _inputBufferSize{ 64 }; ///< 클라이언트마다 받아 두는 입력 틱 수. 받은 가장 새 틱에서 이만큼 뒤까지 보관하고 더 오래된 입력은 버린다
        PROPERTY( Min = 0, Max = 256 )
        int32 _maxViewLagTicks{ 32 }; ///< 입력 스탬프의 보던 틱을 꺼내는 틱에서 이만큼 뒤까지만 받는다(서버 클램프). 랙 보정 기록(`LagCompensationHistory`)보다 길게 잡지 않는다
        PROPERTY( Min = 1 )
        int32 _maxInputCatchUpTicks{ 15 }; ///< 한 연결이 모아 둘 수 있는 입력 몫(틱 수). 늦게 몰려 온 입력(끊김 · 손실 재전송)을 이만큼까지 한 번에 따라잡는다(`popNextIntent`)
        PROPERTY( Min = 0, Max = 1 )
        float32 _inputRateTolerance{ 0.05f }; ///< 입력 몫을 채우는 빠르기의 여유(비율). 두 시계의 어긋남과 서버 멈춤 뒤의 밀린 입력을 이만큼 빠르게 소화한다(언리얼 이동 시간 어긋남 감지의 drift 허용 자리)
        PROPERTY( Min = 1 )
        int32 _dormancyTicks{ 60 }; ///< 이만큼 틱 동안 판이 오르지 않고 깨워지지도 않은 엔티티는 휴면이다 — 연결마다의 관련성 · 우선도 평가에서 빠진다(머리말)
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
     *     // 받은 메시지와 연결 이벤트: router.registerHandler( &server ) 가 돌려준 구독을 쥔 뒤 매 틱 router.poll( host ) 를 부른다.
     *     //                           직접 처리하려면 server.handleMessage( connectionID, bytes ), server.onConnectionOpened / onConnectionClosed 를 쓴다
     *     // 시뮬레이션: server.popInput( connectionID, tick, inputBytes ); ... (void)server.setOwnerState( connectionID, tick, pawnStateBytes );
     * @endcode
     */
    class SW_KIT_API ReplicationServer : public NetTableHandler<ReplicationServer, NetClientServerMessage::Table, NetClientServerMessage::SnapshotAck, NetClientServerMessage::Input>
    {
    public:
        ReplicationServer();

        /**
         * @brief 복제를 시작합니다. 설정이 범위 밖이면 오류를 남기고 false 를 반환합니다.
         * @details 거절된 서버는 호스트가 없는 서버와 같다. `sendSnapshots` 가 아무것도 보내지 않는다.
         */
        [[nodiscard]] bool initialize( INetPeers* pHost, const ReplicationServerConfig& settings, IReplicationPolicy* pPolicy = nullptr );
        void               beginTick( uint32 tick );
        /** @brief 이 틱의 엔티티를 등록합니다. 상태가 `NetSnapshot::kMaxEntityBytes` 를 넘으면 복제하지 않으며, 처음 한 번 오류를 남기고(상태 스키마의 결함이다 — 테스트 · 시나리오가 멈춘다) 그 뒤로는 횟수만 셉니다. */
        void setEntity( uint32 entityID, uint32 typeID, const vector<uint8>& buffer );
        /**
         * @brief 이 틱의 엔티티를 확정하고 id 순으로 정렬합니다. 그 전까지 월드에는 지난 틱의 엔티티가 남아 있으므로 `sendSnapshots` 와 `getWorldSnapshot` 은 이 함수 뒤에 씁니다.
         * @details 지난 틱과 종류나 바이트가 다른 엔티티는 새 판 번호(`NetEntityState::_revision`)를 받는다 — 판을 올리는 자리는 여기 하나다.
         *          값이 A → B → A 로 돌아와도 새 판이다(지난 틱하고만 비교한다). 그만큼 다시 실을 뿐 받는 쪽 상태는 맞다.
         */
        void endTick();
        /**
         * @brief 연결된 클라이언트마다 스냅샷을 보냅니다.
         * @details `setTaskManager` 를 지정했으면 클라이언트를 작업 스레드에 나눠 스냅샷을 만든다. 클라이언트마다 독립적이므로 결과는 한 스레드로 돌릴 때와 같다.
         */
        void sendSnapshots();
        /** @brief 스냅샷 생성을 나눠 처리할 작업 스레드 풀을 지정합니다. 게임은 `game::findSubsystem<TaskManager>()` 를 넘깁니다(작업 풀은 `GameVisible`). nullptr 이면 호출한 스레드에서 처리합니다. */
        void            setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold = NetParallelFor::kDefaultSerialThreshold );
        NetHandleResult receive( NetClientServerMessage::SnapshotAck, const NetMessageContext& context, const NetSnapshotAckBody& body );
        NetHandleResult receive( NetClientServerMessage::Input, const NetMessageContext& context, BitReader& body );
        /** @brief 연결별 상태(확인 틱, 입력 큐, 처리한 입력 틱)를 초기화합니다. 같은 슬롯에 새로 온 클라이언트가 이전 상태를 물려받지 않게 합니다. */
        void onConnectionOpened( int32 connectionID ) override;
        void onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override;

        /**
         * @brief 그 틱의 입력을 꺼냅니다. 아직 도착하지 않았으면 가장 최근 입력을 반복하고 @p outbExact 를 false 로 둡니다. 받은 입력이 하나도 없으면 false 를 반환합니다.
         * @details 그 틱까지는 처리한 것으로 본다. 그 틱 이전의 입력은 더 받지 않고, 스냅샷의 확인 틱도 다음 틱으로 넘어간다.
         */
        /**
         * @brief 이 연결에서 받아 둔 가장 오래된 입력 틱입니다. 없으면 비어 있습니다.
         * @details 클라이언트는 접속 전에도 틱을 세므로 첫 입력의 틱을 서버가 미리 알 수 없다. 조종을 맡을 때 여기서 시작한다.
         */
        [[nodiscard]] optional<uint32> findOldestInputTick( int32 connectionID ) const;
        [[nodiscard]] bool             popInput( int32 connectionID, uint32 tick, vector<uint8>& outInputBuffer, bool& outbExact );
        /**
         * @brief `popInput` 의 바이트를 의도와 근접 맞음 주장으로 읽습니다. 클라이언트의 `ReplicationClient::sendIntent` 와 짝입니다.
         * @details 되풀이한 입력(@p outbExact 가 false)이면 발동 비트 · 발동 순간의 시선 · 주장을 지웁니다 — 한 번 누름 · 한 번 맞음이 두 번 되지 않게. 받은 입력이 없거나 깨졌으면 false 이고
         *          @p outIntent 는 그대로입니다. @p outListClaim 은 늘 비우고 시작합니다. 주장의 `_frame` 은 입력 틱입니다.
         */
        [[nodiscard]] bool popIntent( int32 connectionID, uint32 tick, ControlIntent& outIntent, bool& outbExact, vector<MeleeHitClaim>& outListClaim, uint32* pOutPawnEntityID = nullptr );
        /**
         * @brief 흐른 실제 시간 @p elapsedSeconds 만큼 연결마다 꺼낼 수 있는 입력 몫을 채웁니다(`_maxInputCatchUpTicks` 까지). `popNextIntent` 를 쓰는 게임이 프레임마다 한 번 부릅니다.
         */
        void accumulateInputBudget( float32 elapsedSeconds );
        /**
         * @brief 이 연결에서 받아 둔 가장 오래된 입력 하나를 꺼냅니다 — 서버 틱에 묶지 않고 도착한 입력만큼, 다만 흐른 시간의 몫 안에서 꺼낸다.
         * @details 언리얼 `ServerMove` 의 모양이다: 클라이언트의 입력이 올 때 그만큼 움직이고(늦으면 기다렸다가 몰아서), 걸음 수는 시간의 몫이 막는다(속도 치트 — 빠른 시계로 입력을 더 보내도
         *          몫을 넘은 입력은 창에서 밀려나 서버의 자리는 그대로고, 그 클라이언트가 되돌아온다). 서버 틱마다 하나씩 꺼내며 늦은 입력을 버리는 `popIntent` 는 클라이언트가 한 번 멈추면
         *          그 뒤의 입력이 영영 늦어 조종이 듣지 않았다.
         * @return 꺼낸 입력 틱입니다. 받은 입력이 없거나 몫이 없거나 깨진 입력이면 비어 있습니다.
         */
        [[nodiscard]] optional<uint32> popNextIntent( int32 connectionID, ControlIntent& outIntent, uint32& outPawnEntityID, vector<MeleeHitClaim>& outListClaim );
        /**
         * @brief 이 클라이언트가 조종하는 엔티티의 상태를, 그 상태를 만든 마지막 입력 틱과 함께 정합니다. 다음 스냅샷부터 예산과 관련 정책에 상관없이 통째로 싣습니다.
         * @details 틱과 상태는 늘 한 번에 정한다. 상태가 `NetSnapshot::kMaxEntityBytes` 를 넘으면 오류를 남기고 정하지 않으며 false 를 반환한다.
         */
        [[nodiscard]] bool setOwnerState( int32 connectionID, uint32 inputTick, const vector<uint8>& buffer );
        uint32             getAckedTick( int32 connectionID ) const;
        const NetSnapshot& getWorldSnapshot() const { return _world; }
        /** @brief 상한을 넘어서 복제하지 않은 `setEntity` 호출 수입니다. */
        uint64 getOversizedEntityCount() const;

    private:
        /** @brief 연결이 지난 스냅샷에서 관련 있다고 본 엔티티 하나 — 휴면 엔티티는 다시 묻지 않고 이 판단을 들고 간다. */
        struct RelevantRecord
        {
            uint32  _entityID{ 0 };
            float32 _priority{ 0.0f };
        };

        struct ClientState
        {
            NetPrioritizer                  _prioritizer{}; ///< 관련 엔티티마다 쌓인 우선도
            TickRingBuffer<NetSentSnapshot> _listSent{};    ///< 보낸 스냅샷의 기록(엔티티 · 판). 클라이언트가 확인한 틱의 기록을 델타의 기준으로 찾는다
            NetInputReceiveBuffer           _input{};       ///< 받은 입력. 받은 가장 새 틱을 따라가는 창이며 꺼낸 틱은 버린다
            vector<uint8>                   _lastInput{};
            NetOwnerState                   _ownerState{}; ///< 이 클라이언트가 조종하는 엔티티의 상태와 그 입력 틱. 스냅샷마다 싣는다
            float64                         _viewTick{ 0.0 };
            float32                         _inputBudget{ 0.0f };      ///< 지금 꺼낼 수 있는 입력 수(`accumulateInputBudget` 이 채우고 `popNextIntent` 가 쓴다)
            int32                           _snapshotBudgetBytes{ 0 }; ///< 이번 `sendSnapshots` 의 예산(그 연결의 지금 송신 속도에서). 나눠 실행하기 전에 정하며 워커는 읽기만 한다
            uint32                          _ackedTick{ 0 };
            vector<RelevantRecord>          _listLastRelevant{}; ///< 지난 스냅샷의 관련 엔티티(id 오름차순). 휴면 엔티티의 판단을 이어 간다
            uint8                           _bHasAck{ SW_FALSE };
            uint8                           _bHasInput{ SW_FALSE };
            uint8                           _bActive{ SW_FALSE };
            uint8                           _bFullyEvaluated{ SW_FALSE }; ///< 휴면 엔티티까지 한 번 평가했다(새 연결의 첫 스냅샷)
        };

        /** @brief 작업 스레드 하나가 스냅샷 하나를 만드는 데 쓰는 스크래치 버퍼입니다. 틱마다 다시 사용합니다. */
        struct SnapshotScratch
        {
            vector<uint32>            _listRelevantIndex{}; ///< 클라이언트 하나에 관련된 엔티티의 월드 번호(오름차순 — 곧 id 순). 상태 바이트는 복사하지 않는다
            vector<uint32>            _listOrderEntity{};
            vector<int32>             _listOrder{};    ///< 위 순서를 `_listRelevantIndex` 의 인덱스로 바꾼 것
            vector<uint8>             _listCurrent{};  ///< `_listRelevantIndex` 의 항목마다 `writeDelta` 가 낸 결과(`NetSnapshot::kEntity*`)
            vector<NetRelevantEntity> _listRelevant{}; ///< 정책이 고른 이 연결의 엔티티(`IReplicationPolicy::collectRelevantEntities`)
            vector<NetRelevantEntity> _listMerged{};   ///< 위에 지난 판단을 이어 가는 휴면 엔티티를 월드 차례로 합친 것
            NetMessageWriter          _messageWriter{};
        };

        ClientState& acquireClient( int32 connectionID );
        void         sendSnapshotRange( uint32 start, uint32 end );
        void         sendSnapshot( int32 connectionID, ClientState& client, SnapshotScratch& scratch );
        /** @brief 이 클라이언트가 확인을 기다리는 전송을 판정합니다. 확인한 틱이 보낸 틱을 지났는데 확인된 스냅샷에 그때 보낸 상태가 없으면 잃은 것으로 보고 우선도를 되돌립니다. */
        void               resolveUnconfirmedSends( ClientState& client, const vector<uint32>& listRelevantIndex ) const;
        [[nodiscard]] bool handleInput( ClientState& client, BitReader& reader );
        void               resetClient( int32 connectionID );
        /** @brief 지금 틱에 휴면인가입니다 — 깨어난 틱에서 `_dormancyTicks` 가 지났다. 틱이 뒤로 갔으면 깨어 있다고 본다. */
        bool isEntityDormant( const NetEntityState& entity ) const;
        /** @brief 정책이 알린 엔티티를 깨우고 이번 틱의 후보 목록(깨어 있는 것 · 전부)을 세웁니다(게임 스레드, 나눠 돌기 전). */
        void prepareCandidates( bool bNeedsAllIndex );

    private:
        vector<ClientState>                 _listClient;
        NetSnapshot                         _world;             ///< 확정한 월드(id 순, 판 번호 포함)
        vector<NetEntityState>              _listPendingEntity; ///< 이번 틱에 `setEntity` 로 받는 엔티티. `endTick` 이 지난 월드와 비교해 판을 정한 뒤 `_world` 와 맞바꾼다(버퍼 용량은 두 목록이 돌려 쓴다)
        ReplicationServerConfig             _settings;
        INetPeers*                          _pHost;
        IReplicationPolicy*                 _pPolicy; ///< 틱마다 `prepareSnapshots` 를 부르므로 const 가 아니다
        IReplicationPolicy                  _defaultPolicy;
        NetParallelFor                      _parallel;
        NetParallelScratch<SnapshotScratch> _snapshotScratch;       ///< 스레드마다 하나씩 둔다
        vector<uint32>                      _listAwakeIndex;        ///< 이번 틱에 깨어 있는 엔티티의 월드 번호(오름차순) — 연결마다의 평가 후보
        vector<uint32>                      _listAllIndex;          ///< 모든 월드 번호(새 연결의 첫 스냅샷이 있는 틱에만 채운다)
        vector<uint32>                      _listWokenScratch;      ///< 정책이 알린 깨울 엔티티 id
        vector<int32>                       _listConnectionScratch; ///< 이번 틱에 보낼 연결
        vector<ClientState*>                _listClientScratch;     ///< 위 연결의 상태. 나눠 실행하기 전에 모두 찾아 두어, 실행 중에 목록이 커지지 않게 한다
        size_t                              _pendingEntityCount;    ///< 이 틱에 `setEntity` 를 호출한 수. 엔티티 버퍼는 틱을 넘어 재사용하므로 용량을 남겨 둔다
        uint64                              _lastRevision;          ///< 마지막으로 나눠 준 판 번호. 0 은 "판 없음" 이라 1 부터 준다
        uint64                              _oversizedEntityCount;
        const int32*                        _pRangeConnection; ///< 나눠 실행하는 본문이 읽는 `_listConnectionScratch.data()`. 워커는 컨테이너를 직접 만지지 않는다
        ClientState* const*                 _ppRangeClient;    ///< 나눠 실행하는 본문이 읽는 `_listClientScratch.data()`
    };
} // namespace sw
