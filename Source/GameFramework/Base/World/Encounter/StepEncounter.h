/**
 * @file StepEncounter.h
 * @brief 걸음 조우 판정 하나입니다. 오버월드(결정적 주기)와 클래식 JRPG(걸음마다 확률 + 조우 뒤 유예)가 이 규칙과 셈 하나를 함께 씁니다.
 * @details 예전에는 Overworld 의 `shouldEncounterOnStep`(주기)과 ClassicJRPG 의 `JRPGEncounterWalker`(확률 + 유예)가 따로 걸음을 세고 판정했다.
 *          몬스터 수집 게임처럼 둘 위에 서는 게임이 두 셈을 맞추지 않도록, 규칙(`StepEncounterRule`)은 데이터로 방식을 고르고 셈(`StepEncounterCounter`)은 하나다.
 *          - `Period`: 걸음 수가 `1/rate`(내림)의 배수인 걸음마다 조우한다. 난수를 쓰지 않아 기록 재생 · 테스트에서 같은 걸음에 난다.
 *          - `Chance`: 걸음마다 `rate` 확률로 굴린다. 난수는 부르는 쪽 것을 받는다(세이브가 그 난수를 싣는다).
 *          - 두 방식 모두 rate 0 이하는 끄는 값, 1 이상은 매 걸음이다. 조우 뒤 `_graceSteps` 걸음 동안은 굴리지 않는다(난수도 쓰지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameRandom;

    /** @brief 걸음 조우를 정하는 방식입니다. */
    enum class StepEncounterMode : uint8
    {
        Period = 0, ///< 결정적 주기(`1/rate` 걸음마다)
        Chance,     ///< 걸음마다 확률
    };
} // namespace sw

namespace sw
{
    /** @brief 조우 규칙 한 벌입니다. 값은 게임 데이터(지역 표 · 이동 설정)가 정합니다. */
    struct StepEncounterRule
    {
        StepEncounterMode _mode{ StepEncounterMode::Chance };
        float32           _rate{ 0.0f };    ///< 0 이하는 끄고, 1 이상은 매 걸음이다
        int32             _graceSteps{ 0 }; ///< 조우한 뒤 이 걸음 수까지는 굴리지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class StepEncounterCounter
     * @brief 걸음 수와 조우 뒤 걸음 수를 세고 규칙으로 이번 걸음의 조우를 정합니다. 시드 · 걸음이 같으면 같은 걸음에서 조우합니다.
     * @details 셈은 부르는 쪽이 걸음으로 치는 것만 `advance` 한다(오버월드는 조우 타일의 걸음만, JRPG 는 모든 걸음). 세이브 바이트는 소유자가 쓴다 — 값만 내고 받는다.
     */
    class SW_GF_API StepEncounterCounter
    {
    public:
        /** @brief 아직 조우한 적이 없음을 뜻하는 조우 뒤 걸음 수입니다(유예에 걸리지 않는다). */
        static constexpr int32 kNoEncounterYet = 0x3fffffff;

        StepEncounterCounter() = default;

        /** @brief 한 걸음을 셉니다. */
        void advance();
        /**
         * @brief 이번 걸음(`advance` 뒤)에 조우하는지 정합니다. 셈은 바꾸지 않습니다 — 조우를 받아들이면 `markEncounter` 를 부릅니다.
         * @param pRandom `Chance` 이면 필수(없으면 단언), `Period` 이면 쓰지 않습니다.
         */
        [[nodiscard]] bool roll( const StepEncounterRule& rule, GameRandom* pRandom ) const;
        /** @brief 조우했음을 적습니다. 유예가 여기서 시작합니다. */
        void markEncounter() { _stepsSinceEncounter = 0; }
        /** @brief 유예를 처음부터 다시 셉니다(마을에서 나왔을 때, 성수를 썼을 때). */
        void restartGrace() { _stepsSinceEncounter = 0; }
        /** @brief 처음 상태(조우 없음, 0 걸음)로 돌립니다. */
        void reset();

        int32 getStepsSinceEncounter() const { return _stepsSinceEncounter; }
        int32 getTotalSteps() const { return _totalSteps; }
        /** @brief 저장된 값으로 셈을 바꿉니다. 범위 밖이면 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool restore( int32 stepsSinceEncounter, int32 totalSteps );

        /** @brief `Period` 의 판정입니다. @p stepCount 는 1 부터 센 걸음입니다. 0 이하는 언제나 false, 1 이상은 언제나 true 입니다. */
        static bool isPeriodicEncounter( float32 rate, uint32 stepCount );

    private:
        int32 _stepsSinceEncounter{ kNoEncounterYet };
        int32 _totalSteps{ 0 };
    };
} // namespace sw
