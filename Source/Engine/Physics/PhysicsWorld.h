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
        uint64 _objectId{ 0 };
        uint8  _layer{ 0 };
    };

    /**
     * @class PhysicsWorld
     * @brief 겹침 질의와 레이어 필터입니다. step() 은 적분하지 않습니다.
     */
    class SW_API PhysicsWorld
    {
    public:
        using BodyHandle = SlotHandle;

        /** @brief 기본 레이어 행렬로 빈 월드를 만듭니다. */
        PhysicsWorld() = default;

        /** @brief AABB 바디를 등록합니다. */
        BodyHandle addBody( const AABB& aabb, uint8 layer, uint64 objectId = 0 );
        /** @brief 바디를 제거합니다. */
        void removeBody( BodyHandle handle );
        /** @brief 바디 AABB 를 갱신합니다. */
        void setAabb( BodyHandle handle, const AABB& aabb );
        /** @brief 핸들이 유효하면 out 에 복사하고 true 를 반환합니다. */
        bool tryGetBody( BodyHandle handle, PhysicsBody& out ) const;
        /** @brief 솔버 자리입니다. 지금은 아무것도 하지 않습니다(적분하지 않고, 부르는 곳도 없습니다). */
        void step( float32 deltaTime );

        /** @brief 두 바디가 레이어와 AABB 모두에서 겹치면 true 입니다. */
        bool overlaps( BodyHandle a, BodyHandle b ) const;
        /** @brief box 와 겹치는 바디 핸들을 out 에 넣습니다. */
        void queryAabb( const AABB& box, uint8 layer, vector<BodyHandle>& outListHandle ) const;
        /** @brief movingBox 가 displacement 만큼 움직일 때 layer 의 대상들과 연속 충돌(CCD)을 검사합니다. */
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
         * @details 넓은 질의는 셀을 다 방문하는 값이 바디를 모두 보는 값보다 비싸집니다. 예전에는 이
         *          숫자가 질의 두 곳에 리터럴로 적혀 있었습니다. 값이 같아 증상은 없었지만 한쪽만 바꾸면
         *          질의 종류에 따라 다른 문턱이 됩니다.
         */
        static constexpr int64 kMaxQueryCellCount = 1024;

        /**
         * @brief 바디 하나가 이 셀 수를 넘게 덮으면 그리드에 넣지 않고 **언제나 후보**로 둡니다.
         * @details 질의 쪽에는 상한이 있었는데 **삽입 쪽에는 없었습니다.** 큰 지형 · 바닥 콜라이더 하나가
         *          자기 AABB 가 덮는 모든 셀에 핸들을 적으므로, 20,000 유닛짜리 바닥이면 셀 표에
         *          **한 바디 때문에 십만 개 가까운 항목**이 생깁니다(64 유닛 셀 기준). `setAabb` 로
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
         * @details 이 계산("이 AABB 는 어느 셀들인가")이 **여섯 군데에 복사**돼 있었습니다. 삽입 · 제거 ·
         *          `setAabb` 의 옛/새 비교 둘 · `queryAabb` · `sweepTest` 입니다.
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

            /** @brief 범위의 셀마다 콜백을 부릅니다. 비어 있으면 한 번도 부르지 않습니다. */
            template <typename Func>
            void forEachCell( Func&& func ) const
            {
                for ( int32 gridZ = _minZ; gridZ <= _maxZ; ++gridZ )
                {
                    for ( int32 gridY = _minY; gridY <= _maxY; ++gridY )
                    {
                        for ( int32 gridX = _minX; gridX <= _maxX; ++gridX )
                        {
                            func( CellCoord{ gridX, gridY, gridZ } );
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

    private:
        mutable std::shared_mutex                                   _mutex;
        SlotHandleTable<PhysicsBody>                                _bodies;
        CollisionLayers                                             _layers;
        unordered_map<CellCoord, vector<BodyHandle>, CellCoordHash> _mapGrid;
        /** @brief 그리드에 넣기에는 너무 큰 바디들입니다. 그리드로 가는 질의가 **항상 함께** 봅니다. */
        vector<BodyHandle> _listOversizedBody;
    };
} // namespace sw
