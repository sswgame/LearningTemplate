/**
 * @file GimmickSensorComponent.h
 * @brief 기믹 센서가 읽는 오브젝트 쪽 상태 — 겹친 것(인원 · 무게), 받은 피해, 쓰인 횟수, 바깥이 넣는 신호입니다.
 * @details 회로(`GimmickCircuitComponent`)의 센서 노드(Volume · PressurePlate · Damage · Interaction · Signal)가 대상 오브젝트의 이 컴포넌트를 읽습니다.
 *          겹침은 엔진의 겹침 훅(`onOverlapBegin/End`)으로 받으므로 2D 콜라이더(`BoxCollider2DComponent`)와 3D 물리(트리거 이벤트가 같은 훅으로 오면)
 *          모두 그대로 됩니다 — 이 컴포넌트는 차원을 모릅니다. 같은 오브젝트에 트리거 콜라이더를 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class GimmickWeightComponent
     * @brief 눌림판이 재는 무게입니다(없는 오브젝트는 센서의 기본 무게). 강체가 생기면 강체 질량을 대신 읽습니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Gimmick Weight", Tooltip = "Weight a pressure plate measures for this object" )
    class SW_GF_API GimmickWeightComponent : public Component
    {
    public:
        REFLECT_BODY();

        GimmickWeightComponent();
        virtual ~GimmickWeightComponent() override = default;

        float32 getWeight() const { return _weight; }
        void    setWeight( float32 weight ) { _weight = weight; }

    private:
        PROPERTY( Category = "Gimmick", DisplayName = "Weight", Min = 0.0, Meta = "Units=kg" )
        float32 _weight;
    };
} // namespace sw

namespace sw
{
    /**
     * @class GimmickSensorComponent
     * @brief 겹친 오브젝트 목록(태그 거르기)과 쌓이는 충격(피해 · 사용)과 신호 하나입니다. 틱하지 않습니다.
     * @details 겹침 목록은 게임 스레드의 겹침 훅이 바꾸고, 회로는 다음 틱에 읽습니다. 피해 · 사용 · 신호는 아무 스레드(병렬 틱)에서 넣어도 되도록
     *          원자 값이고, 회로의 걸음이 먹습니다(`consumeDamage` · `consumeUses`). 겹침 목록은 PROPERTY 라 핫 리로드 · 플레이 복원을 넘깁니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Gimmick Sensor", Tooltip = "Occupants, damage, uses and a signal that gimmick circuit sensors read" )
    class SW_GF_API GimmickSensorComponent : public Component
    {
    public:
        REFLECT_BODY();

        GimmickSensorComponent();
        virtual ~GimmickSensorComponent() override = default;

        void onOverlapBegin( const OverlapInfo& overlap ) override;
        void onOverlapEnd( const OverlapInfo& overlap ) override;

        /** @brief 겹친 것으로 칠 오브젝트인가 — 필요한 태그를 모두 가졌는가입니다(태그가 없으면 모두). */
        bool acceptsOccupant( const GameObject& object ) const;
        /** @brief 바깥(시험 · 게임 규칙)이 겹친 것을 직접 더하거나 뺍니다. 겹침 훅과 같은 목록입니다. */
        void addOccupant( const GameObject& object );
        void removeOccupant( GameObjectHandle handle );

        int32 getOccupantCount() const { return static_cast<int32>( _listOccupant.size() ); }
        /** @brief 겹친 것들의 무게 합입니다(`GimmickWeightComponent`, 없으면 기본 무게). 사라진 것은 세지 않습니다. */
        float32                         computeOccupantWeight() const;
        const vector<GameObjectHandle>& getOccupants() const { return _listOccupant; }

        /** @brief 피해를 받습니다(아무 스레드). 다음 회로 걸음의 Damage 센서가 먹습니다. */
        void applyDamage( float32 amount );
        /** @brief 쓰였습니다(상호작용 완료 — 아무 스레드). 다음 걸음의 Interaction 센서가 먹습니다. */
        void notifyUsed();
        /** @brief 신호 값을 정합니다(원소 상태 · 게임 규칙이 넣는 수준). Signal 센서가 읽습니다. */
        void    setSignal( float32 value ) { _signal.store( value, std::memory_order_relaxed ); }
        float32 getSignal() const { return _signal.load( std::memory_order_relaxed ); }

        /** @brief 쌓인 피해를 꺼내고 비웁니다. */
        float32 consumeDamage();
        /** @brief 쌓인 사용 횟수를 꺼내고 비웁니다. */
        float32 consumeUses();

    private:
        PROPERTY( Category = "Sensor", DisplayName = "Required Tags", Tooltip = "Only objects with all of these tags count; empty counts everything" )
        TagContainer _requiredTags;
        PROPERTY( Category = "Sensor", DisplayName = "Occupants", Tooltip = "Objects overlapping now (runtime)" )
        vector<GameObjectHandle> _listOccupant;
        PROPERTY( Category = "Sensor", DisplayName = "Default Weight", Min = 0.0, Tooltip = "Weight of an occupant without a GimmickWeightComponent", Meta = "Units=kg" )
        float32 _defaultWeight;
        PROPERTY( Category = "Sensor", DisplayName = "Count Triggers", Tooltip = "Also count the other object's trigger colliders" )
        bool _bCountTriggers;

        atomic<float32> _pendingDamage;
        atomic<float32> _pendingUse;
        atomic<float32> _signal;
    };
} // namespace sw
