/**
 * @file ResourceGauge.h
 * @brief 쓰고 다시 차는 게이지를 정의합니다.
 * @details 스태미나, 부스트, 데드아이는 쓰면 줄고, 총이나 부스터의 과열은 쓰면 오른다.
 *          젤다의 스태미나는 0 까지 쓰면 탈진해서 일정량 다시 찰 때까지 못 쓴다(스태미나 그릇).
 *          소울라이크의 스태미나는 쓴 뒤 잠깐 멈췄다가 찬다.
 *          캡슐파이터의 부스트, 위쳐와 리썰 컴퍼니의 달리기, 과열형 무기가 같은 계산을 쓴다.
 *          시간은 `update` 로만 흐르므로 결정적이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /**
     * @brief 게이지 설정입니다. 시간은 초입니다.
     * @details 범위는 필드의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `ResourceGauge::initialize` 가 오류로 거절한다(`ConfigRangeUtil`). 값을 조용히 맞추지 않는다(폴백 금지 원칙).
     *          탈진 문턱과 과열 복귀 수준이 `_max` 이하여야 한다는 관계도 `Max = "_max"` 로 선언한다.
     */
    REFLECT()
    struct SW_GF_API ResourceGaugeSettings
    {
        REFLECT_BODY();
        PROPERTY( Min = 0.001 )
        float32 _max{ 100.0f };
        PROPERTY( Min = 0.0 )
        float32 _regenRate{ 20.0f }; ///< 초당 회복(과열형이면 초당 식는 양)
        PROPERTY( Min = 0.0 )
        float32 _regenDelay{ 1.0f }; ///< 마지막으로 쓴 뒤 이만큼 지나야 차기(식기) 시작한다
        PROPERTY( Min = 0.0 )
        float32 _drainPerSecond{ 10.0f }; ///< 계속 쓰기(`drain( deltaTime )`)의 기본 초당 소비
        PROPERTY( Min = 0.0, Max = "_max" )
        float32 _exhaustThreshold{ 0.0f }; ///< 0 보다 크면 0 까지 쓴 순간 탈진하고, 이 값까지 다시 차야 다시 쓸 수 있다
        PROPERTY( Min = 0.0 )
        float32 _overheatCooldown{ 0.0f }; ///< 과열형에서 과열되면 이만큼 더 기다린 뒤 식기 시작한다(벌칙)
        PROPERTY( Min = 0.0, Max = "_max" )
        float32 _overheatRecoverLevel{ 0.0f }; ///< 과열형에서 열이 이 값까지 내려가야 잠금이 풀린다
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bOverheatMode{ SW_FALSE }; ///< 쓰면 오르고 최대에 닿으면 과열(캡슐파이터 부스트 · 총 과열)
    };
} // namespace sw

namespace sw
{
    /**
     * @class ResourceGauge
     * @brief 값 하나와 잠금 하나를 가진 게이지입니다. 보통형은 값이 남은 양(가득이 최대)이고, 과열형은 값이 열(0 이 식은 상태)입니다.
     * @details 보통형에서 `trySpend` 는 남은 양이 모자라면 쓰지 않는다.
     *          `drain` 은 남은 만큼만 쓰고 0 에 닿으면 false 를 반환한다. 이때 `_exhaustThreshold` 가 있으면 탈진하며,
     *          그 값까지 다시 찰 때까지 `trySpend` 와 `drain` 이 실패한다.
     *          과열형에서는 쓰면 열이 오르고, 최대를 넘는 `trySpend` 는 실패한다. 열이 최대에 닿으면 과열이며 `_overheatRecoverLevel` 까지 식을 때까지 잠긴다.
     *          쓰면 회복 지연이 처음부터 다시 센다. 바닥에 닿은 채 계속 쓰려 해도(`drain` 이 false 여도) 지연은 다시 센다.
     *          같은 프레임에 쓰고 `update` 하면 지연이 0 일 때만 그 프레임에 찬다.
     */
    class SW_GF_API ResourceGauge
    {
    public:
        /** @brief 기본 설정(`ResourceGaugeSettings{}`, 범위 안)으로 가득 찬 상태입니다. 다른 설정은 `initialize` 로 줍니다. */
        ResourceGauge();

        /**
         * @brief 설정을 두고 가득 찬(과열형은 식은) 상태로 시작합니다. 최대치 보너스는 0, 회복 배율은 1 로 둡니다.
         * @details 범위 밖 값(`_max` 를 넘는 탈진 문턱 · 과열 복귀 수준 포함)은 오류를 남기고 거절하며 false 를 반환한다. 거절하면 상태는 그대로이다. 값을 조용히 맞추지 않는다.
         */
        [[nodiscard]] bool initialize( const ResourceGaugeSettings& settings );
        /** @brief 지금 설정 그대로 처음 상태(가득 찬, 과열형은 식은)로 되돌립니다. 설정을 새로 받지 않으므로 실패하지 않습니다. */
        void reset();

