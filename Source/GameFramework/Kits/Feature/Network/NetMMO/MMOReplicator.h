/**
 * @file MMOReplicator.h
 * @brief 엔티티가 수천 개여도 관찰자(플레이어)마다 근처의 중요한 엔티티를 정해진 바이트 안에서 보냅니다.
 * @details 동작은 세 부분으로 나뉜다.
 *          1. 관심 영역: Core 의 관심 영역 격자(`NetInterestGrid` — XZ 평면 공간 해시)로 근처 엔티티를 찾는다.
 *             나가는 반경을 들어오는 반경보다 크게 잡아서 경계에서 엔티티가 들락날락하지 않게 한다(히스테리시스).
 *             해시의 칸은 월드 스트리밍 격자(`WorldCellGrid`, D1-7)와 같은 것이다 — `initialize` 가 격자를 받고 칸 크기를 따로 두지 않는다.
 *             서버는 관찰자 둘레의 셀만 올리므로(`feedStreamingSources` → `WorldStreamingSubsystem::setExternalSources`) 나가는 반경이
 *             격자의 로딩 반경보다 크면 보이는 엔티티가 올라 있지 않은 셀에 있을 수 있다. 그 설정은 `initialize` 가 거절한다.
 *          2. 전송: 틱마다 새로 보이는 엔티티를 신뢰 메시지 하나에 묶어 전체 상태로 보낸다(Core 가 64 KB까지 조각으로 나눠 전송한다). 갱신은 비신뢰 메시지로 보낸다.
 *             갱신은 전달 통지(`INetPeers::sendNotifiedMessage`, 값은 서버 틱)를 달아 보내고, 연결 계층의 패킷 확인이 그 판정을 돌려준다(`onDeliveryNotice`).
 *             클라이언트가 확인 메시지를 따로 보내지 않는다(언리얼의 패킷 ACK · NAK 와 같다).
 *             갱신을 잃은 엔티티는 우선도와 "바뀜" 가중치를 되돌려서 곧 다시 보낸다.
 *             보이지 않게 된 엔티티는 조각나지 않는 크기(1 KB)의 메시지 여러 개로 나눠 알리고, 보낸 것만 보이는 목록에서 뺀다.
 *             그래서 신뢰 창이 차도 앞쪽 메시지는 나가고 남은 것은 다음 틱에 보낸다.
 *             새로 보임과 갱신 메시지에는 서버 틱을 싣는다. 클라이언트는 엔티티마다 마지막으로 적용한 틱보다 오래된 메시지를 버린다.
 *             순서가 뒤바뀐 비신뢰 갱신이 새 상태를 덮어쓰지 않게 하기 위해서이다.
 *          3. 우선도 누적(`NetPrioritizer`): 보이는 엔티티마다 매 틱 우선도를 쌓는다. 우선도는 정책(거리, 중요도)이 정한다.
 *             예산 안에서 우선도가 큰 순서로 보내고, 보낸 엔티티의 우선도는 0으로 되돌린 뒤 확인을 기다린다. 잃었으면 우선도를 되돌려 놓는다.
 *             멀거나 변하지 않는 엔티티도 언젠가는 차례가 오므로 굶지 않는다. 상태가 바뀐 엔티티는 우선도가 더 빨리 쌓인다.
 *
 *          엔티티 상태의 크기는 `NetMMOMessage::kMaxStateBytes` 까지 허용한다. 서버와 클라이언트가 같은 상한을 쓰며, 이를 넘는 `setEntity` 는 받지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Replication/NetInterestGrid.h"
#include "Core/Network/Replication/NetParallel.h"
#include "Core/Network/Replication/NetPrioritizer.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/WorldCellGrid.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class INetPeers;
    class WorldStreamingSubsystem;

    /** @brief 메시지 종류입니다. 메시지의 첫 바이트에 들어갑니다. */
    struct NetMMOMessage
    {
        static constexpr uint8 kEnter  = NetKitMessageRange::kMMO + 0;
        static constexpr uint8 kLeave  = NetKitMessageRange::kMMO + 1;
        static constexpr uint8 kUpdate = NetKitMessageRange::kMMO + 2; ///< 서버 → 클라이언트(비신뢰, 전달 통지). 통지 값은 서버 틱이다
        static_assert( NetMessageRange::isInRange( kUpdate, NetKitMessageRange::kMMO ), "message kinds must stay inside the kit's range" );

        static constexpr int32  kMaxStateBytes       = 512; ///< 엔티티 상태 상한. 서버는 넘는 엔티티를 받지 않고, 클라이언트는 넘는 길이를 손상된 메시지로 본다
        static constexpr uint32 kMaxUnconfirmedTicks = 64;  ///< 이만큼 지나도 판정이 없으면 잃은 것으로 본다. 서버 판정 테이블의 크기이기도 하다
    };
} // namespace sw

