/**
 * @file HorrorStealthGimmicks.h
 * @brief 공포와 잠입 기믹입니다. 놀래기 트리거, 깜빡이는 라이트(퀘이크 라이트 스타일 문자열), 숨는 곳(스마트 오브젝트 슬롯과 숨음 태그), 소리 내는 것, 빛 노출 질의를 다룹니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class ScareTriggerComponent
     * @brief 태그(보통 `Player`)를 가진 것이 처음 들어오면 연출 신호(`GimmickCueEvent`, `_cue`)와 소리를 냅니다. `_bOnce` 가 꺼져 있으면 `_cooldown` 뒤에 다시 작동합니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Scare Trigger", Tooltip = "Fires a scripted scare cue and sound when a tagged object enters" )
    class SW_GF_API ScareTriggerComponent : public Component
    {
    public:
        REFLECT_BODY();

        ScareTriggerComponent();
        virtual ~ScareTriggerComponent() override = default;

        void  onOverlapBegin( const OverlapInfo& overlap ) override;
        void  onTick( float32 deltaTime ) override;
        int32 getFireCount() const { return _fireCount; }

    private:
        PROPERTY( Category = "Scare", DisplayName = "Cue", Tooltip = "Cue name for the sequencer / camera / audio listeners" )
        hashed_string _cue;
        PROPERTY( Category = "Scare", DisplayName = "Sound", AssetPath, AssetType = "Audio" )
        string _sound;
        PROPERTY( Category = "Scare", DisplayName = "Required Tags" )
        TagContainer _requiredTags;
        PROPERTY( Category = "Scare", DisplayName = "Once" )
        bool _bOnce;
        PROPERTY( Category = "Scare", DisplayName = "Cooldown", Min = 0.0, Units = s )
        float32 _cooldown;
        PROPERTY( Category = "Scare", DisplayName = "Cooldown Left", Tooltip = "Runtime", Units = s )
        float32 _cooldownLeft;
        PROPERTY( Category = "Scare", DisplayName = "Fire Count", Tooltip = "Runtime" )
        int32 _fireCount;
    };
} // namespace sw

namespace sw
{
    /**
     * @class FlickerLightComponent
     * @brief 같은 오브젝트의 `LightComponent` 세기를 문자열 패턴으로 깜빡입니다. 퀘이크 라이트 스타일이며 'a' 는 0, 'm' 은 1, 'z' 는 2배이고, 속도는 `_rate` 글자/초입니다.
     * @details 스텝 수로 글자를 고르므로 결정적이다. 같은 스텝이면 같은 밝기가 나온다. 처음 세기를 기준으로 곱하고, 바뀔 때만 틱 뒤에 쓴다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Flicker Light", Tooltip = "Drives the light intensity from a Quake-style pattern string" )
    class SW_GF_API FlickerLightComponent : public Component
    {
    public:
        REFLECT_BODY();

        FlickerLightComponent();
        virtual ~FlickerLightComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;
        /** @brief 스텝 @p step 의 밝기 배율입니다. 패턴이 비어 있으면 1 입니다. */
        float32 computeScaleAtStep( int32 step ) const;

    private:
        PROPERTY( Category = "Flicker", DisplayName = "Pattern", Tooltip = "a = off, m = normal, z = double" )
        string _pattern;
        PROPERTY( Category = "Flicker", DisplayName = "Rate", Min = 0.1, Tooltip = "Pattern letters per second" )
        float32 _rate;
        PROPERTY( Category = "Flicker", DisplayName = "Step", Tooltip = "Runtime" )
        int32 _step;

        FixedStepTimer _clock;
        float32        _baseIntensity;
        float32        _appliedScale;
    };
} // namespace sw

namespace sw
{
    /**
     * @class HidingSpotComponent
     * @brief 숨는 곳입니다(사물함, 옷장, 침대 밑). 같은 오브젝트의 `SmartObjectComponent` 슬롯(Activity.Hide)을 차지하면 `State.Hidden` 태그를 붙입니다.
     *        AI 감각은 그 태그를 보고 대상을 보지 않습니다(`isHidden`). 상호작용(Hide)을 끝내면 들어가고, 다시 끝내면 나옵니다. 센서의 사용 알림을 읽어서 처리합니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Hiding Spot", Tooltip = "Claims a smart object slot and tags the occupant as hidden" )
    class SW_GF_API HidingSpotComponent : public Component
    {
    public:
        REFLECT_BODY();

        HidingSpotComponent();
        virtual ~HidingSpotComponent() override = default;

        void onTick( float32 deltaTime ) override;
        /** @brief 숨깁니다. 슬롯이 없으면 false 입니다. */
        [[nodiscard]] bool enter( GameObject& who );
        /** @brief 나옵니다. 숨어 있지 않았으면 false 입니다. */
        bool exit( GameObject& who );
        /** @brief 숨음 태그를 가지고 있으면 true 입니다. AI 감각이 이 값을 묻습니다. */
        static bool isHidden( const GameObject& object );
    };
} // namespace sw

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

        void onTick( float32 deltaTime ) override;
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
        PROPERTY( Category = "Noise", DisplayName = "Noise Radius", Tooltip = "Runtime: radius of the last noise this step" )
        float32 _noiseRadius;
        PROPERTY( Category = "Noise", DisplayName = "Steps", Tooltip = "Runtime" )
        int32 _steps;
        PROPERTY( Category = "Noise", DisplayName = "Occupants", Tooltip = "Runtime" )
        int32 _lastOccupantCount;
        PROPERTY( Category = "Noise", DisplayName = "Emit Count", Tooltip = "Runtime" )
        int32 _emitCount;

        FixedStepTimer _clock;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LightExposure
     * @brief 잠입의 빛 노출입니다. 한 위치가 씬의 라이트(점, 스폿, 방향)에 얼마나 드러나는지 계산하며, 0 이면 어둠입니다. 점 라이트와 스폿 라이트는 반경 안에서 (1 − d/r)² × 세기이고(스폿은 원뿔 밖이면 0),
     *        방향광은 세기 그대로입니다. @p bOcclusion 이 켜져 있으면 라이트까지 광선이 막힐 때 그 라이트를 뺍니다(방향광은 라이트 반대쪽 100 m 까지 검사). 꺼져 있으면 가림을 검사하지 않습니다.
     */
    struct SW_GF_API LightExposure
    {
        static float32 computeExposure( const GameObjectManager& manager, const float3& position, bool bOcclusion, uint64 ignoreObjectID = 0 );
    };
} // namespace sw
