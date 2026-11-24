/**
 * @file NoiseEmitterComponent.h
 * @brief 소리를 내는 것(기계 · 삐걱이는 마루)입니다. 같은 오브젝트의 AI 자극 원천에 소리 반경을 겁니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class NoiseEmitterComponent
     * @brief 소리를 냅니다. `emit` 로 직접 낼 수 있고, `_interval` 마다 낼 수 있으며(기계), 같은 오브젝트의 센서에 새로 올라선 것이 있을 때도 냅니다(삐걱이는 마루).
     * @details 반경은 같은 오브젝트의 `AIStimulusSourceComponent::reportNoise` 로 걸어 AI 감각이 색인으로 듣고(필수 — 없으면 시작에서 오류), 다음 프레임 틱에서 거둔다.
     *          이름으로 더 받을 쪽(게임 규칙 · 연출)을 위해 `GimmickNoiseEvent` 도 낸다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Noise Emitter", Tooltip = "Emits noise events AI hearing can use (periodic, on step, or scripted)" )
    class SW_GF_API NoiseEmitterComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr float32 kTickInterval = constant::time::kDefaultFixedStepSeconds; ///< 고정 걸음마다 한 번 — 지연 · 간격을 걸음 수로 센다(프레임 시간과 무관하게 같은 걸음)

        NoiseEmitterComponent();
        virtual ~NoiseEmitterComponent() override = default;

        /** @brief 같은 오브젝트에 AI 자극 원천이 없으면 오류를 남깁니다. */
        void onBeginPlay() override;
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
        PROPERTY( RuntimeState, Category = "Noise", DisplayName = "Noise Radius", Tooltip = "Runtime: radius of the noise held this frame" )
        float32 _noiseRadius;
        PROPERTY( RuntimeState, Category = "Noise", DisplayName = "Steps", Tooltip = "Runtime" )
        int32 _steps;
        PROPERTY( RuntimeState, Category = "Noise", DisplayName = "Occupants", Tooltip = "Runtime" )
        int32 _lastOccupantCount;
        PROPERTY( RuntimeState, Category = "Noise", DisplayName = "Emit Count", Tooltip = "Runtime" )
        int32 _emitCount;
    };
} // namespace sw
