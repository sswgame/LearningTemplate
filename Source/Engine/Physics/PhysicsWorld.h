#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandleTable.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Physics/AABB.h"
#include "Engine/Physics/CollisionLayers.h"

namespace sw
{
    struct SweepHit;
    /** @brief PhysicsWorld 에 등록된 AABB 바디입니다. */
    struct PhysicsBody
    {
        AABB   _aabb{};
        AABB   _stepAabb{}; ///< 지난 `step` 때의 자리 — 연속 바디는 여기서 `_aabb` 까지 쓸린다. 더할 때 · 순간이동 때는 `_aabb` 와 같다
        uint64 _objectId{ 0 };
        uint8  _layer{ 0 };
        uint8  _bContinuous{ SW_FALSE }; ///< 연속 충돌(ContinuousCollision) 바디면 SW_TRUE — `step` 이 지난 자리에서 지금 자리까지 쓸어 그 사이에 닿은 것도 겹침으로 낸다
        uint8  _bTrigger{ SW_FALSE };    ///< 트리거면 SW_TRUE — 겹침은 내지만 막지 않는다(유니티 `isTrigger` · 언리얼 Overlap 반응). 받는 쪽이 이벤트에서 본다
    };
} // namespace sw

namespace sw
{
    /** @brief 콜라이더가 바디에 맞추는 값 한 벌입니다 — 자리 · 레이어 · 판정 방식(`PhysicsWorld::addBody` · `updateBody`). */
    struct PhysicsBodyState
    {
        AABB  _aabb{};
        uint8 _layer{ 0 };
        uint8 _bContinuous{ SW_FALSE };
        uint8 _bTrigger{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 바디를 옮기는 방식입니다(언리얼 `ETeleportType` · 유니티 `Rigidbody.MovePosition` 대 `position` 대입).
     * @details 연속 바디만 다릅니다 — `Sweep` 은 지난 step 의 자리에서 새 자리까지 쓸려 그 사이에 닿은 것과 겹치고, `Teleport` 는 그 사이를 건너뛴다
     *          (새 자리가 다음 쓸림의 출발점이 된다). 리스폰 · 문 통과 · 풀에서 꺼낸 총알처럼 "그 길을 실제로 지나가지 않은" 이동은 Teleport 다.
     */
    enum class BodyMoveType : uint8
    {
        Sweep,
        Teleport
    };

    /**
     * @brief `step` 이 낸 겹침 시작 · 끝 하나입니다. 두 바디의 오브젝트 id 와 각 바디가 트리거인지입니다(순서는 정해져 있지 않다).
     * @details 목록은 `_time` 순서입니다 — 빠른 연속 바디가 한 step 에 둘을 지나가면 **먼저 닿은 쪽**이 먼저 옵니다(총알이 뒤의 적부터 맞지 않는다).
     *          한 오브젝트에 콜라이더가 여럿이면 바디 쌍마다 하나씩 옵니다 — 트리거 여부로 어느 콜라이더였는지 가립니다.
     */
    struct PhysicsOverlapEvent
    {
        uint64  _objectA{ 0 };
        uint64  _objectB{ 0 };
        float32 _time{ 1.0f };          ///< 이번 step 안에서 닿은 때(0..1). 쓸려서 닿은 시작만 1 보다 작다 — 끝 · 제자리 겹침은 1
        uint8   _bBegin{ SW_FALSE };    ///< 시작이면 SW_TRUE, 끝이면 SW_FALSE
        uint8   _bTriggerA{ SW_FALSE }; ///< A 쪽 바디가 트리거인지
        uint8   _bTriggerB{ SW_FALSE }; ///< B 쪽 바디가 트리거인지
    };
} // namespace sw

namespace sw
{
    /**
     * @class PhysicsWorld
     * @brief 겹침 질의 · 레이어 필터 · 겹침 이벤트입니다. 강체가 없어 `step` 은 적분하지 않고 겹침만 다시 잽니다.
     */
    class SW_API PhysicsWorld
    {
    public:
        using BodyHandle = SlotHandle;

        /** @brief 기본 레이어 행렬로 빈 월드를 만듭니다. */
        PhysicsWorld() = default;