        /** @brief 한 번에 @p amount 를 씁니다. 구르기, 강공격, 부스트 대시, 한 발에 씁니다. 잠겼거나 모자라면 쓰지 않고 false 를 반환합니다. */
        [[nodiscard]] bool trySpend( float32 amount );
        /**
         * @brief 계속 씁니다. 달리기, 활공, 부스트 유지에 씁니다. 이번 프레임에 @p rate × @p deltaTime 만큼 씁니다.
         * @return 다 쓸 수 있었으면 true 이다. 잠겼거나 이번에 바닥(과열)에 닿았으면 false 이며, 게임은 그 행동을 멈춘다.
         */
        bool drain( float32 rate, float32 deltaTime );
        /** @brief 설정의 `_drainPerSecond` 로 계속 씁니다. */
        bool drain( float32 deltaTime ) { return drain( _settings._drainPerSecond, deltaTime ); }
        /**
         * @brief 잠금과 상관없이 바로 깎습니다. 정신력 충격이나 맞아서 줄어드는 스태미나에 씁니다. 과열형이면 그만큼 열이 오릅니다.
         * @details 회복 지연이 처음부터 다시 센다. 보통형이 0 에 닿으면 `_exhaustThreshold` 에 따라 탈진하고, 과열형이 최대에 닿으면 과열이다.
         * @return 실제로 깎인(과열형은 오른) 양이다.
         */
        float32 reduce( float32 amount );
        /** @brief 시간을 흘립니다. 회복 지연, 회복(식기), 잠금 풀기가 진행됩니다. */
        void update( float32 deltaTime );
        /** @brief 바로 채웁니다. 물약과 보급에 씁니다. 과열형이면 그만큼 식히며, 잠금 풀기 조건도 다시 봅니다. */
        void restore( float32 amount );
        /** @brief 가득 채우고(과열형은 다 식히고) 잠금을 풉니다. */
        void refill();
        /**
         * @brief 회복(식기) 속도 배율입니다. 코어가 줄어 회복이 느려지거나 스킬 버프를 받을 때 씁니다. 회복 지연에는 적용하지 않으며, 음수는 0 으로 칩니다.
         */
        void    setRegenScale( float32 scale ) { _regenScale = scale > 0.0f ? scale : 0.0f; }
        float32 getRegenScale() const { return _regenScale; }
        /** @brief 최대치 보너스를 둡니다. 스태미나 그릇이나 업그레이드가 씁니다. 보통형은 늘어난 만큼 함께 차고, 줄면 값을 새 최대로 자릅니다. */
        void setMaxBonus( float32 bonus );

        const ResourceGaugeSettings& getSettings() const { return _settings; }
        float32                      getValue() const { return _value; }
        float32                      getMax() const { return _settings._max + _maxBonus; }
        float32                      getMaxBonus() const { return _maxBonus; }
        /** @brief 값을 최대로 나눈 비율입니다. 과열형은 열의 비율입니다. */
        float32 getRatio() const;
        /** @brief 보통형에서 바닥나 잠겼으면 true 입니다. */
        bool isExhausted() const { return _bLocked == SW_TRUE && _settings._bOverheatMode == SW_FALSE; }
        /** @brief 과열형에서 최대에 닿아 잠겼으면 true 입니다. */
        bool isOverheated() const { return _bLocked == SW_TRUE && _settings._bOverheatMode == SW_TRUE; }
        /** @brief 지금 쓸 수 있으면 true 입니다. 잠기지 않았고, 보통형이면 남은 양이 있어야 합니다. */
        bool canUse() const;

        /** @brief 값, 최대 보너스, 재생 배율, 마지막 사용 뒤 경과, 과열 벌칙, 잠김을 씁니다. 설정은 `initialize` 의 것이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 상태를 바꿉니다. 바이트가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 범위를 본 뒤의 설정으로 상태를 비웁니다(`initialize` 와 기본 생성자가 부른다). */
        void applySettings( const ResourceGaugeSettings& settings );
        void markUsed();
        void releaseLockIfRecovered();

        ResourceGaugeSettings _settings;
        float32               _value;
        float32               _maxBonus;
        float32               _regenScale;
        float32               _sinceUse; ///< 마지막으로 쓴 뒤 지난 시간
        Countdown             _overheatPenalty;
        uint8                 _bLocked; ///< 탈진 · 과열 잠금
    };
} // namespace sw
