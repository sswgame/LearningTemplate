/**
 * @file NetInterestGrid.h
 * @brief 서버 복제의 관심 영역(relevancy) 격자입니다 — 엔티티 자리를 XZ 공간 해시에 두고, 관찰자 둘레의 엔티티를 찾고, 들어옴 · 나감 반경으로 경계의 깜빡임을 막습니다.
 * @details 두 키트가 함께 쓴다(결정 기록 5-12 `D7-8`). MMO 복제기(`MMOReplicator`)는 수천 엔티티의 들어옴 질의에, 클라이언트-서버 복제(`ReplicationServer`)는
 *          관찰자마다 스냅샷에 실을 엔티티를 거르는 데 쓴다. 언리얼 Replication Graph 의 격자 공간화 노드(`UReplicationGraphNode_GridSpatialization2D`)와 같은 자리다.
 *          칸 크기는 데이터다 — 월드 스트리밍 격자(`WorldCellGrid`)의 셀 크기를 넘기는 것이 기본 쓰임이고, 코드에 기본값이 없다(`reset` 이 0 이하를 거절한다).
 *
 *          [히스테리시스] 보이지 않던 엔티티는 들어옴 반경 안이면 보이고, 보이던 엔티티는 나감 반경 밖으로 가야 사라진다(`isInInterest`). 두 반경의 관계
 *          (나감 >= 들어옴)는 쓰는 쪽의 설정 검사가 본다.
 *          자리가 없는 엔티티(점수판 · 게임 상태처럼 공간에 있지 않은 것)는 격자에 없고, 쓰는 쪽이 공간 밖 엔티티로 다룬다(`ReplicationServer` 는 늘 관련).
 * @warning 스레드 안전하지 않다. 틱 안에서 자리를 다 넣은 뒤에는 여러 스레드가 질의만 해도 된다(`queryCircle` · `findPosition` 은 const 이고 상태를 바꾸지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Spatial/SpatialHashGrid2D.h"

namespace sw
{
    /** @class NetInterestGrid @brief 파일 머리말 참고. */
    class SW_API NetInterestGrid
    {
    public:
        NetInterestGrid();

        /**
         * @brief 칸 크기를 정하고 자리를 모두 지웁니다.
         * @return @p cellSize 가 0 이하이면 오류를 남기고 false 이며 격자는 그대로입니다.
         */
        [[nodiscard]] bool reset( float32 cellSize );
        /** @brief `reset` 을 지났으면 true 입니다. 지나지 않은 격자는 자리를 받지 않습니다. */
        bool isReady() const { return _cellSize > 0.0f; }
        /** @brief 칸 크기입니다(`reset` 전이면 0). */
        float32 getCellSize() const { return _cellSize; }

        /** @brief 엔티티의 자리를 넣거나 옮깁니다. `isReady` 가 아니면 아무것도 하지 않습니다. */
        void setPosition( uint32 entityID, const float3& position );
        /** @brief 엔티티를 격자에서 뺍니다(없으면 아무것도 하지 않는다). */
        void remove( uint32 entityID );
        /** @brief 자리를 모두 지웁니다(칸 크기는 그대로). */
        void clearPositions();
        /** @brief 엔티티의 자리입니다. 격자에 없으면 nullptr 입니다. */
        const float3* findPosition( uint32 entityID ) const;
        /** @brief 격자에 든 엔티티 수입니다. */
        uint32 getEntityCount() const { return static_cast<uint32>( _mapPosition.size() ); }

        /**
         * @brief @p center 에서 XZ 거리 @p radius 안의 엔티티 id 를 채웁니다(먼저 비운다). 순서는 정해지지 않는다 — 결정적인 순서가 필요하면 부르는 쪽이 정렬한다.
         * @param inoutListScratchKey 해시의 키를 받는 작업 버퍼입니다(부르는 쪽 것 — 여러 스레드가 함께 질의하고, 틱마다 할당하지 않게).
         */
        void queryCircle( const float3& center, float32 radius, vector<SlotHandle>& inoutListScratchKey, vector<uint32>& outListEntityID ) const;

        /** @brief 히스테리시스 규칙 — 보이던 것은 @p leaveRadius 안이면, 보이지 않던 것은 @p enterRadius 안이면 관심 안입니다(XZ 거리). */
        static bool isInInterest( const float3& center, const float3& position, float32 enterRadius, float32 leaveRadius, bool bWasVisible );

    private:
        SpatialHashGrid2D             _grid;
        unordered_map<uint32, float3> _mapPosition;
        float32                       _cellSize;
    };
} // namespace sw
