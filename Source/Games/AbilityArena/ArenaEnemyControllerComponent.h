/**
 * @file ArenaEnemyControllerComponent.h
 * @brief 적 유닛의 AI — 근접(Grunt)은 다가와 때리고, 원거리(Caster)는 거리를 지키며 쏩니다. 적끼리는 겹치지 않게 밀려납니다.
 */
#pragma once
#include "Games/AbilityArena/ArenaControllerComponent.h"

namespace sw
{
    struct ArenaUnitView;

    /**
     * @class ArenaEnemyControllerComponent
     * @brief Grunt · Caster 프리팹의 컨트롤러입니다. 대상은 디렉터의 유닛 모습에서 가장 가까운 적대 유닛입니다.
     * @details **주 틱이 아니라 서브틱(`kChaseSubTick`, DuringPhysics)에서 돕니다.** 대상 컨트롤러의 주 틱을 선행 조건으로 걸어 그 뒤에 돌고, 대상의
     *          이번 프레임 자리(대상 메시의 월드 자리 — 선행 조건 스테이지 앞에서 적용된다)를 쫓고 바라봅니다. 디렉터의 모습은 PrePhysics 에서 적은
     *          틱 전 자리라 그것을 쓰면 한 프레임 늦은 자리를 쫓는다. 대상이 바뀌면 선행 조건을 갈아 걸고, 그 프레임만 모습의 자리를 씁니다.
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Enemy Controller", Tooltip = "Grunt / caster AI of an arena enemy" )
    class ArenaEnemyControllerComponent : public ArenaControllerComponent
    {
    public:
        REFLECT_BODY();

        /** @brief AI 를 돌리는 서브틱 번호입니다. */
        static constexpr uint32 kChaseSubTick = 1;

        ArenaEnemyControllerComponent();
        virtual ~ArenaEnemyControllerComponent() override = default;

        /** @brief 주 틱을 끄고 AI 서브틱을 등록합니다. */
        void onBeginPlay() override;
        /** @brief AI 서브틱 — 컨트롤러 한 프레임(`ArenaControllerComponent::onTick`)입니다. */
        void onSubTick( uint32 subTickId, float32 deltaTime ) override;

    protected:
        void tickController( float32 deltaTime, const ArenaDirectorComponent& director, AbilitySystemComponent& abilitySystem, float3& inoutPosition ) override;

    private:
        /**
         * @brief 대상의 이번 프레임 자리입니다. 대상 컨트롤러의 주 틱이 선행 조건으로 걸려 있으면 대상 메시의 월드 자리이고, 아니면(대상이 바뀐 첫 프레임)
         *        선행 조건을 갈아 걸고 디렉터 모습의 자리를 돌려줍니다.
         */
        float3 resolveTargetPosition( const ArenaUnitView& target );
        /** @brief 다른 적과 겹친 만큼의 절반을 자기 쪽으로 밀어냅니다(상대도 같은 일을 한다 — 물리 없이, 유닛 수가 적다). */
        void separateFromEnemies( const ArenaDirectorComponent& director, float3& inoutPosition ) const;

    private:
        PROPERTY( Category = "AI", DisplayName = "Kind", Tooltip = "Grunt walks up and strikes; Caster keeps its distance and shoots" )
        ArenaUnitKind _kind;
        PROPERTY( Category = "AI", DisplayName = "Melee Reach", Tooltip = "Grunt stops and strikes inside this distance", Min = 0.0, Units = m )
        float32 _meleeReach;
        PROPERTY( Category = "AI", DisplayName = "Preferred Min", Tooltip = "Caster backs off inside this distance", Min = 0.0, Units = m )
        float32 _preferredMin;
        PROPERTY( Category = "AI", DisplayName = "Preferred Max", Tooltip = "Caster closes in beyond this distance", Min = 0.0, Units = m )
        float32 _preferredMax;
        PROPERTY( Category = "AI", DisplayName = "Fire Range", Tooltip = "Caster shoots inside this distance", Min = 0.0, Units = m )
        float32 _fireRange;
        PROPERTY( Category = "AI", DisplayName = "Unit Radius", Tooltip = "Body radius used to push overlapping enemies apart", Min = 0.0, Units = m )
        float32 _unitRadius;

        SubTickHandle _targetTick; ///< 지금 선행 조건으로 건 대상 컨트롤러의 주 틱
    };
} // namespace sw