namespace sw
{
    /** @brief 엔티티 하나입니다. */
    struct MMOEntity
    {
        vector<uint8> _listState{};
        float3        _position{};
        uint32        _entityID{ 0 };
        uint32        _typeID{ 0 };
        float32       _importance{ 1.0f }; ///< 보스와 다른 플레이어는 크게 잡는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class IInterestPolicy
     * @brief 관찰자가 어떤 엔티티를 얼마나 원하는지 정하는 정책입니다. 게임마다 교체합니다.
     * @details 예를 들어 파티원은 항상 보이게 하고, 길드전 지역은 반경을 넓힐 수 있다.
     * @warning 복제기에 `setTaskManager` 를 주면 이 함수들이 **여러 스레드에서 동시에** 불린다. 상태를 바꾸지 말고 읽기만 해야 한다.
     */
    class SW_KIT_API IInterestPolicy
    {
    public:
        IInterestPolicy()          = default;
        virtual ~IInterestPolicy() = default;

        IInterestPolicy( const IInterestPolicy& )            = default;
        IInterestPolicy& operator=( const IInterestPolicy& ) = default;

        /**
         * @brief 반경 밖에서도 항상 보이는 엔티티가 있을 수 있으면 true 를 반환합니다.
         * @details 기본값은 false 이다. false 이면 새로 보임 단계가 엔티티 전체를 훑지 않으므로 관찰자 수 × 엔티티 수만큼의 비용을 아낀다.
         *          `isAlwaysRelevant` 를 바꾼 정책은 true 를 반환해야 한다.
         */
        virtual bool hasAlwaysRelevant() const { return false; }
        /** @brief 반경과 상관없이 항상 보여야 하면 true 를 반환합니다. 파티원이나 추적 중인 퀘스트 대상이 해당합니다. `hasAlwaysRelevant` 가 true 일 때만 호출합니다. */
        virtual bool isAlwaysRelevant( int32 connectionID, const MMOEntity& entity ) const
        {
            (void)connectionID;
            (void)entity;
            return false;
        }
        /** @brief 한 틱에 쌓을 우선도를 반환합니다. 기본값은 중요도 / (1 + 거리 / 10) 입니다. */
        virtual float32 computePriority( int32 connectionID, const MMOEntity& entity, float32 distance ) const
        {
            (void)connectionID;
            return entity._importance / ( 1.0f + distance * 0.1f );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 복제 설정입니다. 범위는 칸의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `MMOReplicator::initialize` 가 오류로 거절합니다(`ConfigRangeUtil`).
     * @details 나가는 반경 >= 들어오는 반경은 칸 사이의 관계라 `initialize` 가 따로 본다. 예전에는 작은 나가는 반경을 조용히 들어오는 반경으로 올렸다(폴백 금지 원칙).
     *          예산의 `Max` 는 `NetConnection::kMaxSingleMessageSize` 상수 이름이라 연결의 한 메시지 상한과 어긋나지 않는다.
     */
    REFLECT()
    struct SW_KIT_API MMOReplicatorConfig
    {
        REFLECT_BODY();
        PROPERTY( Min = 0 )
        float32 _enterRadius{ 60.0f }; ///< 이 안에 들어오면 보이기 시작한다
        PROPERTY( Min = 0 )
        float32 _leaveRadius{ 70.0f }; ///< 이 밖으로 나가야 보이지 않게 된다. `_enterRadius` 이상이어야 한다(히스테리시스)
        PROPERTY( Min = 0 )
        float32 _changedBoost{ 4.0f }; ///< 상태가 바뀐 엔티티의 우선도 배율
        PROPERTY( Min = 1, Max = NetConnection::kMaxSingleMessageSize )
        int32 _updateBudgetBytes{ 600 }; ///< 관찰자 한 명당 틱마다 보내는 갱신 메시지 바이트(1 ~ `NetConnection::kMaxSingleMessageSize`). 종류 바이트와 틱을 포함한다. 연결 상한이 더 작으면 그 값을 쓴다
        PROPERTY( Min = 1 )
        int32 _maxEnterPerTick{ 32 }; ///< 한 틱에 새로 보이게 하는 엔티티 수의 상한. 텔레포트 직후에 몰리지 않게 하며, 신뢰 메시지 하나에 묶고 64 KB를 넘는 나머지는 다음 틱에 보낸다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 서버 쪽 복제기입니다. 관찰자는 연결과 그 연결이 조종하는 엔티티이며, 그 엔티티의 위치가 관심의 중심입니다.
     * @details 받는 메시지는 없다. 갱신(`kUpdate`)의 전달 판정을 라우터에서 받는다(`getNotifiedKindMask` · `onDeliveryNotice`).
     *          라우터에 등록하면 연결이 닫힐 때 그 관찰자를 스스로 지운다(`onConnectionClosed`).
     */
    class SW_KIT_API MMOReplicator : public INetMessageHandler
    {
    public:
        MMOReplicator();

        uint8  getMessageRangeBase() const override { return NetKitMessageRange::kMMO; }
        uint16 getMessageKindMask() const override { return 0; }
        uint16 getNotifiedKindMask() const override { return static_cast<uint16>( 1u << ( NetMMOMessage::kUpdate - NetKitMessageRange::kMMO ) ); }
        /** @brief 받는 종류가 없으므로 라우터는 부르지 않는다. 직접 전달하면 NotMine 이다. */
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 갱신 하나(값 = 그 틱)의 판정을 그 관찰자의 판정 테이블에 적습니다. 다음 `update` 가 전송 중인 갱신을 판정할 때 읽는다. */
        void onDeliveryNotice( const NetDeliveryNotice& notice ) override;
        void onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override;

        /**
         * @brief 호스트, 설정, 월드 격자, 정책을 받아 시작합니다.
         * @details 설정이 범위 밖이거나, 나가는 반경이 들어오는 반경보다 작거나, 격자가 만들어지지 않았거나(`WorldCellGrid::tryCreate` 를 지나지 않은 기본값),
         *          나가는 반경이 격자의 로딩 반경보다 크면 오류를 남기고 false 를 반환한다. 거절된 복제기는 호스트가 없는 복제기와 같다. `update` 가 아무것도 보내지 않는다.
         *          스트리밍하는 씬은 `WorldStreamingSubsystem::getGrid()` 를, 스트리밍하지 않는 게임은 자기 데이터의 `WorldCellGridDef` 로 만든 격자를 넘긴다.
         */
        [[nodiscard]] bool initialize( INetPeers* pHost, const MMOReplicatorConfig& settings, const WorldCellGrid& cellGrid, const IInterestPolicy* pPolicy = nullptr );
        /** @brief 엔티티를 추가하거나 갱신합니다. 상태가 `NetMMOMessage::kMaxStateBytes` 를 넘으면 받지 않고 그 엔티티는 지난 값을 유지합니다. 처음 한 번만 경고하고 그 뒤로는 횟수만 셉니다. */
        void setEntity( const MMOEntity& entity );
        void removeEntity( uint32 entityID );
        void setObserver( int32 connectionID, uint32 entityID );
        /** @brief 관찰자를 지웁니다. 연결이 닫히면 라우터를 통해 자동으로 지워지므로, 연결은 두고 관심만 끊을 때 직접 호출합니다. */
        void removeObserver( int32 connectionID );
        /**
         * @brief 관찰자마다 새로 보임, 보이지 않게 됨, 갱신을 보냅니다.
         * @details `setTaskManager` 를 지정했으면 관찰자를 작업 스레드에 나눠 계산한다. 관찰자마다 독립적이므로 결과는 한 스레드로 돌릴 때와 같다.
         *          실행하는 동안 엔티티와 관찰자를 바꾸면 안 된다. 같은 스레드에서 차례로 호출하면 이 조건이 지켜진다.
         */
        void update( float32 deltaTime );
        /** @brief 관심 영역 계산을 나눠 처리할 작업 스레드 풀을 지정합니다. 게임은 `&engine::getSubsystem<TaskManager>()` 를 넘깁니다. nullptr 이면 호출한 스레드에서 처리합니다. */
        void setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold = NetParallelFor::kDefaultSerialThreshold );

        /**
         * @brief 관찰자마다 조종하는 엔티티의 위치를 채웁니다(활성이고 엔티티가 있는 관찰자만, 연결 번호 순).
         * @param outListPosition 비우고 채웁니다.
         */
        void collectObserverPositions( vector<float3>& outListPosition ) const;
        /**
         * @brief 서버의 월드 스트리밍 원점을 관찰자 위치로 정합니다(`WorldStreamingSubsystem::setExternalSources`). 서버 게임 코드가 틱마다 `update` 와 함께 부릅니다.
         * @details 관찰자가 없으면 빈 목록을 넣는다 — 아무 셀도 원하지 않는다(카메라 원점으로 돌아가지 않는다).
         */
        void feedStreamingSources( WorldStreamingSubsystem& streaming );

        /** @brief 그 관찰자에게 지금 보이는 엔티티 수를 반환합니다. */
        int32  getVisibleCount( int32 connectionID ) const;
        uint64 getSentUpdateCount() const { return _sentUpdateCount; }
        /** @brief 상한을 넘어서 받지 않은 `setEntity` 호출 수를 반환합니다. */
        uint64 getOversizedEntityCount() const { return _oversizedEntityCount; }

    private:
        struct VisibleEntry
        {
            vector<uint8> _listSentState{};     ///< 클라이언트가 가진 것으로 확인된 상태. 새로 보임과 확인된 갱신이 해당한다
            vector<uint8> _listInFlightState{}; ///< 보냈지만 아직 확인되지 않은 갱신의 상태
            uint32        _inFlightTick{ 0 };
            uint8         _bInFlight{ SW_FALSE };
        };

        struct Observer
        {
            unordered_map<uint32, VisibleEntry> _mapVisible{};
            NetPrioritizer                      _prioritizer{};   ///< 보이는 엔티티마다 쌓인 우선도
            TickRingBuffer<uint8>               _updateOutcome{}; ///< 판정이 난 갱신 틱. 값이 `SW_TRUE` 면 전달, `SW_FALSE` 면 잃음이다. 항목이 없으면 아직 판정이 없다
            uint32                              _entityID{ 0 };
            uint8                               _bActive{ SW_FALSE };
        };

        /** @brief 작업 스레드 하나가 관찰자를 계산하는 데 쓰는 스크래치 버퍼입니다. 틱마다 다시 사용합니다. */
        struct ObserverScratch
        {
            vector<uint32>                     _listLeave{};
            vector<SlotHandle>                 _listNear{};  ///< 들어오는 반경 안의 엔티티와 항상 보이는 엔티티. 격자 키이며 엔티티 id 를 담은 SlotHandle 이다
            vector<std::pair<float32, uint32>> _listRank{};  ///< (거리, id)
            vector<uint32>                     _listOrder{}; ///< 쌓인 우선도 순서
            vector<uint32>                     _listSent{};
            vector<uint32>                     _listEnter{}; ///< 이번 틱의 새로 보임 메시지에 실은 엔티티. 전송이 받아들여져야 보이는 목록에 넣는다
            NetMessageWriter                   _messageWriter{};
            uint64                             _sentUpdateCount{ 0 }; ///< 이번 update 에서 이 스크래치를 쓴 스레드가 보낸 갱신 수. update 가 끝나면 합산한다
        };

        void updateObserverRange( uint32 start, uint32 end );
        void updateObserver( int32 connectionID, Observer& observer, float32 deltaTime, ObserverScratch& scratch );
        /** @brief 보이지 않게 된 엔티티를 조각나지 않는 크기(`NetConnection::kMaxSingleMessageSize`)로 쪼개 보내고, 보낸 것만 보이는 목록에서 뺍니다. 못 보낸 엔티티가 시작하는 `_listLeave` 의 인덱스를 반환합니다. */
        size_t sendLeaves( int32 connectionID, Observer& observer, ObserverScratch& scratch );
        /**
         * @brief 들어오는 반경 안(과 항상 보이는) 엔티티 중 아직 안 보이는 것을 가까운 것부터 신뢰 메시지 하나로 묶어 보냅니다.
         * @details 틱마다 `_maxEnterPerTick` 과 신뢰 메시지 크기가 상한이다. 전송이 받아들여진 것만 보이는 목록에 넣는다.
         */
        void sendEnters( int32 connectionID, Observer& observer, const float3& center, ObserverScratch& scratch );
        /** @brief 보이는 엔티티의 우선도를 쌓고 틱 예산 안에서 큰 것부터 비신뢰 갱신으로 보냅니다. `_listLeave` 의 @p pendingLeaveIndex 뒤(못 보낸 사라짐)는 건너뜁니다. */
        void sendUpdates( int32 connectionID, Observer& observer, const float3& center, size_t pendingLeaveIndex, float32 deltaTime, ObserverScratch& scratch );
        /** @brief 판정을 기다리는 갱신을 판정 테이블로 정리합니다. 전달됐으면 확인된 상태로 옮기고, 잃었으면 우선도를 되돌립니다(`NetPrioritizer::resolveSend`). */
        void             resolveInFlightUpdates( Observer& observer ) const;
        const MMOEntity& getEntity( uint32 entityID ) const;

        unordered_map<uint32, MMOEntity>    _mapEntity;
        vector<Observer>                    _listObserver;
        NetInterestGrid                     _interest;          ///< 엔티티 위치(XZ 평면의 점)와 두 반경. 칸은 `initialize` 가 받은 월드 격자의 셀 크기, 키는 `NetInterestGrid::makeKey`
        vector<float3>                      _listScratchSource; ///< `feedStreamingSources` 가 틱마다 다시 쓴다
        MMOReplicatorConfig                 _settings;
        IInterestPolicy                     _defaultPolicy;
        INetPeers*                          _pHost;
        const IInterestPolicy*              _pPolicy;
        uint64                              _sentUpdateCount;
        uint64                              _oversizedEntityCount;
        NetParallelFor                      _parallel;
        NetParallelScratch<ObserverScratch> _observerScratch;       ///< 스레드마다 하나씩 둔다
        float32                             _tickDeltaTime;         ///< 이번 update 의 시간. 나눠 실행하는 본문이 읽는다
        int32                               _tickUpdateBudgetBytes; ///< 이번 update 의 갱신 예산. 설정값과 연결 상한 중 작은 값이며 나눠 실행하는 본문이 읽는다
        uint32                              _tick;                  ///< `update` 마다 하나씩 올린다. 새로 보임과 갱신 메시지에 실어서 받는 쪽이 오래된 메시지를 버리게 한다
        Observer*                           _pRangeObserver;        ///< 나눠 실행하는 본문이 쓰는 `_listObserver.data()`. 워커는 컨테이너를 직접 만지지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트에서 일어난 일을 알리는 이벤트입니다. */
    struct MMOClientEvent
    {
        enum class Kind : uint8
        {
            Entered = 0,
            Left,
            Updated
        };
        uint32 _entityID{ 0 };
        Kind   _kind{ Kind::Entered };
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트 쪽에서 보이는 엔티티의 마지막 상태입니다. */
    class SW_KIT_API MMOClientView : public INetMessageHandler
    {
    public:
        uint8  getMessageRangeBase() const override { return NetKitMessageRange::kMMO; }
        uint16 getMessageKindMask() const override
        {
            return static_cast<uint16>( ( 1u << ( NetMMOMessage::kEnter - NetKitMessageRange::kMMO ) ) | ( 1u << ( NetMMOMessage::kLeave - NetKitMessageRange::kMMO ) ) |
                                        ( 1u << ( NetMMOMessage::kUpdate - NetKitMessageRange::kMMO ) ) );
        }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 서버에 (다시) 연결되면 호출합니다. 보이던 엔티티를 비우며, 새 연결은 새로 보임부터 다시 받습니다. */
        void onConnectionOpened( int32 connectionID ) override;
        void drainEvents( vector<MMOClientEvent>& outListEvent );

        const MMOEntity* findEntity( uint32 entityID ) const;
        int32            getEntityCount() const { return static_cast<int32>( _mapEntity.size() ); }
        /** @brief 순서가 뒤바뀌어 마지막으로 적용한 틱보다 오래돼서 버린 갱신 수입니다. */
        uint64 getStaleUpdateCount() const { return _staleUpdateCount; }

    private:
        struct ClientEntity
        {
            MMOEntity _entity{};
            uint32    _tick{ 0 }; ///< 마지막으로 적용한 서버 틱
        };

        unordered_map<uint32, ClientEntity> _mapEntity{};
        EventBuffer<MMOClientEvent>         _eventBuffer{};
        uint64                              _staleUpdateCount{ 0 };
    };
} // namespace sw
