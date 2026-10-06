/**
 * @file HorrorStealthGimmicks.h
 * @brief 공포 · 잠입 기믹 — 놀래기 트리거, 깜빡이는 빛(퀘이크 빛 스타일 문자열), 숨는 곳(스마트 오브젝트 자리 + 숨음 태그), 소리 내는 것, 빛 노출 질의입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Utility/FixedStepTimer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class ScareTriggerComponent
     * @brief 태그(보통 `Player`)를 가진 것이 처음 들어오면 연출 신호(`GimmickCueEvent` — `_cue`)와 소리를 냅니다. `_bOnce` 가 아니면 `_cooldown` 뒤에 다시 됩니다.
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
     * @brief 같은 오브젝트의 `LightComponent` 세기를 문자열 무늬로 깜빡입니다 — 퀘이크 빛 스타일('a' = 0, 'm' = 1, 'z' = 2배), `_rate` 글자/초.
     * @details 걸음 수로 글자를 고르므로 결정적입니다(같은 걸음 = 같은 밝기). 처음 세기를 기준으로 곱하고, 바뀔 때만 틱 뒤에 씁니다.
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
        /** @brief 걸음 @p step 의 밝기 배율입니다(무늬가 비면 1). */
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
     * @brief 숨는 곳(사물함 · 옷장 · 침대 밑) — 같은 오브젝트의 `SmartObjectComponent` 자리(Activity.Hide)를 차지하면 `State.Hidden` 태그를 붙입니다.
     *        AI 감각은 그 태그를 보고 보지 않습니다(`isHidden`). 상호작용(Hide)을 끝내면 들어가고 다시 끝내면 나옵니다(센서의 사용 알림).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Hiding Spot", Tooltip = "Claims a smart object slot and tags the occupant as hidden" )
    class SW_GF_API HidingSpotComponent : public Component
    {
    public:
        REFLECT_BODY();

        HidingSpotComponent();
        virtual ~HidingSpotComponent() override = default;

        void onTick( float32 deltaTime ) override;
        /** @brief 숨깁니다. 자리가 없으면 false 입니다. */
        [[nodiscard]] bool enter( GameObject& who );
        /** @brief 나옵니다. 숨어 있지 않았으면 false 입니다. */
        bool exit( GameObject& who );
        /** @brief 숨은 태그를 가졌는가입니다(AI 감각이 묻는다). */
        static bool isHidden( const GameObject& object );
    };
} // namespace sw

namespace sw
{
    /**
     * @class NoiseEmitterComponent
     * @brief 소리를 냅니다 — `emit` 로 직접, `_interval` 마다(기계), 같은 오브젝트의 센서에 새로 올라선 것이 있을 때(삐걱이는 마루). `GimmickNoiseEvent` 와
     *        마지막 반경(`getNoiseRadius` — `AiStimulus::_noiseRadius` 로 넘긴다)을 남깁니다.
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
     * @brief 잠입의 빛 노출 — 한 자리가 씬의 빛(점 · 스폿 · 방향)에 얼마나 드러나는가(0 = 어둠). 점 · 스폿은 반경 안 (1 − d/r)² × 세기(스폿은 원뿔 밖 0),
     *        방향광은 세기 그대로입니다. @p bOcclusion 이면 빛까지 광선이 막히면 빼고(방향광은 빛 반대쪽 100 m), 다른 경우는 가림을 보지 않습니다.
     */
    struct SW_GF_API LightExposure
    {
        static float32 computeExposure( const GameObjectManager& manager, const float3& position, bool bOcclusion, uint64 ignoreObjectId = 0 );
    };
} // namespace sw
