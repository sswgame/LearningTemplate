/**
 * @file AbilityEffectVolumeComponent.h
 * @brief 공간 효과 영역입니다 — 들어온 개체에 Ability 카탈로그의 이펙트를 걸고(진입 · 체류 주기), 나가면 지웁니다(정책). 둔화 늪 · 치유 샘 · 독 안개.
 * @details 새 버프 체계를 두지 않는다: 이펙트 정의(속성 · 태그 · 스택 · 지속)가 정본이고 영역은 "언제 거나" 만 정한다. 이동 제약은 그 이펙트의 속성 · 태그를
 *          이동기가 읽어 생긴다(`MovementConstraint`). 같은 오브젝트의 트리거 콜라이더 겹침으로 진입 · 이탈을 안다(`GimmickSensorComponent` 와 같은 길).
 *          건 길은 `volume:<이 오브젝트 이름>` 이라 Ability 디버거가 출처를 보이고, 이탈 · 영역 제거 때 그 길의 이펙트만 지운다(`removeActiveEffectsFromSource`) —
 *          겹친 다른 영역 · 상호작용의 효과는 남는다. 언리얼 GAS 의 Gameplay Effect Volume(샘플) · 유니티 트리거 + 버프와 같은 자리.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 영역을 나갈 때 이펙트를 어떻게 하나입니다. */
    ENUM()
    enum class EffectVolumeExitPolicy : uint8
    {
        RemoveOnExit = 0, ///< 나가면 이 영역이 건 이펙트를 지운다(늪의 둔화)
        KeepUntilExpired  ///< 이펙트가 제 지속 시간대로 끝난다(독이 묻어 나간다)
    };

    /**
     * @class AbilityEffectVolumeComponent
     * @brief 진입하면 이펙트를 걸고, 안에 있는 동안 `_reapplyInterval` 마다 다시 걸고(0 이면 진입 때만), 나가면 정책대로 지웁니다. 필터 태그가 맞지 않는 개체 · AbilityComponent 가 없는 개체는 건너뜁니다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "Ability Effect Volume", Tooltip = "Applies an ability effect to objects inside its trigger (enter, stay interval, exit policy)" )
    class SW_GF_API AbilityEffectVolumeComponent : public Component
    {
    public:
        REFLECT_BODY();

        AbilityEffectVolumeComponent();
        ~AbilityEffectVolumeComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;
        void onOverlapBegin( const OverlapInfo& overlap ) override;
        void onOverlapEnd( const OverlapInfo& overlap ) override;

        /** @brief 개체가 들어왔습니다(겹침 · 테스트). 필터에 맞고 AbilityComponent 가 있으면 이펙트를 걸고 true 입니다. */
        bool enter( GameObject& occupant );
        /** @brief 개체가 나갔습니다(겹침 · 테스트). 안에 있던 개체면 정책대로 하고 true 입니다. */
        bool exit( GameObject& occupant );
        /** @brief 코드로 만든 영역 · 테스트가 칸을 정합니다. 씬에 놓은 영역은 PROPERTY 가 채웁니다. */
        void configure( const hashed_string& effectID, EffectVolumeExitPolicy exitPolicy, float32 reapplyInterval, TagID occupantTag = TagID{} );

        /** @brief 이 영역이 건 이펙트의 건 길(`volume:<오브젝트 이름>`)입니다. */
        hashed_string getSourceName() const;
        /** @brief 안에 있는 개체 수입니다(디버거 · 시험). */
        uint32 getOccupantCount() const { return static_cast<uint32>( _listOccupant.size() ); }

    private:
        /** @brief @p occupant 에 이펙트를 한 번 겁니다. 걸렸으면 true(거부는 대상의 이펙트 기록에 까닭이 남는다). */
        bool applyTo( GameObject& occupant ) const;

    private:
        PROPERTY( Required, Category = "Effect", DisplayName = "Effect", Tooltip = "Effect id in the AbilityCatalog applied to objects inside" )
        hashed_string _effectID;
        PROPERTY( Category = "Effect", DisplayName = "Exit Policy", Tooltip = "Remove the effect when the object leaves, or let it run out" )
        EffectVolumeExitPolicy _exitPolicy;
        PROPERTY( Category = "Effect", DisplayName = "Reapply Interval", Min = 0.0, Tooltip = "Apply again every this many seconds while inside (0: only on enter)", Units = s )
        float32 _reapplyInterval;
        PROPERTY( Category = "Effect", DisplayName = "Occupant Tag", Tooltip = "Only objects with this tag are affected (empty: any object with an ability component)" )
        TagID _occupantTag;

        vector<GameObjectHandle> _listOccupant; ///< 안에 있는 개체(겹침 순)
        float32                  _reapplyTimer; ///< 다음 다시 걸기까지 남은 시간
    };
} // namespace sw