        /** @brief 막는 · 이산 AABB 바디를 등록합니다. 출발점(`_stepAabb`)은 지금 자리입니다. */
        BodyHandle addBody( const AABB& aabb, uint8 layer, uint64 objectId = 0 );
        /** @brief 판정 방식(연속 · 트리거)까지 정한 바디를 등록합니다. 출발점(`_stepAabb`)은 지금 자리입니다. */
        BodyHandle addBody( const PhysicsBodyState& state, uint64 objectId );
        /** @brief 바디를 제거합니다. */
        void removeBody( BodyHandle handle );
        /** @brief 바디 AABB 를 갱신합니다. 연속 바디는 다음 `step` 에서 지난 step 의 자리부터 여기까지 쓸립니다(`BodyMoveType::Sweep`). */
        void setAabb( BodyHandle handle, const AABB& aabb );
        /**
         * @brief 바디의 상자 · 레이어 · 판정 방식을 한 번에 맞춥니다. 콜라이더가 `step` 직전에 부릅니다(`BoxCollider2DComponent::syncPhysicsBody`).
         * @details 레이어 · 판정 방식도 매번 맞춥니다 — 더할 때 한 번만 적으면 시작한 뒤 바꾼 콜라이더 종류(`setColliderType`)가 겹침에 반영되지 않습니다.
         * @param moveType `Teleport` 면 새 자리가 다음 쓸림의 출발점이 됩니다 — 지난 자리에서 여기까지의 길을 쓸지 않습니다.
         */
        void updateBody( BodyHandle handle, const PhysicsBodyState& state, BodyMoveType moveType = BodyMoveType::Sweep );
        /** @brief 핸들이 유효하면 out 에 복사하고 true 를 반환합니다. */
        [[nodiscard]] bool tryGetBody( BodyHandle handle, PhysicsBody& out ) const;
        /**
         * @brief 바디 쌍의 겹침을 다시 재고, 지난 step 과 달라진 쌍을 시작 · 끝 이벤트로 냅니다(`getOverlapEvents`).
         * @details 유니티 `OnTriggerEnter2D/Exit2D` · 언리얼 `BeginOverlap/EndOverlap` 의 자리입니다. 계속 겹친 쌍은 다시 내지 않고, 바디가 사라진
         *          쌍은 끝납니다(언리얼은 컴포넌트를 내릴 때 EndOverlap 을 낸다). 강체가 없으므로 적분하지 않습니다 — @p deltaTime 은 그때를 위한
         *          자리입니다. 매니저가 틱 · 트랜스폼 적용 뒤에 게임 스레드에서 부릅니다(`SceneOverlapWorld2D::step`).
         *
         *          **연속 바디(`_bContinuous`)는 지난 step 의 자리에서 지금 자리까지 쓸립니다**(`ContinuousCollision::sweepAabb`, 유니티 `CollisionDetectionMode2D.Continuous`).
         *          한 프레임에 얇은 바디를 통째로 건너뛴 총알도 그 바디와 겹친 것으로 칩니다 — 이번 step 에 시작하고, 다음 step 에(이미 지나갔으면)
         *          끝납니다. 출발점에서 이미 겹쳐 있던 것(닿은 때 0)은 쓸림으로 더하지 않습니다 — 그 겹침은 지난 step 이 쟀고, 지금도 겹치면 제자리
         *          겹침이 이어 갑니다.
         *
         *          **상대도 움직였으면 상대 운동으로 잽니다**(Box2D 총알 TOI · 유니티 Continuous Dynamic): 두 바디 모두 지난 step 의 자리에서 출발해
         *          연속 바디의 이동에서 상대의 이동을 뺀 만큼 쓸립니다. 프레임 사이에 총알 길을 가로질러 건너편으로 간 상대도 맞습니다(상대를
         *          이번 step 의 자리에 세워 두고 재면 놓친다).
         */
        void step( float32 deltaTime );
        /** @brief 마지막 `step` 이 낸 겹침 이벤트입니다. 다음 `step` 까지 그대로입니다. */
        const vector<PhysicsOverlapEvent>& getOverlapEvents() const { return _listOverlapEvent; }

        /** @brief 두 바디가 레이어와 AABB 모두에서 겹치면 true 입니다. */
        bool overlaps( BodyHandle a, BodyHandle b ) const;
        /** @brief box 와 겹치는 바디 핸들을 out 에 넣습니다. */
        void queryAabb( const AABB& box, uint8 layer, vector<BodyHandle>& outListHandle ) const;
        /** @brief movingBox 가 displacement 만큼 움직일 때 layer 의 대상들과 연속 충돌(ContinuousCollision)을 검사합니다. */
        bool sweepTest( const AABB& movingBox, const float3& displacement, uint8 layer, SweepHit& outHit ) const;

        /** @brief 레이어 필터를 반환합니다. */
        CollisionLayers& layers() { return _layers; }
        /** @brief 레이어 필터를 반환합니다. */
        const CollisionLayers& layers() const { return _layers; }

