/**
 * @file GimmickSensorComponent.h
 * @brief 기믹 센서가 읽는 오브젝트 쪽 상태입니다. 겹친 것(인원, 무게), 받은 피해, 쓰인 횟수, 바깥이 넣는 신호를 담습니다.
 * @details 회로(`GimmickCircuitComponent`)의 센서 노드(Volume, PressurePlate, Damage, Interaction, Signal)가 대상 오브젝트의 이 컴포넌트를 읽는다.
 *          겹침은 엔진의 겹침 훅(`onOverlapBegin/End`)으로 받으므로, 2D 콜라이더(`BoxCollider2DComponent`)와 3D 물리(트리거 이벤트가 같은 훅으로 올 때)
 *          모두 그대로 동작한다. 이 컴포넌트는 차원을 구분하지 않는다. 같은 오브젝트에 트리거 콜라이더를 둔다.
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
     * @brief 눌림판이 측정하는 무게입니다. 데이터로 덮어쓸 때 씁니다. 없으면 강체(`RigidBodyComponent`, `RigidBody2DComponent`)의 질량을 쓰고, 그것도 없으면 센서의 기본 무게를 씁니다.
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
        PROPERTY( Category = "Gimmick", DisplayName = "Weight", Min = 0.0, Units = kg )
        float32 _weight;
    };
} // namespace sw

namespace sw
{
    /**
     * @class GimmickSensorComponent
     * @brief 겹친 오브젝트 목록(태그로 거름), 쌓이는 충격(피해, 사용), 신호 하나를 담습니다. 틱하지 않습니다.
     * @details 겹침 목록은 게임 스레드의 겹침 훅이 바꾸고, 회로는 다음 틱에 읽는다. 피해, 사용, 신호는 아무 스레드(병렬 틱)에서 넣어도 되도록
     *          원자 값으로 두었고, 회로의 스텝이 소비한다(`consumeDamage`, `consumeUses`). 겹침 목록은 PROPERTY 이므로 핫 리로드와 플레이 복원을 거쳐도 유지된다.
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

        /** @brief 겹친 것으로 인정할 오브젝트인지 판정합니다. 필요한 태그를 모두 가지고 있으면 true 이고, 필요한 태그가 없으면 모든 오브젝트를 인정합니다. */
        bool acceptsOccupant( const GameObject& object ) const;
        /** @brief 바깥(테스트나 게임 규칙)이 겹친 것을 직접 추가하거나 뺍니다. 겹침 훅과 같은 목록을 씁니다. */
        void addOccupant( const GameObject& object );
        void removeOccupant( GameObjectHandle handle );

        int32 getOccupantCount() const { return static_cast<int32>( _listOccupant.size() ); }
        /** @brief 겹친 것들의 무게 합입니다. `GimmickWeightComponent`, 강체 질량, 기본 무게 순으로 찾아서 씁니다. 사라진 것은 세지 않습니다. */
        float32                         computeOccupantWeight() const;
        const vector<GameObjectHandle>& getOccupants() const { return _listOccupant; }

        /** @brief 피해를 받습니다. 아무 스레드에서 불러도 됩니다. 다음 회로 스텝의 Damage 센서가 소비합니다. */
        void applyDamage( float32 amount );
        /** @brief 쓰였음을 알립니다. 상호작용이 완료되면 부르며 아무 스레드에서 불러도 됩니다. 다음 스텝의 Interaction 센서가 소비합니다. */
        void notifyUsed();
        /** @brief 신호 값을 정합니다. 원소 상태나 게임 규칙이 넣는 수준이며, Signal 센서가 읽습니다. */
        void    setSignal( float32 value ) { _signal.store( value, std::memory_order_relaxed ); }
        float32 getSignal() const { return _signal.load( std::memory_order_relaxed ); }

        /** @brief 쌓인 피해를 꺼내고 비웁니다. */
        float32 consumeDamage();
        /** @brief 쌓인 사용 횟수를 꺼내고 비웁니다. `notifyUsed` 로 쌓인 것과, 같은 오브젝트의 `InteractableComponent` 가 지난번 이후 완료한 수를 더한 값입니다. */
        float32 consumeUses();

    private:
        PROPERTY( Category = "Sensor", DisplayName = "Required Tags", Tooltip = "Only objects with all of these tags count; empty counts everything" )
        TagContainer _requiredTags;
        PROPERTY( Category = "Sensor", DisplayName = "Occupants", Tooltip = "Objects overlapping now (runtime)" )
        vector<GameObjectHandle> _listOccupant;
        PROPERTY( Category = "Sensor", DisplayName = "Default Weight", Min = 0.0, Tooltip = "Weight of an occupant without a GimmickWeightComponent", Units = kg )
        float32 _defaultWeight;
        PROPERTY( Category = "Sensor", DisplayName = "Count Triggers", Tooltip = "Also count the other object's trigger colliders" )
        bool _bCountTriggers;

        atomic<float32> _pendingDamage;
        atomic<float32> _pendingUse;
        atomic<uint32>  _seenCompletedCount; ///< 지난번에 읽은 `InteractableComponent::getCompletedCount` 값
        atomic<float32> _signal;
    };
} // namespace sw
