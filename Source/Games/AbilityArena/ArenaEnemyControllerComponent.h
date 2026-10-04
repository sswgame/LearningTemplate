/**
 * @file ArenaEnemyControllerComponent.h
 * @brief 적 유닛의 AI — 근접(Grunt)은 다가와 때리고, 원거리(Caster)는 거리를 지키며 쏩니다. 적끼리는 겹치지 않게 밀려납니다.
 */
#pragma once
#include "Games/AbilityArena/ArenaControllerComponent.h"

namespace sw
{
    /**
     * @class ArenaEnemyControllerComponent
     * @brief Grunt · Caster 프리팹의 컨트롤러입니다. 대상은 디렉터의 유닛 모습에서 가장 가까운 적대 유닛입니다.
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Enemy Controller", Tooltip = "Grunt / caster AI of an arena enemy" )
    class ArenaEnemyControllerComponent : public ArenaControllerComponent
    {
    public:
        REFLECT_BODY();

        ArenaEnemyControllerComponent();
        virtual ~ArenaEnemyControllerComponent() override = default;

    protected:
        void tickController( float32 deltaTime, const ArenaDirectorComponent& director, AbilitySystemComponent& abilitySystem, float3& inoutPosition ) override;

    private:
        /** @brief 다른 적과 겹친 만큼의 절반을 자기 쪽으로 밀어냅니다(상대도 같은 일을 한다 — 물리 없이, 유닛 수가 적다). */
        void separateFromEnemies( const ArenaDirectorComponent& director, float3& inoutPosition ) const;

    private:
        PROPERTY( Category = "AI", DisplayName = "Kind", Tooltip = "Grunt walks up and strikes; Caster keeps its distance and shoots" )
        ArenaUnitKind _kind;
        PROPERTY( Category = "AI", DisplayName = "Melee Reach", Tooltip = "Grunt stops and strikes inside this distance", Min = 0.0, Meta = "Units=m" )
        float32 _meleeReach;
        PROPERTY( Category = "AI", DisplayName = "Preferred Min", Tooltip = "Caster backs off inside this distance", Min = 0.0, Meta = "Units=m" )
        float32 _preferredMin;
        PROPERTY( Category = "AI", DisplayName = "Preferred Max", Tooltip = "Caster closes in beyond this distance", Min = 0.0, Meta = "Units=m" )
        float32 _preferredMax;
        PROPERTY( Category = "AI", DisplayName = "Fire Range", Tooltip = "Caster shoots inside this distance", Min = 0.0, Meta = "Units=m" )
        float32 _fireRange;
        PROPERTY( Category = "AI", DisplayName = "Unit Radius", Tooltip = "Body radius used to push overlapping enemies apart", Min = 0.0, Meta = "Units=m" )
        float32 _unitRadius;
    };
} // namespace sw
