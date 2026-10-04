/**
 * @file MmoReplicator.h
 * @brief MMO 복제 — 엔티티가 수천이어도 관찰자(플레이어)마다 "근처만, 중요한 것 먼저, 정한 바이트 안에서" 보냅니다.
 * @details 1. 관심 영역 — 격자 버킷으로 근처를 찾고, 들어오는 반경보다 나가는 반경을 크게 해 경계에서 들락날락하지 않게 합니다(히스테리시스).
 *          2. 들어옴 · 나감은 신뢰 메시지(전체 상태), 갱신은 비신뢰 묶음입니다(잃으면 다음 갱신이 메운다).
 *          3. 우선도 누적 — 보이는 엔티티마다 매 틱 우선도(정책: 거리 · 중요도)를 쌓고, 예산 안에서 쌓인 것이 큰 순서로 보낸 뒤 0 으로 돌립니다.
 *             멀거나 변하지 않는 것도 언젠가는 차례가 옵니다(굶지 않는다). 상태가 바뀐 것은 더 빨리 쌓입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Network/NetMessage.h"
#include "Core/Network/NetParallel.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Network/NetKitMessageRange.h"

namespace sw
{
    class NetHost;

    /** @brief 메시지 종류(첫 바이트)입니다. */
    struct NetMmoMessage
    {
        static constexpr uint8 kEnter  = NetKitMessageRange::kMmo + 0;
        static constexpr uint8 kLeave  = NetKitMessageRange::kMmo + 1;
        static constexpr uint8 kUpdate = NetKitMessageRange::kMmo + 2;
        static_assert( NetMessageRange::isInRange( kUpdate, NetKitMessageRange::kMmo ), "message kinds must stay inside the kit's range" );
    };
} // namespace sw

namespace sw
{
    /** @brief 엔티티 하나입니다. */
    struct MmoEntity
    {
        vector<uint8> _listState{};
        float3        _position{};
        uint32        _entityId{ 0 };
        uint32        _typeId{ 0 };
        float32       _importance{ 1.0f }; ///< 보스 · 다른 플레이어는 크게
    };
} // namespace sw

namespace sw
{
    /**
     * @class InterestGrid
     * @brief XZ 평면의 격자 버킷입니다. 반경 질의는 걸친 칸만 봅니다.
     */
    class SW_GF_API InterestGrid
    {
    public:
        void initialize( float32 cellSize );
        void setPosition( uint32 entityId, const float3& position );
        void remove( uint32 entityId );
        void queryRadius( const float3& center, float32 radius, vector<uint32>& outListEntity ) const;
        bool findPosition( uint32 entityId, float3& outPosition ) const;

    private:
        static int64 makeCellKey( int32 x, int32 z ) { return ( static_cast<int64>( x ) << 32 ) ^ static_cast<int64>( static_cast<uint32>( z ) ); }
        int32        computeCellCoord( float32 value ) const;

