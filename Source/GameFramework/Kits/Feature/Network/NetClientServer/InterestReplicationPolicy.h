/**
 * @file InterestReplicationPolicy.h
 * @brief 클라이언트-서버 복제의 관심 영역 정책입니다 — 관찰자(연결의 폰) 근처의 엔티티만 보냅니다.
 * @details 규칙은 MMO 키트와 같은 Core 부품 `NetInterestGrid` 다(들어오는 반경 안이면 보이기 시작, 나가는 반경 밖으로 가야 빠짐 — 히스테리시스).
 *          `IReplicationPolicy::isRelevant` 는 여러 스레드에서 읽기만 하므로, 보이는 목록은 게임 스레드가 `sendSnapshots` 앞에 `updateRelevancy` 로 한 번 정한다.
 *          - 자리를 알리지 않은 엔티티(판 상태 · 점수판처럼 공간에 없는 것)는 모두에게 보낸다 — 공간 정책이 판단할 대상이 아니다.
 *          - 관찰자를 정하지 않은 연결에는 자리를 알린 엔티티를 보내지 않는다(자리 없는 엔티티와 소유자 상태만 간다).
 *          - 보내지 않게 된 엔티티는 스냅샷 델타가 클라이언트에서 지운다(`ReplicationServer` 머리말). 와이어 형식은 바뀌지 않는다.
 *          우선도는 기본 정책과 같은 모양(중요도 1 / (1 + 거리 / 10))이다.
 */
#pragma once
#include "Core/Container/SlotHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
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
        bool    evaluateEntity( int32 connectionID, const NetEntityState& entity, float32& outPriority ) const override;
        /** @brief 자리를 알리지 않은 엔티티의 월드 번호를 모읍니다(연결마다 월드 전체를 돌지 않게, 틱마다 한 번). */
        void prepareSnapshots( const vector<NetEntityState>& listWorldEntity ) override;
        /**
         * @brief 관찰자의 보이는 목록(id 순)과 자리 없는 엔티티 목록만 돌아 월드 차례로 합칩니다 — 엔티티 × 연결이던 일이 (보이는 수 + 자리 없는 수) × 연결이 된다.
         * @details 결과는 기본 구현(모든 엔티티에 `evaluateEntity`)과 같다: 보이는 목록에 남아 있어도 자리를 지운 엔티티는 자리 없는 엔티티로 한 번만 든다.
         */
        void collectRelevantEntities( int32 connectionID, const vector<NetEntityState>& listWorldEntity, vector<NetRelevantEntity>& outList ) const override;

        int32 getVisibleCount( int32 connectionID ) const;

    private:
        struct Observer
        {
            vector<uint32> _listVisible{}; ///< 보이는 엔티티 id(오름차순, 겹치지 않음 — 월드 목록과 같은 차례라 합치기만 한다)
            float3         _center{ 0.0f, 0.0f, 0.0f };
            uint8          _bActive{ SW_FALSE };
        };

        const Observer* findObserver( int32 connectionID ) const;

    private:
        NetInterestGrid               _interest;
        unordered_map<uint32, float3> _mapPosition; ///< 자리를 알린 엔티티
        vector<Observer>              _listObserver;
        vector<SlotHandle>            _listKeyScratch;
        vector<uint32>                _listEnterScratch;
        vector<uint32>                _listMergeScratch;
        vector<uint32>                _listUnpositionedIndex; ///< 이번 틱 월드에서 자리를 알리지 않은 엔티티의 번호(오름차순, `prepareSnapshots`)
    };
} // namespace sw
