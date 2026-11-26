#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

#include "Engine/Destruction/Fracture/FractureBaseComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

namespace sw
{
    /**
     * @class DestructibleComponent
     * @brief 부서지는 엄폐물입니다. 받은 피해가 `_listStageThreshold` 를 넘을 때마다 단계가 오르고("Stage" 연출과 회로 신호), 마지막 임계값을 넘으면 몸을 끕니다.
     * @details 같은 오브젝트의 센서 신호에 단계(0..)를 넣어 회로가 읽게 한다. 오브젝트에 파괴 컴포넌트(`FractureBaseComponent`)가 있으면 단계가 오를 때마다
     *          중심에 변형(`_stageStrain` × 단계 비율)을 주어 조각을 깎아 내고, 마지막 단계에서는 몸을 끄는 대신 `_shatterStrain` 으로 통째로 부순다.
     *          파쇄 데이터가 없으면 몸을 끈다(`GimmickCueEvent` "Destroyed").
     */
    REFLECT( Category = "Gimmick", DisplayName = "Destructible", Tooltip = "Damage stages ending in destruction (fracture hook)" )
    class DestructibleComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr float32 kTickInterval = constant::time::kDefaultFixedStepSeconds; ///< 고정 걸음마다 한 번 — 지연 · 간격을 걸음 수로 센다(프레임 시간과 무관하게 같은 걸음)

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
    };

    DestructibleComponent::DestructibleComponent()
        : _listStageThreshold{ 20.0f, 50.0f }
        , _stageStrain{ 80.0f }
        , _shatterStrain{ 600.0f }
        , _damageTaken{ 0.0f }
        , _stage{ 0 }
    {
    }

    void DestructibleComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const int32 stepCount = context.getIntervalCount();
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void DestructibleComponent::stepOnce()
    {
        GameObject*             pOwner  = getOwner();
        GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( pSensor == nullptr )
            return;
        if ( isDestroyed() )
        {
            pSensor->setSignal( static_cast<float32>( _stage ) ); // 되살린 판의 센서는 신호를 싣지 않는다 — 부서진 단계에서 다시 낸다
            return;
        }
        _damageTaken += pSensor->consumeDamage();
        const int32            stageCount = static_cast<int32>( _listStageThreshold.size() );
        FractureBaseComponent* pFracture  = pOwner->getComponent<FractureBaseComponent>();
        const bool             bFracture  = pFracture != nullptr && pFracture->hasFractureData();
        while ( _stage < stageCount && _damageTaken >= _listStageThreshold[static_cast<size_t>( _stage )] )
        {
            ++_stage;
            const SceneComponent* pScene   = pOwner->getPrimarySceneComponent();
            const float3          position = pScene != nullptr ? pScene->getWorldPosition() : float3{};
            GimmickUtil::sendCue( *pOwner, hashed_string( _stage >= stageCount ? "Destroyed" : "Stage" ), position, static_cast<float32>( _stage ) );
            if ( bFracture == false )
                continue;
            // 단계마다 조각을 깎고, 마지막 단계는 통째로 부순다.
            const float32 strain = _stage >= stageCount ? _shatterStrain : _stageStrain * static_cast<float32>( _stage ) / static_cast<float32>( stageCount );
            GimmickUtil::breakAtCenter( *pFracture, position, strain, strain * 0.5f );
        }
        pSensor->setSignal( static_cast<float32>( _stage ) );
        if ( isDestroyed() && bFracture == false )
            GimmickUtil::setBodyActive( *pOwner, false, this );
    }
} // namespace sw

#include "DestructibleComponent.cpp.gen.inl"
