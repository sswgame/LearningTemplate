/**
 * @file MetroDuelist.h
 * @brief 2D 소울라이크의 맞붙기를 다룹니다. 스태미나(기반 `ResourceGauge`), 패리(기반 `TimingJudge` 창이며 성공하면 반격 배율), 막기(스태미나로 받고 모자라면 가드 붕괴),
 *        강인도(기반 `Vitality` 의 poise이며 깎이면 비틀거리고 그 상대에게는 반격 배율)를 관리합니다.
 * @details 시간은 `update` 로만 흐른다(결정적이다). 패리 판정의 목표 시각은 "공격이 닿는 순간", 곧 `receiveAttack` 을 호출한 시각이고, 누른 시각은 `pressParry` 를
 *          호출한 시각이다. 너무 일찍 누르면(창 밖) 패리가 아니라 그냥 맞는다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "GameFramework/Base/Actor/Combat/Health/ResourceGauge.h"
#include "GameFramework/Base/Actor/Combat/Health/Vitality.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class MetroidvaniaCatalog;

    /** @brief 공격을 받은 결과입니다. */
    enum class MetroDefenseResult : uint8
    {
        Hit = 0,     ///< 그냥 맞았다
        Blocked,     ///< 막았다(스태미나를 쓰고 체력은 조금 깎인다)
        GuardBroken, ///< 막았지만 스태미나가 모자라 가드가 깨졌다(그대로 맞는다)
        Parried,     ///< 패리에 성공했다(피해가 없고 반격 배율이 열린다)
        Ignored      ///< 이미 죽었다
    };

    /** @brief `receiveAttack` 한 번의 결과입니다. */
    struct MetroDefenseOutcome
    {
        hashed_string      _parryGrade{};         ///< 패리 창의 등급(Parried 일 때)
        float32            _healthDamage{ 0.0f }; ///< 실제로 깎인 체력
        MetroDefenseResult _result{ MetroDefenseResult::Hit };
        uint8              _bStaggered{ SW_FALSE }; ///< 이번에 강인도가 무너졌다
    };
} // namespace sw

namespace sw
{
    /**
     * @class MetroDuelist
     * @brief 한 전투원의 체력, 스태미나, 패리, 막기입니다. 적 하나도 같은 것을 씁니다(보스의 강인도가 붕괴하면 반격할 수 있습니다).
     */
    class SW_KIT_API MetroDuelist
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "MDUE" );
        static constexpr uint32 kStateVersion = 1;

        MetroDuelist();

        /**
         * @brief 카탈로그의 체력, 스태미나, 패리 규칙으로 가득 찬 상태에서 시작합니다.
         * @details 카탈로그가 없거나 체력 · 스태미나 규칙이 범위 밖이면 오류를 남기고 false 를 반환하며 상태는 그대로입니다(카탈로그를 읽을 때 이미 거절하므로 읽은 카탈로그는 닿지 않는다).
         */
        [[nodiscard]] bool initialize( const MetroidvaniaCatalog* pCatalog );
        /** @brief 시간을 흘립니다. 체력(강인도 회복), 스태미나, 반격 창이 갱신됩니다. */
        void update( float32 deltaTime );

        /** @brief 공격합니다. 스태미나가 모자라면 false 를 반환합니다(공격하지 못합니다). */
        [[nodiscard]] bool tryAttack();
        /** @brief 구르기와 회피를 합니다. 스태미나가 모자라면 false 를 반환합니다. */
        [[nodiscard]] bool tryDodge();
        /** @brief 패리 버튼을 누릅니다. 누른 시각을 기록해 두고 다음 공격 판정에서 한 번 씁니다. */
        void pressParry();
        void setGuarding( bool bGuarding ) { _bGuarding = bGuarding ? SW_TRUE : SW_FALSE; }
        /** @brief 받는 피해 배율입니다(부적 넘겨 끼기, 난이도). */
        void setDamageTakenScale( float32 scale ) { _damageTakenScale = scale > 0.0f ? scale : 1.0f; }

        /**
         * @brief 공격이 지금 닿았음을 알립니다. 패리 창, 막기, 맞기 순서로 판정합니다.
         * @param poiseDamage 강인도 피해(막으면 반만 들어가고 패리하면 없다)
         */
        MetroDefenseOutcome receiveAttack( float32 damage, float32 poiseDamage, int32 attackerID = -1 );
        /**
         * @brief 내가 @p target 을 칠 때의 피해를 계산합니다. 패리 직후(반격 창)이거나 상대 강인도가 무너졌으면 반격 배율을 곱하고, 반격 창은 그 한 번으로 닫힙니다.
         */
        float32 computeAttackDamage( float32 baseDamage, const MetroDuelist& target );

        bool                 isRiposteReady() const { return _riposteWindow.isActive(); }
        bool                 isGuarding() const { return _bGuarding == SW_TRUE; }
        float32              getTime() const { return _time; }
        const Vitality&      getVitality() const { return _vitality; }
        Vitality&            getVitality() { return _vitality; }
        const ResourceGauge& getStamina() const { return _stamina; }
        ResourceGauge&       getStamina() { return _stamina; }

        /** @brief 체력(`Vitality`), 지구력(`ResourceGauge`), 시각, 패리를 누른 시각, 반격 창, 받는 피해 배율, 막기를 씁니다. 카탈로그 규칙은 `initialize` 에서 받은 것이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다. 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        const MetroidvaniaCatalog* _pCatalog;
        Vitality                   _vitality;
        ResourceGauge              _stamina;
        float32                    _time;
        float32                    _parryPressTime; ///< 마지막으로 패리를 누른 시각(쓰고 나면 아주 먼 과거로 돌린다)
        Countdown                  _riposteWindow;
        float32                    _damageTakenScale;
        uint8                      _bGuarding;
    };
} // namespace sw
