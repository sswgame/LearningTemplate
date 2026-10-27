/**
 * @file InterestReplicationPolicy.h
 * @brief 클라이언트-서버 복제의 관심 영역 정책입니다 — 관찰자(연결의 폰) 근처의 엔티티만 보냅니다(결정 기록 5-9 R2).
 * @details 규칙은 MMO 키트와 같은 Core 부품 `NetInterestGrid` 다(들어오는 반경 안이면 보이기 시작, 나가는 반경 밖으로 가야 빠짐 — 히스테리시스).
 *          `IReplicationPolicy::isRelevant` 는 여러 스레드에서 읽기만 하므로, 보이는 목록은 게임 스레드가 `sendSnapshots` 앞에 `updateRelevancy` 로 한 번 정한다.
 *          - 자리를 알리지 않은 엔티티(판 상태 · 점수판처럼 공간에 없는 것)는 모두에게 보낸다 — 공간 정책이 판단할 대상이 아니다.
 *          - 관찰자를 정하지 않은 연결에는 자리를 알린 엔티티를 보내지 않는다(자리 없는 엔티티와 소유자 상태만 간다).
 *          - 보내지 않게 된 엔티티는 스냅샷 델타가 클라이언트에서 지운다(`ReplicationServer` 머리말). 와이어 형식은 바뀌지 않는다.
 *          우선도는 기본 정책과 같은 모양(중요도 1 / (1 + 거리 / 10))이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Network/Replication/NetInterestGrid.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"

namespace sw
{
    /**
     * @class InterestReplicationPolicy
     * @brief 엔티티 자리와 연결의 관찰 중심을 받아 연결마다 보이는 엔티티를 정합니다.
     * @code
     *     if ( policy.initialize( 16.0f, 40.0f, 48.0f ) == false ) return false;
     *     server.initialize( &peers, settings, &policy );
     *     // 틱마다(게임 스레드): policy.setEntityPosition( id, pos ) … policy.setObserver( connectionID, pawnPos ) … policy.updateRelevancy(); server.sendSnapshots();
     * @endcode
     */
    class SW_KIT_API InterestReplicationPolicy final : public IReplicationPolicy
    {
    public:
        InterestReplicationPolicy();

        /** @brief 칸 크기와 두 반경을 정하고 비웁니다. 나가는 반경 < 들어오는 반경 등 쓸 수 없는 값이면 오류 로그와 false 이고 아무것도 바꾸지 않습니다. */
        [[nodiscard]] bool initialize( float32 cellSize, float32 enterRadius, float32 leaveRadius );

        void setEntityPosition( uint32 entityID, const float3& position );
        void removeEntity( uint32 entityID );
        /** @brief 연결 @p connectionID 의 관찰 중심(보통 그 연결의 폰)을 정합니다. */
        void setObserver( int32 connectionID, const float3& center );
        void removeObserver( int32 connectionID );
        /** @brief 관찰자마다 보이는 목록을 고칩니다(나간 것 빼기 → 들어온 것 더하기). 게임 스레드에서 `sendSnapshots` 앞에 한 번 부릅니다. */
        void updateRelevancy();

        bool    isRelevant( int32 connectionID, const NetEntityState& entity ) const override;
        float32 computePriority( int32 connectionID, const NetEntityState& entity ) const override;

        int32 getVisibleCount( int32 connectionID ) const;

    private:
        struct Observer
        {
            unordered_set<uint32> _uniqueVisible{};
            float3                _center{ 0.0f, 0.0f, 0.0f };
            uint8                 _bActive{ SW_FALSE };
        };

        const Observer* findObserver( int32 connectionID ) const;

        NetInterestGrid               _interest;
        unordered_map<uint32, float3> _mapPosition; ///< 자리를 알린 엔티티
        vector<Observer>              _listObserver;
        vector<SlotHandle>            _listKeyScratch;
        vector<uint32>                _listLeaveScratch;
    };
} // namespace sw
