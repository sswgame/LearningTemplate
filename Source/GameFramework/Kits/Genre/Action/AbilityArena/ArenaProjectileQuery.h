/**
 * @file ArenaProjectileQuery.h
 * @brief 아레나의 탄이 묻는 세계(`IProjectileWorld`)입니다 — 이번 스냅샷의 유닛과 판 가장자리.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/AbilityArena/ArenaUnitView.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.h"

namespace sw
{
    /**
     * @class ArenaProjectileQuery
     * @brief 아레나의 탄이 묻는 세계입니다. 맞는 것은 이번 스냅샷의 적대 유닛(선분에서 유닛 반지름 + 탄 반지름 안, 선분 앞쪽이 먼저)이고, 판 밖은 표면입니다(튕기는 탄은 판 가장자리에서 튕긴다).
     * @details 편은 쏜 쪽이 플레이어 편인지로 가른다(플레이어 편 탄은 플레이어가 아닌 유닛을, 적 탄은 플레이어를 맞힌다). 쓰러진 유닛은 지나간다. 유도 대상은 두지 않는다.
     *          `_other` · 탄의 꿰뚫은 대상은 유닛 번호(`ArenaUnitView::_unitID`)다 — 스냅샷 첨자는 앞 유닛이 빠지면 밀려 관통 탄이 다음 프레임에 엉뚱한 유닛을 거른다
     *          (언리얼 GAS 가 이미 맞힌 대상을 액터 참조로 기억하는 것과 같다).
     */
    class SW_KIT_API ArenaProjectileQuery final : public IProjectileWorld
    {
    public:
        static constexpr int32 kTeamEnemy  = 0; ///< `ProjectileState::_team` — 적이 쏜 탄
        static constexpr int32 kTeamPlayer = 1; ///< 플레이어 편이 쏜 탄

        ArenaProjectileQuery( const vector<ArenaUnitView>& listView, float32 unitRadius, float32 boundsHalfSize );

        bool findTargetPoint( const ProjectileState& projectile, float3& outPoint ) const override;
        bool sweep( const ProjectileState& projectile, const float3& from, const float3& to, ProjectileHit& outHit ) const override;

        /** @brief 유닛 번호 @p unitID 의 스냅샷입니다. 이번 스냅샷에 없으면(정리됐다) nullptr 입니다. */
        const ArenaUnitView* findView( int32 unitID ) const;

    private:
        const vector<ArenaUnitView>& _listView;
        float32                      _unitRadius;
        float32                      _boundsHalfSize;
    };
} // namespace sw