        /**
         * @brief 셀 표에 들어 있는 셀 수입니다(진단용).
         * @details 큰 바디가 그리드를 부풀리지 않는지 재는 데 씁니다. 질의 결과로는 보이지 않는
         *          비용이라 숫자로 봐야 합니다.
         */
        size_t getGridCellCount() const;

    private:
        struct CellCoord
        {
            int32 _x{ 0 };
            int32 _y{ 0 };
            int32 _z{ 0 };

            bool operator==( const CellCoord& other ) const noexcept
            {
                return _x == other._x && _y == other._y && _z == other._z;
            }
        };

        struct CellCoordHash
        {
            size_t operator()( const CellCoord& coord ) const noexcept
            {
                size_t hash = std::hash<int32>{}( coord._x );
                hash ^= std::hash<int32>{}( coord._y ) + 0x9e3779b9 + ( hash << 6 ) + ( hash >> 2 );
                hash ^= std::hash<int32>{}( coord._z ) + 0x9e3779b9 + ( hash << 6 ) + ( hash >> 2 );
                return hash;
            }
        };

        static constexpr float32 kCellSize = 64.0f;

        /**
         * @brief 이 셀 수를 넘으면 그리드를 훑지 않고 모든 바디를 돕니다.
         * @details 넓은 질의는 셀을 다 방문하는 값이 바디를 모두 보는 값보다 비싸집니다. 질의 두 곳이 이 상수를 같이 씁니다 —
         *          리터럴로 따로 적으면 한쪽만 바뀌어 질의 종류에 따라 다른 문턱이 됩니다.
         */
        static constexpr int64 kMaxQueryCellCount = 1024;

        /**
         * @brief 바디 하나가 이 셀 수를 넘게 덮으면 그리드에 넣지 않고 **언제나 후보**로 둡니다.
         * @details 삽입 쪽에도 상한이 필요합니다. 큰 지형 · 바닥 콜라이더 하나가 자기 AABB 가 덮는 모든 셀에 핸들을 적으면,
         *          20,000 유닛짜리 바닥이면 셀 표에 **한 바디 때문에 십만 개 가까운 항목**이 생깁니다(64 유닛 셀 기준). `setAabb` 로
         *          움직이기라도 하면 그만큼을 매번 지웠다 다시 적습니다.
         *
         *          넘치는 바디는 그리드에 흩뿌리는 대신 목록 하나에 모아 두고, 그리드로 가는 질의가
         *          그 목록을 **항상 함께** 봅니다. 그런 바디는 수가 적고 어차피 거의 모든 질의에
         *          걸리므로, 셀에 흩어 두는 것이 이득이 되지 않습니다.
         * @note `kMaxQueryCellCount` 와 값이 같지만 **다른 질문**입니다(질의 범위가 넓은가 / 바디가 큰가).
         *       한쪽 사정으로 값을 바꿀 수 있어야 하므로 별칭을 두지 않고 따로 적습니다.
         */
        static constexpr int64 kMaxBodyCellCount = 1024;

        /**
         * @struct CellRange
         * @brief AABB 하나가 덮는 그리드 셀 범위입니다. **삽입 · 제거 · 질의가 같은 집합을 보게 하는 자리**입니다.
         *
         * @details 이 계산("이 AABB 는 어느 셀들인가")을 삽입 · 제거 · `setAabb` 의 옛/새 비교 둘 · `queryAabb` · `sweepTest` 가
         *          함께 씁니다.
         *
         *          어긋났을 때의 증상은 방향마다 다르고, **둘 다 그 자리에서 터지지 않습니다.**
         *          - **삽입이 덜 훑으면 충돌을 놓칩니다.** 바디가 실제로 겹치는 셀에 등록되지 않으므로
         *            그 셀을 보는 질의가 바디를 **찾지 못합니다**. 틀린 답이 조용히 나옵니다.
         *          - **제거가 덜 훑으면 그리드가 자랍니다.** 질의는 후보를 실제 AABB 로 다시 걸러내므로
         *            틀린 답이 되지는 않지만, 옮겨 다닌 바디가 지나온 셀마다 죽은 핸들을 남겨
         *            **셀 표가 끝없이 커지고** 후보 목록이 길어집니다.
         *
         *          `setAabb` 의 "셀이 그대로면 그리드를 건드리지 않는다" 지름길도 같은 계산에 기댑니다.
         *          이 비교가 삽입과 어긋나면 **새 셀에 등록되지 않은 채** 넘어가서 첫 번째 증상이 됩니다.
         */
        struct CellRange
        {
            int32 _minX{ 0 };
            int32 _minY{ 0 };
            int32 _minZ{ 0 };
            int32 _maxX{ -1 }; /**< 기본값은 비어 있는 범위입니다(max < min). */
            int32 _maxY{ -1 };
            int32 _maxZ{ -1 };