        unordered_map<int64, vector<uint32>> _mapCell{};
        unordered_map<uint32, float3>        _mapPosition{};
        float32                              _cellSize{ 32.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IInterestPolicy
     * @brief 누가 무엇을 얼마나 원하는가 — 게임마다 바꿉니다(파티원은 늘 보이기 · 길드전 지역은 반경 넓히기).
     * @warning 복제기에 `setTaskManager` 를 주면 이 함수들이 **여러 스레드에서 동시에** 불린다 — 상태를 바꾸지 말고 읽기만.
     */
    class SW_GF_API IInterestPolicy
    {
    public:
        IInterestPolicy()          = default;
        virtual ~IInterestPolicy() = default;

        IInterestPolicy( const IInterestPolicy& )            = default;
        IInterestPolicy& operator=( const IInterestPolicy& ) = default;

        /**
         * @brief 반경 밖에서도 늘 보이는 엔티티가 있을 수 있는가입니다. 기본은 false — 그러면 들어옴 단계가 엔티티 전체를 훑지 않는다
         *        (관찰자 × 엔티티 비용). `isAlwaysRelevant` 를 바꾸는 정책은 true 를 돌려준다.
         */
        virtual bool hasAlwaysRelevant() const { return false; }
        /** @brief 반경과 상관없이 늘 보이는가입니다(파티원 · 추적 중인 퀘스트 대상). `hasAlwaysRelevant` 가 true 일 때만 묻는다. */
        virtual bool isAlwaysRelevant( int32 connectionId, const MmoEntity& entity ) const
        {
            (void)connectionId;
            (void)entity;
            return false;
        }
        /** @brief 한 틱에 쌓을 우선도입니다. 기본은 중요도 / (1 + 거리 / 10). */
        virtual float32 computePriority( int32 connectionId, const MmoEntity& entity, float32 distance ) const
        {
            (void)connectionId;
            return entity._importance / ( 1.0f + distance * 0.1f );
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 복제 설정입니다. */
    struct MmoReplicatorSettings
    {
        float32 _cellSize{ 32.0f };
        float32 _enterRadius{ 60.0f };
        float32 _leaveRadius{ 70.0f };
        float32 _changedBoost{ 4.0f };     ///< 상태가 바뀐 엔티티의 우선도 배율
        int32   _updateBudgetBytes{ 600 }; ///< 관찰자 · 틱마다 갱신 바이트
        int32   _maxEnterPerTick{ 32 };    ///< 한 틱에 새로 보이는 것 상한(텔레포트 직후 몰리지 않게)
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 쪽입니다. 관찰자 = 연결 + 그 연결이 조종하는 엔티티(그 자리가 관심의 중심). */
    class SW_GF_API MmoReplicator
    {
    public:
        MmoReplicator();

        void initialize( NetHost* pHost, const MmoReplicatorSettings& settings, const IInterestPolicy* pPolicy = nullptr );
        void setEntity( const MmoEntity& entity );
        void removeEntity( uint32 entityId );
        void setObserver( int32 connectionId, uint32 entityId );
        void removeObserver( int32 connectionId );
        /**
         * @brief 관찰자마다 들어옴 · 나감 · 갱신을 보냅니다. `setTaskManager` 를 줬으면 관찰자들을 작업 스레드에 나눠 계산한다(관찰자마다 독립 —
         *        결과는 한 스레드로 돌 때와 같다). 도는 동안 엔티티 · 관찰자를 바꾸지 않는다(같은 스레드에서 차례로 부르면 그렇다).
         */
        void update( float32 deltaTime );
        /** @brief 관심 영역 계산을 나눌 작업 스레드 풀입니다(게임은 `&engine::getTaskManager()`). nullptr 이면 지금 스레드가 돈다. */
        void setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold = NetParallelFor::kDefaultSerialThreshold );

        /** @brief 그 관찰자에게 지금 보이는 엔티티 수입니다. */
        int32               getVisibleCount( int32 connectionId ) const;
        uint64              getSentUpdateCount() const { return _sentUpdateCount; }
        const InterestGrid& getGrid() const { return _grid; }

    private:
        struct VisibleEntry
        {
            vector<uint8> _listSentState{};
            float32       _accumulated{ 0.0f };
        };

        struct Observer
        {
            unordered_map<uint32, VisibleEntry> _mapVisible{};
            uint32                              _entityId{ 0 };
            uint8                               _bActive{ SW_FALSE };
        };

        /** @brief 작업 스레드 하나가 관찰자를 계산하는 데 쓰는 자리입니다(틱마다 다시 쓴다). */
        struct ObserverScratch
        {
            vector<uint32>                     _listLeave{};
            vector<uint32>                     _listNear{};
            vector<std::pair<float32, uint32>> _listRank{}; ///< (거리 또는 우선도, id)
            vector<uint32>                     _listSent{};
            NetMessageWriter                   _messageWriter{};
            uint64                             _sentUpdateCount{ 0 }; ///< 이번 update 에서 이 자리가 보낸 갱신 — 끝나고 합친다
        };

        void             updateObserverRange( uint32 start, uint32 end );
        void             updateObserver( int32 connectionId, Observer& observer, float32 deltaTime, ObserverScratch& scratch );
        const MmoEntity& getEntity( uint32 entityId ) const;

        unordered_map<uint32, MmoEntity>    _mapEntity;
        vector<Observer>                    _listObserver;
        InterestGrid                        _grid;
        MmoReplicatorSettings               _settings;
        IInterestPolicy                     _defaultPolicy;
        NetHost*                            _pHost;
        const IInterestPolicy*              _pPolicy;
        uint64                              _sentUpdateCount;
        NetParallelFor                      _parallel;
        NetParallelScratch<ObserverScratch> _observerScratch; ///< 스레드마다 하나
        float32                             _tickDeltaTime;   ///< 이번 update 의 시간 — 나눈 본문이 읽는다
        Observer*                           _pRangeObserver;  ///< 나눈 본문이 쓰는 `_listObserver.data()` — 워커는 컨테이너를 만지지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트에서 생긴 일입니다. */
    struct MmoClientEvent
    {
        enum class Kind : uint8
        {
            Entered = 0,
            Left,
            Updated
        };
        uint32 _entityId{ 0 };
        Kind   _kind{ Kind::Entered };
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트 쪽 — 보이는 엔티티의 마지막 상태입니다. */
    class SW_GF_API MmoClientView : public INetMessageHandler
    {
    public:
        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kMmo; }
        bool  handleNetMessage( int32 connectionId, const uint8* pData, int32 size ) override;
        /** @brief 받은 메시지 하나 — 내 영역이 아니면 false(`NetMessageRouter` 를 쓰지 않는 게임의 손 배달). */
        bool handleMessage( const vector<uint8>& buffer ) { return handleNetMessage( -1, buffer.data(), static_cast<int32>( buffer.size() ) ); }
        void drainEvents( vector<MmoClientEvent>& outListEvent );

        const MmoEntity* findEntity( uint32 entityId ) const;
        int32            getEntityCount() const { return static_cast<int32>( _mapEntity.size() ); }

    private:
        unordered_map<uint32, MmoEntity> _mapEntity{};
        vector<MmoClientEvent>           _listEvent{};
    };
} // namespace sw
