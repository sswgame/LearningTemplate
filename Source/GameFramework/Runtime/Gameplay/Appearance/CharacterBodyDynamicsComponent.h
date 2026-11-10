/**
 * @file CharacterBodyDynamicsComponent.h
 * @brief 캐릭터 몸 동작입니다. 외형이 고른 몸 프로필(`bodyprofiles.xml`)을 실행 중인 몸에 겁니다 — 굽 높이(발 디딤 IK 입력), 지글(물성 · 동작 배율 · 맞음 충격), 주름 매핑,
 *        눈동자 순응(눈에 닿는 실제 빛), 체액(눈물 · 침 자국과 화장 번짐).
 * @details 같은 오브젝트의 `CharacterAppearanceComponent`(해석 결과 · 표면 상태 · 발 들림), `PoseModifierComponent`(리그 몸 입력), `FacialAnimationComponent`(주름),
 *          `SkeletalAnimatorComponent`(동공 감정 · 체액 방아쇠 커브)를 읽고 쓴다. 해석이 바뀌면(해시) 프로필을 다시 건다.
 *          - **빛**: 장면의 방향광(시간대 · 날씨가 `DaylightComponent` 로 세기를 정한다)이 눈에서 해 쪽으로 쏜 광선에 막히지 않으면 세기만큼, 막히면(실내 · 그늘) 방향광의 주변광만큼,
 *            점광은 거리 제곱으로 준다. 렌더러의 노출 측정을 GPU 에서 읽어 오지 않는다 — 게임 스레드가 프레임마다 쓰는 값이 같은 원천(해 세기 · 주변광)이라 늦지 않는다.
 *          - **물리 질의 · 표면 쓰기**는 틱 뒤 게임 스레드에서 한다(`SurfaceExposureComponent` 와 같은 길).
 *          언리얼에서는 캐릭터 블루프린트 · 애님 BP · 머티리얼 파라미터 콜렉션이 나눠 하는 일을, 데이터 프로필 하나와 이 컴포넌트로 모았다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Appearance/SkinLayer.h"

namespace sw
{
    class CharacterAppearanceComponent;
    class GameObjectManager;
    class VFXComponent;
    struct CharacterBodyProfileDef;
    struct BodyFluidDef;

    /** @class CharacterBodyDynamicsComponent @brief 파일 머리말 참고. 외형 컴포넌트와 같은 오브젝트에 둡니다. */
    REFLECT( Category = "Character", DisplayName = "Character Body Dynamics", Tooltip = "Applies the appearance body profile: heel foot lift, jiggle, wrinkles, pupil adaptation, tears and smearing" )
    class SW_GF_API CharacterBodyDynamicsComponent : public Component
    {
    public:
        REFLECT_BODY();

        CharacterBodyDynamicsComponent();
        virtual ~CharacterBodyDynamicsComponent() override = default;

        void onTick( float32 deltaTime ) override;
        /** @brief 맞음(전투 맞음 이벤트)이 지글에 충격을 줍니다 — 때린 방향 × 충격량 × `_hitImpulseScale`. */
        void onHitReceived( const HitInfo& hit ) override;
        /** @brief 알림 이벤트 `_wipeEvent`(손으로 닦기)가 화장 층을 아래로 끕니다. */
        void onAnimationNotify( const AnimationNotifyInfo& notify ) override;

        /** @brief 지글 충격(월드 속도 m/s)을 더합니다(착지 · 게임 코드). 다음 리그 준비 단계에 들어간다. */
        void addImpulse( const float3& worldVelocity );
        /** @brief 체액 방아쇠를 @p seconds 동안 @p strength 로 켭니다(편집 창 미리보기의 울기 · 침 단추, 시나리오). 애니메이터 커브와 큰 쪽을 쓴다. */
        void triggerFluid( const hashed_string& trigger, float32 seconds, float32 strength );
        /** @brief 씻기 — 화장 번짐을 지운다(체액 자국은 표면 채널의 마르기가 지운다). */
        void clearSmears();

        float32 getFootLift() const { return _footLift; }
        float32 getPupilSize() const { return _pupilSize; }
        float32 getEyeLuminance() const { return _eyeLuminance; }
        float32 getMotionScale() const { return _motionScale; }
        /** @brief 지글 노드 끝 입자가 애니메이션 자세에서 벗어난 가장 큰 거리(m)입니다(감시 값). */
        float32 computeJiggleDisplacement() const;
        /** @brief 체액 채널 마스크의 가장 큰 값입니다(감시 값 — 울면 오르고 마르면 내린다). */
        float32 computeFluidWetness() const;
        /** @brief 피부 층 번짐의 가장 큰 묽음(0..1)입니다(감시 값 — 눈물 길 아래 마스카라가 묽어진다). */
        float32 computeMaxSmearDilution() const;
        /** @brief 지금 방울 · 가닥을 내는 체액 효과 수입니다(감시 값 — 눈물이 턱에 닿으면 방울, 침 방아쇠가 켜진 동안 가닥). */
        float32 computeActiveFluidEffects() const;
        /** @brief 해석 순서의 피부 층 번짐 상태입니다(굽기가 읽는다). */
        const vector<SkinLayerSmearState>& getSmearStates() const { return _listSmear; }

    private:
        /** @brief 체액 방출 하나의 실행 상태입니다. */
        struct FluidStream
        {
            vector<float32>                _listDistance{}; ///< 흐르는 줄기마다 경로 위 거리(UV)
            hashed_string                  _name{};
            TComponentHandle<VFXComponent> _effect{};                ///< 방울 · 가닥 효과(이 오브젝트에 붙인 것 — 처음 켤 때 만든다)
            float32                        _emitAccumulator{ 0.0f }; ///< 새 줄기까지 쌓인 몫
            float32                        _dripRemaining{ 0.0f };   ///< 방울이 더 떨어질 시간(초)
            uint8                          _bEffectOn{ SW_FALSE };   ///< 효과의 방출기를 켜 두었다
        };

        /** @brief 수동 방아쇠 하나입니다(`triggerFluid`). */
        struct ManualTrigger
        {
            hashed_string _trigger{};
            float32       _remainingSeconds{ 0.0f };
            float32       _strength{ 0.0f };
        };

        /** @brief 쌓인 게임 초만큼 몸 동작을 진행합니다(틱 뒤 게임 스레드). */
        void update( GameObjectManager& manager );
        /** @brief 해석이 바뀌었으면 프로필(지글 물성 · 주름 · 번짐 칸 수)을 다시 겁니다. */
        void applyProfile( CharacterAppearanceComponent& appearance, const CharacterBodyProfileDef* pProfile, uint64 resolvedHash );
        /** @brief 눈 위치의 빛 세기를 잽니다(방향광 + 가림 광선 + 점광). */
        float32 measureEyeLuminance( GameObjectManager& manager, const float3& eye ) const;
        /** @brief 동공 크기를 순응시켜 몸 머티리얼에 겁니다. */
        void updatePupil( GameObjectManager& manager, CharacterAppearanceComponent& appearance, const CharacterBodyProfileDef& profile, float32 seconds );
        /** @brief 체액 줄기를 내고 흘려 표면 채널과 화장 번짐에 찍습니다. */
        void updateFluids( CharacterAppearanceComponent& appearance, const CharacterBodyProfileDef& profile, float32 seconds );
        /**
         * @brief 체액 효과(방울 · 가닥)를 켜고 끄고 소켓에 둡니다. 효과 컴포넌트는 처음 켤 때 이 오브젝트에 붙인다(꺼도 남아 떨어지던 방울은 끝까지 떨어진다).
         * @param amount 이번 방아쇠 세기입니다(가닥). @param arrivedCount 이번에 경로 끝(턱)에 닿은 줄기 수입니다(방울).
         */
        void updateFluidEffect( CharacterAppearanceComponent& appearance, const BodyFluidDef& def, FluidStream& stream, float32 amount, uint32 arrivedCount, float32 seconds );
        /** @brief 붙인 체액 효과를 모두 뗍니다(프로필이 바뀌었다). */
        void removeFluidEffects();
        /** @brief 방아쇠 @p trigger 의 이번 세기입니다(애니메이터 커브와 수동 방아쇠 중 큰 쪽). */
        float32 computeTriggerStrength( const hashed_string& trigger ) const;

        PROPERTY( Category = "Eyes", DisplayName = "Eye Socket", Tooltip = "Socket whose world position is where the eye light is measured" )
        hashed_string _eyeSocket;
        PROPERTY( Category = "Jiggle", DisplayName = "Hit Impulse Scale", Min = 0.0, Tooltip = "Jiggle velocity (m/s) per newton-second of hit impulse" )
        float32 _hitImpulseScale;
        PROPERTY( Category = "Eyes", DisplayName = "Sun Trace Distance", Min = 0.0, Units = m, Tooltip = "Ray length from the eye towards the sun; a hit means shade or an interior" )
        float32 _sunTraceDistance;
        PROPERTY( Category = "Eyes", DisplayName = "Sky Light Value", Tooltip = "Weather value (WeatherCatalog <Values>) that scales direct sunlight: 1 clear, lower for overcast, rain, fog. Empty = the sun light already carries the weather (DaylightComponent)" )
        hashed_string _skyLightValue;
        PROPERTY( Required, Category = "Smear", DisplayName = "Wipe Event", Tooltip = "Animation notify event that wipes the face (drags makeup sideways)" )
        hashed_string _wipeEvent;
        PROPERTY( Category = "Smear", DisplayName = "Tear Smear Strength", Min = 0.0, Tooltip = "Pigment transport per wet stamp of a fluid stream" )
        float32 _fluidSmearStrength;

        vector<FluidStream>         _listFluid;
        vector<ManualTrigger>       _listManualTrigger;
        vector<SkinLayerSmearState> _listSmear; ///< 해석의 피부 층 순서
        vector<float32>             _listSmearResistance;
        float3                      _pendingImpulse;
        uint64                      _appliedHash;
        float32                     _pendingSeconds;
        float32                     _footLift;
        float32                     _pupilSize;
        float32                     _eyeLuminance;
        float32                     _motionScale;
        float32                     _pendingWipe;
        uint8                       _bJiggleApplied;
    };
} // namespace sw
