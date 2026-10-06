/**
 * @file WitcherCombat.h
 * @brief 위쳐 전투 — 빠른 · 강한 공격 · 회피 · 구르기의 스태미나(기반 `ResourceGauge`), 표식(기력 소모 · 표식 위력 능력치 · 기반 `SkillTreeState` 로 대체 시전 해금 ·
 *        기반 `ElementChart` 상태이상), 아드레날린 포인트(적중으로 쌓이고 피격으로 준다)입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Combat/ResourceGauge.h"
#include "GameFramework/Base/Utility/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class ElementChart;
    class SkillTreeState;
    class StatBlock;
    class WitcherCatalog;

    /** @brief 스태미나를 쓰는 몸 동작입니다. */
    enum class WitcherAction : uint8
    {
        FastAttack = 0,
        StrongAttack,
        Dodge,
        Roll
    };

    /** @brief 동작 · 표식 결과입니다. */
    enum class WitcherCombatResult : uint8
    {
        Ok = 0,
        UnknownSign,
        NotEnoughStamina,
        AlternateLocked ///< 대체 시전 스킬을 배우지 않았다
    };

    SW_GF_API const utf8* toString( WitcherCombatResult result );

    /** @brief 표식 한 번의 결과입니다. */
    struct WitcherSignCast
    {
        hashed_string       _signId{};
        hashed_string       _element{};
        hashed_string       _status{}; ///< 걸린 상태이상(없으면 빈 이름)
        float32             _power{ 0.0f };
        uint8               _bAlternate{ SW_FALSE };
        WitcherCombatResult _result{ WitcherCombatResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @class WitcherCombat
     * @brief 위쳐 한 명의 전투 자원입니다. 표식과 몸 동작이 같은 스태미나(기력)를 씁니다. 표식 위력 = 기본 위력 × (1 + 위력 능력치 / 100)
     *        (× 대체 시전 배율). 아드레날린은 실수로 쌓이고 정수 부분이 포인트입니다 — 포인트 하나마다 피해 배율이 오릅니다. 씨앗이 같으면 같은 상태이상입니다.
     */
    class SW_GF_API WitcherCombat
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "WCMB" );
        static constexpr uint32 kStateVersion = 1;

        WitcherCombat();

        void initialize( const WitcherCatalog* pCatalog, uint32 seed );
        void update( float32 deltaTime ) { _stamina.update( deltaTime ); }
        /** @brief 몸 동작을 합니다. 스태미나가 모자라면 하지 않습니다(빠른 공격은 보통 공짜). */
        WitcherCombatResult performAction( WitcherAction action );
        /**
         * @brief 표식을 겁니다.
         * @param pSkill 대체 시전 해금을 볼 스킬 트리(없으면 대체 시전 불가) @param stats 위력 능력치를 읽는 곳 @param pChart 상태이상을 굴릴 표(없으면 굴리지 않음)
         */
        WitcherSignCast castSign( const hashed_string& signId, bool bAlternate, const SkillTreeState* pSkill, const StatBlock& stats, const ElementChart* pChart );
        /** @brief 공격이 맞았습니다 — 아드레날린이 쌓입니다. */
        void registerHitLanded();
        /** @brief 맞았습니다 — 아드레날린이 줍니다. */
        void registerHitTaken();
        /** @brief 아드레날린 포인트를 모두 씁니다(아드레날린 기술). 쓴 포인트입니다. */
        int32 consumeAdrenalinePoints();

        /** @brief 아드레날린 포인트에 따른 피해 배율입니다. */
        float32              computeDamageScale() const;
        float32              getAdrenaline() const { return _adrenaline; }
        int32                getAdrenalinePoints() const;
        float32              getActionCost( WitcherAction action ) const;
        const ResourceGauge& getStamina() const { return _stamina; }
        ResourceGauge&       getStamina() { return _stamina; }
        /** @brief 스태미나 게이지 · 난수 · 아드레날린을 씁니다. 카탈로그는 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        ResourceGauge         _stamina;
        GameRandom            _random;
        const WitcherCatalog* _pCatalog;
        float32               _adrenaline;
    };
} // namespace sw
