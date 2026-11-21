/**
 * @file NoiseEmitterComponent.h
 * @brief 소리를 내는 것(기계 · 삐걱이는 마루)입니다. AI 청각이 읽을 소리 반경을 남깁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class NoiseEmitterComponent
     * @brief 소리를 냅니다. `emit` 로 직접 낼 수 있고, `_interval` 마다 낼 수 있으며(기계), 같은 오브젝트의 센서에 새로 올라선 것이 있을 때도 냅니다(삐걱이는 마루). `GimmickNoiseEvent` 와
     *        마지막 반경(`getNoiseRadius`)을 남기며, 반경은 `AIStimulus::_noiseRadius` 로 넘깁니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Noise Emitter", Tooltip = "Emits noise events AI hearing can use (periodic, on step, or scripted)" )
    class SW_GF_API NoiseEmitterComponent : public Component
    {
    public:
        REFLECT_BODY();

        NoiseEmitterComponent();
        virtual ~NoiseEmitterComponent() override = default;

        void onTick( TickContext& context ) override;
        void stepOnce();
        /** @brief 반경 @p radius 의 소리를 냅니다. */
        void    emit( float32 radius );
        float32 getNoiseRadius() const { return _noiseRadius; }
        int32   getEmitCount() const { return _emitCount; }

    private:
        PROPERTY( Category = "Noise", DisplayName = "Radius", Min = 0.0, Units = m )
        float32 _radius;
        PROPERTY( Category = "Noise", DisplayName = "Interval", Min = 0.0, Tooltip = "0 disables periodic noise", Units = s )
        float32 _interval;
        PROPERTY( Category = "Noise", DisplayName = "On Step", Tooltip = "Emit when something steps onto the sensor" )
        bool _bOnStep;
        PROPERTY( RuntimeState, Category = "Noise", DisplayName = "Noise Radius", Tooltip = "Runtime: radius of the last noise this step" )
        float32 _noiseRadius;
        PROPERTY( RuntimeState, Category = "Noise", DisplayName = "Steps", Tooltip = "Runtime" )
        int32 _steps;
        PROPERTY( RuntimeState, Category = "Noise", DisplayName = "Occupants", Tooltip = "Runtime" )
        int32 _lastOccupantCount;
        PROPERTY( RuntimeState, Category = "Noise", DisplayName = "Emit Count", Tooltip = "Runtime" )
        int32 _emitCount;

        FixedStepTimer _clock;
    };
} // namespace sw
