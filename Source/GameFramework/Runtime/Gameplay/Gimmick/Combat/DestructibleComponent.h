/**
 * @file DestructibleComponent.h
 * @brief 피해 단계를 거쳐 부서지는 엄폐물입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class DestructibleComponent
     * @brief 부서지는 엄폐물입니다. 받은 피해가 `_listStageThreshold` 를 넘을 때마다 단계가 오르고("Stage" 연출과 회로 신호), 마지막 임계값을 넘으면 몸을 끕니다.
     * @details 같은 오브젝트의 센서 신호에 단계(0..)를 넣어 회로가 읽게 한다. 오브젝트에 파괴 컴포넌트(`FractureComponentBase`)가 있으면 단계가 오를 때마다
     *          중심에 변형(`_stageStrain` × 단계 비율)을 주어 조각을 깎아 내고, 마지막 단계에서는 몸을 끄는 대신 `_shatterStrain` 으로 통째로 부순다.
     *          파쇄 데이터가 없으면 몸을 끈다(`GimmickCueEvent` "Destroyed").
     */
    REFLECT( Category = "Gimmick", DisplayName = "Destructible", Tooltip = "Damage stages ending in destruction (fracture hook)" )
    class SW_GF_API DestructibleComponent : public Component
    {
    public:
        REFLECT_BODY();

        DestructibleComponent();
        virtual ~DestructibleComponent() override = default;

        void onTick( TickContext& context ) override;
        void stepOnce();

        int32 getStage() const { return _stage; }
        bool  isDestroyed() const { return _stage >= static_cast<int32>( _listStageThreshold.size() ) && _listStageThreshold.empty() == false; }

    private:
        PROPERTY( Category = "Destructible", DisplayName = "Stage Thresholds", Tooltip = "Total damage at which each stage begins; the last destroys" )
        vector<float32> _listStageThreshold;
        PROPERTY( Category = "Destructible", DisplayName = "Stage Strain", Min = 0.0, Tooltip = "Fracture strain at the centre when a stage is reached (scaled by stage / last)" )
        float32 _stageStrain;
        PROPERTY( Category = "Destructible", DisplayName = "Shatter Strain", Min = 0.0, Tooltip = "Fracture strain that shatters the object at the last stage" )
        float32 _shatterStrain;
        PROPERTY( RuntimeState, Category = "Destructible", DisplayName = "Damage Taken", Tooltip = "Runtime" )
        float32 _damageTaken;
        PROPERTY( RuntimeState, Category = "Destructible", DisplayName = "Stage", Tooltip = "Runtime" )
        int32 _stage;

        FixedStepTimer _clock;
    };
} // namespace sw
