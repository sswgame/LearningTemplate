/**
 * @file AdventureVitals.h
 * @brief 젤다의 몸 상태입니다. 하트(4분의 1 단위), 하트 조각(모으면 그릇 하나), 하트 그릇, 마법 게이지(두 배 마법), 스태미나 그릇을 다룹니다.
 *        오르기, 질주, 활공, 수영은 스태미나를 쓰고, 스태미나가 바닥나면 각각 떨어지거나 멈추거나 물에 빠집니다.
 * @details 하트는 기반 `Vitality`(체력 = 하트 × 4)가, 마법과 스태미나는 기반 `ResourceGauge` 가 맡는다. 스태미나를 0까지 쓰면 탈진 상태가 되어 `_staminaRecoverLevel` 까지 다시 찰 때까지 어떤 행동도 스태미나를 쓰지 못한다(야생의 숨결의 붉은 바퀴).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/Base/Actor/Combat/Health/ResourceGauge.h"
#include "GameFramework/Base/Actor/Combat/Health/Vitality.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 몸의 수치입니다. 하트는 개수로, 피해와 회복은 4분의 1 하트 단위로 셉니다. */
    struct AdventureVitalsSettings
    {
        int32   _startHeartCount{ 3 };
        int32   _maxHeartCount{ 20 };
        int32   _piecesPerHeart{ 4 }; ///< 하트 조각이 이만큼 모이면 하트 그릇 하나가 된다
        float32 _magicMax{ 48.0f };
        float32 _magicUpgrade{ 48.0f }; ///< 두 배 마법이 더하는 양
        float32 _staminaMax{ 100.0f };  ///< 처음 스태미나 바퀴 하나의 양
        float32 _staminaVessel{ 20.0f };
        float32 _staminaVesselMax{ 200.0f }; ///< 그릇으로 늘릴 수 있는 보너스의 최대값
        float32 _staminaRegenRate{ 40.0f };
        float32 _staminaRegenDelay{ 0.5f };
        float32 _staminaRecoverLevel{ 100.0f }; ///< 탈진한 뒤 이만큼 다시 차야 스태미나를 쓸 수 있다
        float32 _climbDrain{ 10.0f };           ///< 초당 소모량
        float32 _sprintDrain{ 15.0f };
        float32 _glideDrain{ 6.0f };
        float32 _swimDrain{ 8.0f };
        int32   _drownDamage{ 4 }; ///< 수영 중 스태미나가 바닥나 빠졌을 때 받는 피해(4분의 1 하트)
    };
} // namespace sw

namespace sw
{
    class Archive;

    /** @brief 지금 스태미나를 쓰는 행동입니다. */
    enum class AdventureStaminaAction : uint8
    {
        Idle = 0, ///< 아무것도 쓰지 않는다. 스태미나가 다시 찬다
        Climb,
        Sprint,
        Glide,
        Swim
    };

    /** @brief 스태미나 한 틱의 결과입니다. 게임이 그 행동을 어떻게 끝낼지 알려 줍니다. */
    enum class AdventureStaminaOutcome : uint8
    {
        Continue = 0,
        Stop, ///< 질주가 걷기로 바뀐다
        Fall, ///< 오르기나 활공 중 바닥나서 떨어진다
        Drown ///< 수영 중 바닥나서 피해를 입고 물가로 돌아간다(게임이 옮긴다)
    };

    SW_KIT_API const utf8* toString( AdventureStaminaOutcome outcome );

    /**
     * @class AdventureVitals
     * @brief 플레이어 한 명의 하트, 마법, 스태미나입니다. 시간은 `updateStamina` 로만 흐릅니다(결정적).
     * @details 하트 그릇이 늘면 하트가 가득 찬다(젤다의 그릇 획득). 그 밖의 회복은 `heal` 로 한다.
     */
    class SW_KIT_API AdventureVitals
    {
    public:
        static constexpr uint32 kStateTag         = FourCcUtil::make( "AVIT" );
        static constexpr uint32 kStateVersion     = 1;
        static constexpr int32  kQuartersPerHeart = 4;

        AdventureVitals();

        void initialize( const AdventureVitalsSettings& settings );

        /** @brief 4분의 1 하트 단위로 피해를 줍니다. 이번에 죽었으면 true 를 반환합니다. */
        [[nodiscard]] bool applyDamage( int32 quarters );
        /** @brief 4분의 1 하트 단위로 회복하고, 실제로 찬 양을 반환합니다. */
        int32 heal( int32 quarters );
        /** @brief 하트 조각 하나를 얻습니다. 이번에 그릇 하나가 완성되었으면 true 를 반환합니다. 이미 최대면 조각이 쌓이지 않으므로 false 입니다. */
        bool addHeartPiece();
        /** @brief 하트 그릇 하나를 얻고 체력을 가득 채웁니다. 이미 최대면 false 를 반환합니다. */
        bool addHeartContainer();

        [[nodiscard]] bool trySpendMagic( float32 amount ) { return _magic.trySpend( amount ); }
        void               restoreMagic( float32 amount ) { _magic.restore( amount ); }
        /** @brief 두 배 마법을 받습니다. 이미 받았으면 false 를 반환합니다. */
        bool upgradeMagic();

        /** @brief 이번 틱의 스태미나 행동을 처리합니다. 스태미나가 바닥나면 행동마다 정해진 결과를 반환하며, 수영이면 피해까지 줍니다. */
        AdventureStaminaOutcome updateStamina( AdventureStaminaAction action, float32 deltaTime );
        /** @brief 스태미나를 한 번에 씁니다(오르기 점프, 회전 베기 모으기). 탈진했거나 모자라면 false 를 반환합니다. */
        [[nodiscard]] bool trySpendStamina( float32 amount ) { return _stamina.trySpend( amount ); }
        /** @brief 스태미나 그릇 하나를 얻습니다. 이미 최대면 false 를 반환합니다. */
        bool addStaminaVessel();

        const AdventureVitalsSettings& getSettings() const { return _settings; }
        int32                          getHeartCount() const { return _heartCount; }
        int32                          getHeartPieceCount() const { return _heartPieceCount; }
        /** @brief 남은 체력(4분의 1 하트 단위)입니다. */
        int32                computeHealthQuarters() const;
        int32                getMaxHealthQuarters() const { return _heartCount * kQuartersPerHeart; }
        bool                 isDead() const { return _health.isDead(); }
        const Vitality&      getHealth() const { return _health; }
        const ResourceGauge& getMagic() const { return _magic; }
        const ResourceGauge& getStamina() const { return _stamina; }
        bool                 isMagicUpgraded() const { return _bMagicUpgraded == SW_TRUE; }

        /** @brief 하트 수, 조각 수, 두 배 마법, 체력(`Vitality`), 마력과 지구력(`ResourceGauge`)을 씁니다. 설정은 `initialize` 에서 받은 것이라 싣지 않습니다. 체력 최대값은 하트 수로 다시 계산합니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다. 하트 수가 설정 범위 밖이거나 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 하트 수에 맞춰 체력 최대값을 다시 설정하고 체력을 가득 채웁니다. */
        void resetHealth();

        AdventureVitalsSettings _settings;
        Vitality                _health;
        ResourceGauge           _magic;
        ResourceGauge           _stamina;
        int32                   _heartCount;
        int32                   _heartPieceCount;
        uint8                   _bMagicUpgraded;
    };
} // namespace sw