            /** @brief AABB 가 덮는 셀 범위입니다. 뒤집힌 AABB 도 정규화해서 받습니다. */
            static CellRange fromAabb( const AABB& aabb, float32 cellSize );

            /** @brief 두 범위가 같은 셀 집합인지 여부입니다. */
            bool operator==( const CellRange& other ) const noexcept
            {
                return _minX == other._minX && _maxX == other._maxX &&
                       _minY == other._minY && _maxY == other._maxY &&
                       _minZ == other._minZ && _maxZ == other._maxZ;
            }

            /** @brief 이 범위가 덮는 셀 수입니다. 비어 있으면 0 입니다. */
            int64 getCellCount() const noexcept;

            /**
             * @brief 범위의 셀마다 콜백을 부릅니다. 비어 있으면 한 번도 부르지 않습니다.
             * @details 순회 변수는 int64 다. 셀 번호는 int32 끝(`MaxInt32`)까지 접히는데(+inf · 아주 먼 좌표), int32 로 돌면 `++` 가 넘쳐
             *          `<= _maxX` 가 영원히 참이었다 — 게임 스레드가 락을 쥔 채 멈추고 셀 표가 끝없이 자랐다.
             */
            template <typename Func>
            void forEachCell( Func&& func ) const
            {
                for ( int64 gridZ = _minZ; gridZ <= _maxZ; ++gridZ )
                {
                    for ( int64 gridY = _minY; gridY <= _maxY; ++gridY )
                    {
                        for ( int64 gridX = _minX; gridX <= _maxX; ++gridX )
                        {
                            func( CellCoord{ static_cast<int32>( gridX ), static_cast<int32>( gridY ), static_cast<int32>( gridZ ) } );
                        }
                    }
                }
            }
        };

    private:
        void insertBodyToGrid( BodyHandle handle, const AABB& aabb );
        void removeBodyFromGrid( BodyHandle handle, const AABB& aabb );

        /** @brief 이 AABB 가 그리드에 흩뿌리기에 너무 큰지 반환합니다. AABB 만으로 정해집니다. */
        static bool isOversizedForGrid( const AABB& aabb );

        /** @brief 그리드를 훑기보다 모든 바디를 도는 편이 나은지 반환합니다. 범위가 비었거나 너무 넓으면 true 입니다. */
        bool shouldScanAllBodies( const CellRange& range ) const;
        /** @brief 범위가 덮는 셀들의 바디 핸들을 **중복 없이** 모읍니다. */
        void gatherCandidateHandles( const CellRange& range, vector<BodyHandle>& outListHandle ) const;
        /** @brief `step` 의 후보 — 범위가 비었거나 너무 넓으면 모든 바디, 아니면 그리드에서 모읍니다. `_mutex` 를 잡은 채로 부릅니다. */
        void gatherStepCandidates( const CellRange& range, vector<BodyHandle>& outListHandle ) const;
        /** @brief 바디 하나의 상자를 바꾸고 그리드 셀을 다시 맞춥니다. `_mutex` 를 잡은 채로 부릅니다(`setAabb` · `updateBody`). */
        void setAabbLocked( BodyHandle handle, PhysicsBody& body, const AABB& aabb );
        /**
         * @brief 연속 바디 @p body 가 지난 step 의 자리에서 지금 자리까지 쓸리며 처음 닿은 바디를 쌍으로 더합니다. `step` 이 `_mutex` 를 잡은 채로 부릅니다.
         * @details 상대 운동으로 잽니다 — 두 바디의 출발점(`_stepAabb`)에서 이 바디의 이동에서 상대의 이동을 뺀 만큼. 후보는 그리드에 **지금 자리**로
         *          들어 있으므로, 쓸린 범위를 이번 step 에 가장 많이 움직인 바디의 이동만큼 넓혀 모읍니다(그 안에 없으면 그 사이 어느 때에도 닿을 수 없다).
         *          셀 하나보다 멀리 간 바디(@p listFarMover — 에디터 드래그 · 아주 빠른 바디)는 범위를 넓히지 않고 따로 하나씩 잽니다. 넓히면 그 한 바디
         *          때문에 모든 연속 바디가 월드 전체를 훑습니다.
         * @param maxNearDisplacement 이번 step 에 셀 하나 이내로 움직인 바디들의 축별 가장 큰 거리(절댓값)
         * @param listFarMover 이번 step 에 어느 축으로든 셀 하나보다 멀리 간 바디들
         * @param inoutListCandidate 후보를 모을 자리(할당 재사용 — 들어 있던 것은 버린다)
         */
        void addSweptPairs( BodyHandle handle, const PhysicsBody& body, const float3& maxNearDisplacement, const vector<BodyHandle>& listFarMover,
                            vector<BodyHandle>& inoutListCandidate );
        /** @brief 연속 바디 @p body(이번 step 이동 @p displacement)가 @p candidate 와 쓸리며 처음 닿았으면 쌍으로 더합니다. `addSweptPairs` 의 후보 하나입니다. */
        void addSweptPairIfTouched( BodyHandle handle, const PhysicsBody& body, const float3& displacement, BodyHandle candidate );
        /** @brief 바디가 이번 step 에 어느 축으로든 셀 하나보다 멀리 갔으면 true 입니다(`addSweptPairs` 가 범위를 넓히지 않고 따로 재는 바디). */
        static bool isFarMover( const PhysicsBody& body );

