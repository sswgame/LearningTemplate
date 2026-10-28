/**
 * @file NetInterestGrid.h
 * @brief 관심 영역(relevancy) 격자입니다 — 엔티티 점을 XZ 평면 공간 해시에 두고, 들어오는 반경 · 나가는 반경(히스테리시스)으로 관찰자에게 보일지 정합니다.
 * @details 복제 키트(MMO 의 `MMOReplicator`, 클라이언트-서버의 관심 영역 거르기)가 같은 규칙을 쓰도록 Core 에 둔다.
 *          - 들어오는 반경 안이면 보이기 시작하고, 나가는 반경 밖으로 가야 보이지 않게 된다. 나가는 반경 < 들어오는 반경은 `initialize` 가 거절한다(경계에서 들락날락한다).
 *          - 격자는 점만 든다(위치의 원본은 부르는 쪽의 엔티티 표다). 읽기(`queryEntering` · `isOutsideLeaveRadius`)는 상태를 바꾸지 않아 여러 스레드가 함께 불러도 된다 —
 *            쓰기(`setPoint` · `removePoint`)와 겹치지 않을 때.
 *          언리얼 Replication Graph 의 공간 노드(`UReplicationGraphNode_GridSpatialization2D`)와 같은 자리다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Spatial/SpatialHashGrid2D.h"

namespace sw
{
    /**
     * @class NetInterestGrid
     * @brief 엔티티 id 로 점을 넣고 빼며, 관찰자 중심에서 새로 보일 후보와 보이지 않게 될 판정을 줍니다.
     */
    class SW_API NetInterestGrid
    {
    public:
        NetInterestGrid();

        /**
         * @brief 칸 크기와 두 반경을 정하고 비웁니다. 칸 크기가 0 이하이거나, 반경이 음수이거나, 나가는 반경이 들어오는 반경보다 작으면 false 이고 아무것도 바꾸지 않습니다.
         * @param outError 거절 이유(로그에 남길 글)입니다.
         */
        [[nodiscard]] bool initialize( float32 cellSize, float32 enterRadius, float32 leaveRadius, string& outError );

        /** @brief 엔티티 점을 넣거나 옮깁니다(XZ 평면). */
        void setPoint( uint32 entityID, const float3& position );
        void removePoint( uint32 entityID );

        /** @brief @p center 의 들어오는 반경 안 엔티티 키를 @p outListKey 에 채웁니다(비우고 채운다). 키의 id 는 `getEntityID` 로 읽습니다. */
        void queryEntering( const float3& center, vector<SlotHandle>& outListKey ) const;
        /** @brief @p position 이 @p center 의 나가는 반경 밖이면 true 입니다(수평 거리). 이미 보이는 엔티티를 뺄지 정합니다. */
        bool isOutsideLeaveRadius( const float3& center, const float3& position ) const;

        float32 getEnterRadius() const { return _enterRadius; }
        float32 getLeaveRadius() const { return _leaveRadius; }

        /** @brief 엔티티 id 를 격자 키로 바꿉니다. 격자는 키를 비교하고 해시하기만 하므로 세대는 늘 1 이다(세대 0 은 무효 핸들이라 격자가 받지 않는다). */
        static SlotHandle makeKey( uint32 entityID ) { return SlotHandle::make( entityID, 1u ); }
        static uint32     getEntityID( SlotHandle key ) { return key.index(); }

    private:
        SpatialHashGrid2D _grid;
        float32           _enterRadius;
        float32           _leaveRadius;
    };
} // namespace sw
