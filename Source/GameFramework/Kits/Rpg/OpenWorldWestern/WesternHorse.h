/**
 * @file WesternHorse.h
 * @brief 말 한 마리 — 유대 단계(타기 · 손질 · 먹이 · 달래기 경험치) · 단계별 능력 해금 · 체력 · 스태미나 게이지(기반 `ResourceGauge`)와 코어 · 질주 · 겁먹음입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Combat/ResourceGauge.h"
#include "GameFramework/Base/Progression/LevelProgress.h"
#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/Base/Utility/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct WesternHorseDef;

    class Archive;
    class WesternCatalog;

    /** @brief 겁을 먹었을 때의 반응입니다. */
    enum class WesternHorseReaction : uint8
    {
        Calm = 0, ///< 버텼다
        Rearing,  ///< 뒷발로 섰다 — 잠깐 못 움직인다
        Bucked    ///< 탄 사람을 떨어뜨렸다
    };

    /** @brief 말에 생긴 일입니다. */
    struct WesternHorseEvent
    {
        enum class Kind : uint8
        {
            BondLevelUp = 0, ///< _value = 새 단계
            AbilityUnlocked, ///< _ability
            Exhausted,       ///< 질주하다 스태미나가 바닥났다
            Spooked,         ///< 뒷발로 섰다
            ThrewRider       ///< 탄 사람을 떨어뜨렸다
        };
        hashed_string _ability{};
        int32         _value{ 0 };
        Kind          _kind{ Kind::BondLevelUp };
    };
} // namespace sw

namespace sw
{
    /**
     * @class WesternHorse
     * @brief 코어는 0..100 입니다. 게이지 회복 속도는 코어에 비례합니다(코어 0 이면 생존 설정의 `_minRegenScale` 배). 코어는 게임 시간으로 줄고
     *        먹이로 찹니다. 겁은 쌓였다 식고, 문턱(1)을 넘으면 씨앗 난수로 뒷발 · 낙마를 정합니다 — 용기와 유대가 높을수록 덜 쌓이고 덜 떨어뜨립니다.
     */
    class SW_GF_API WesternHorse
    {
    public:
        static constexpr uint32  kStateTag      = FourCcUtil::make( "WHRS" );
        static constexpr uint32  kStateVersion  = 1;
        static constexpr float32 kCoreMax       = 100.0f;
        static constexpr float32 kFearThreshold = 1.0f;

        WesternHorse();

        /** @brief 품종으로 시작합니다. 품종이 없으면 false 입니다. */
        [[nodiscard]] bool initialize( const WesternCatalog* pCatalog, const hashed_string& horseId, uint32 seed );
        /**
         * @brief 시간을 흘립니다 — 게이지 회복(코어 배율) · 코어 감소 · 겁 식기 · 탄 시간 경험치.
         * @param deltaTime 실제 초 @param gameHours 이번에 흐른 게임 시간
         */
        void update( float32 deltaTime, float32 gameHours );
        void setRidden( bool bRidden ) { _bRidden = bRidden ? SW_TRUE : SW_FALSE; }
        /** @brief 질주합니다(이번 프레임). 스태미나가 바닥나면 false — 질주를 멈춘다. */
        bool gallop( float32 deltaTime );
        /** @brief 솔질합니다. 손질 대기 시간이 지났으면 경험치가 붙고 true 입니다. */
        bool brush();
        /** @brief 먹이를 줍니다 — 코어 · 게이지 회복과 유대 경험치. 모르는 먹이면 false 입니다. */
        bool feed( const hashed_string& foodId );
        /** @brief 겁을 줍니다(총소리 · 뱀 · 곰 — @p amount 1 이면 용기 없는 말이 바로 넘는다). */
        WesternHorseReaction frighten( float32 amount );
        /** @brief 달랩니다 — 겁을 지우고 유대 경험치를 조금. */
        void calm();
        void addBondExperience( float32 amount );
        void takeDamage( float32 amount ) { (void)_health.reduce( amount ); }

        /** @brief 지금 유대 단계(카탈로그의 `level`)입니다. */
        int32 getBondLevel() const;
        /** @brief 모은 유대 경험치(정수 경험치 + 아직 1 이 안 된 소수)입니다. */
        float32 getBondExperience() const { return static_cast<float32>( _bond.getTotalXp() ) + _bondXpCarry; }
        bool    hasAbility( const hashed_string& abilityId ) const;
        float32 getHealthCore() const { return _healthCore; }
        float32 getStaminaCore() const { return _staminaCore; }
        float32 getFear() const { return _fear; }
        /** @brief 코어에 따른 게이지 회복 배율입니다. */
        float32                computeRegenScale( float32 core ) const;
        const ResourceGauge&   getHealth() const { return _health; }
        const ResourceGauge&   getStamina() const { return _stamina; }
        const WesternHorseDef* getDef() const { return _pDef; }
        void                   drainEvents( vector<WesternHorseEvent>& outListEvent );
        /** @brief 품종 id · 열린 능력 · 체력 · 스태미나 게이지 · 난수 · 코어 · 유대(레벨 진행 + 소수) · 겁 · 손질 뒤 시간 · 탔는지 · 질주를 씁니다. 카탈로그는 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 품종이 카탈로그에 없거나 깨졌으면 false 이고 그대로입니다(품종이 다르면 그 품종의 게이지 설정으로 바꾼다). */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void    applyBondLevel( int32 level );
        float32 computeFearResist() const;

        EventBuffer<WesternHorseEvent> _eventBuffer;
        vector<hashed_string>          _listAbility; ///< 지금 단계까지 열린 능력
        ResourceGauge                  _health;
        ResourceGauge                  _stamina;
        GameRandom                     _random;
        const WesternCatalog*          _pCatalog;
        const WesternHorseDef*         _pDef;
        float32                        _healthCore;
        float32                        _staminaCore;
        LevelProgress                  _bond;        ///< 유대 — 레벨 L 은 카탈로그 단계 L − 1 번째(`WesternCatalog::getBondCurve`)
        float32                        _bondXpCarry; ///< 아직 1 이 안 된 유대 경험치(타기는 초당 소수로 쌓인다)
        float32                        _fear;
        float32                        _hoursSinceBrush;
        uint8                          _bRidden;
        uint8                          _bGalloping; ///< 이번 `update` 앞에 질주했다(코어가 더 준다)
    };
} // namespace sw