    private:
        mutable std::shared_mutex                                   _mutex;
        SlotHandleTable<PhysicsBody>                                _bodies;
        CollisionLayers                                             _layers;
        unordered_map<CellCoord, vector<BodyHandle>, CellCoordHash> _mapGrid;
        /** @brief 그리드에 넣기에는 너무 큰 바디들입니다. 그리드로 가는 질의가 **항상 함께** 봅니다. */
        vector<BodyHandle> _listOversizedBody;

        /**
         * @brief 겹친 쌍 하나 — 두 핸들(작은 쪽이 먼저)과 그 오브젝트 id · 트리거 여부입니다. 바디가 사라진 뒤에도 끝 이벤트를 낼 수 있게 함께 든다.
         * @details `_time` 은 이번 step 안에서 닿은 때입니다(제자리 겹침은 1). 같은 쌍이 제자리 겹침과 쓸림(또는 연속 바디 둘의 양쪽 쓸림)으로
         *          두 번 들 수 있어, 줄 세운 뒤 가장 이른 것 하나만 남깁니다(`isEarlierInOrder`).
         */
        struct OverlapPair
        {
            BodyHandle _first{};
            BodyHandle _second{};
            uint64     _firstObjectId{ 0 };
            uint64     _secondObjectId{ 0 };
            float32    _time{ 1.0f };
            uint8      _bFirstTrigger{ SW_FALSE };
            uint8      _bSecondTrigger{ SW_FALSE };

            /** @brief @p a · @p b 를 핸들 순서(작은 쪽이 먼저)로 놓은 쌍을 만듭니다. */
            static OverlapPair makeOrdered( BodyHandle a, const PhysicsBody& bodyA, BodyHandle b, const PhysicsBody& bodyB, float32 time ) noexcept
            {
                if ( b < a )
                    return OverlapPair{ b, a, bodyB._objectId, bodyA._objectId, time, bodyB._bTrigger, bodyA._bTrigger };
                return OverlapPair{ a, b, bodyA._objectId, bodyB._objectId, time, bodyA._bTrigger, bodyB._bTrigger };
            }

            bool operator<( const OverlapPair& other ) const noexcept
            {
                return ( _first != other._first ) ? ( _first < other._first ) : ( _second < other._second );
            }
            /** @brief 쌍 순서가 같으면 먼저 닿은 쪽이 앞입니다 — 줄 세운 뒤 같은 쌍의 첫 항목이 가장 이른 닿은 때입니다. */
            static bool isEarlierInOrder( const OverlapPair& lhs, const OverlapPair& rhs ) noexcept
            {
                if ( lhs.isSamePair( rhs ) )
                    return lhs._time < rhs._time;
                return lhs < rhs;
            }
            bool isSamePair( const OverlapPair& other ) const noexcept { return _first == other._first && _second == other._second; }
        };
        vector<OverlapPair>         _listOverlapPair;  ///< 지난 step 의 겹친 쌍(정렬)
        vector<OverlapPair>         _listScratchPair;  ///< 이번 step 의 겹친 쌍을 모으는 자리(할당 재사용)
        vector<PhysicsOverlapEvent> _listOverlapEvent; ///< 지난 step 이 낸 이벤트
    };
} // namespace sw
