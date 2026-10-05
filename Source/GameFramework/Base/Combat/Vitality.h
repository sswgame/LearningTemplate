/**
 * @file Vitality.h
 * @brief 체력 상태 기계 — 실드(먼저 깎인다) · 체력 · 재생 · 기절(출혈 · 부활) · 죽음 · 무적 시간 · 경직 게이지입니다.
 * @details 배틀로얄(실드 · 기절 · 동료 부활), 데드 바이 데이라이트(빈사 · 치료 · 갈고리 단계 = 최대 기절 횟수), 캡슐파이터(다운치),
 *          소울라이크(강인도 붕괴), 리썰 컴퍼니 · 위쳐 · 젤다(체력 · 재생)가 같은 상태 기계를 씁니다. 엔진 컴포넌트가 아닌 보통 클래스라
 *          씬 없이 돌고, 시간은 `update` 로만 흐릅니다(결정적).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Utility/Countdown.h"
#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /** @brief 체력 설정입니다. 시간은 초, 비율은 0..1 입니다. 0 인 재생 속도는 "재생 없음" 입니다. */
    struct VitalitySettings
    {
        float32 _maxHealth{ 100.0f };
        float32 _maxShield{ 0.0f };         ///< 체력보다 먼저 깎이는 실드 · 방탄(0 = 없음)
        float32 _shieldRegenDelay{ 3.0f };  ///< 마지막 피해 뒤 이만큼 지나야 실드가 찬다
        float32 _shieldRegenRate{ 0.0f };   ///< 초당 실드 회복
        float32 _healthRegenDelay{ 5.0f };  ///< 마지막 피해 뒤 이만큼 지나야 체력이 찬다
        float32 _healthRegenRate{ 0.0f };   ///< 초당 체력 회복
        float32 _downedHealth{ 100.0f };    ///< 기절 상태의 출혈 체력(이것이 0 이 되면 죽는다)
        float32 _bleedoutRate{ 5.0f };      ///< 기절 중 초당 줄어드는 출혈 체력(부활 받는 동안은 멈춘다)
        float32 _reviveTime{ 5.0f };        ///< 배율 1 로 살리는 데 걸리는 시간
        float32 _reviveHealthRatio{ 0.3f }; ///< 살아난 뒤 체력 = 최대 × 이 비율
        float32 _invulnerableAfterRevive{ 1.0f };
        float32 _poiseMax{ 0.0f };                   ///< 경직 · 다운 게이지(0 = 경직 없음). 0 까지 깎이면 경직 붕괴
        float32 _poiseRegenDelay{ 2.0f };            ///< 마지막 경직 피해 뒤 이만큼 지나야 찬다
        float32 _poiseRegenRate{ 0.0f };             ///< 초당 경직 게이지 회복
        float32 _poiseBreakDuration{ 1.0f };         ///< 붕괴(비틀거림 · 다운) 시간 — 끝나면 게이지가 가득 찬다
        int32   _maxDownCount{ 0 };                  ///< 이 횟수만큼 기절한 뒤 치명타는 바로 죽음(0 = 제한 없음)
        uint8   _bDownedEnabled{ SW_FALSE };         ///< 치명타에 바로 죽지 않고 기절(배틀로얄 기절 · DBD 빈사)
        uint8   _bDamageInterruptsRevive{ SW_TRUE }; ///< 부활 받는 중에 맞으면 부활이 끊긴다
        uint8   _bKeepReviveProgress{ SW_FALSE };    ///< 끊겨도 진행을 남긴다(DBD 치료) — 아니면 처음부터(배틀로얄)
    };
} // namespace sw

namespace sw
{
    /** @brief 체력 상태입니다. */
    enum class VitalityState : uint8
    {
        Alive = 0,
        Downed, ///< 기절 · 빈사 — 출혈 체력이 줄고 부활을 기다린다
        Dead
    };

    /** @brief 체력 알림의 종류입니다. */
    enum class VitalityEventType : uint8
    {
        Damaged = 0,       ///< `_amount` = 실드 + 체력(또는 출혈 체력) 피해
        ShieldBroken,      ///< 실드가 0 이 됐다
        Healed,            ///< `_amount` = 실제로 찬 체력
        Downed,            ///< 기절했다
        ReviveStarted,     ///< `_instigatorId` = 살리는 쪽
        ReviveInterrupted, ///< 맞아서 부활이 끊겼다(`stopRevive` 로 그만둔 것은 알리지 않는다)
        Revived,           ///< `_instigatorId` = 살린 쪽
        Died,              ///< `_instigatorId` = 마지막으로 때린 쪽(출혈사면 기절시킨 쪽)
        PoiseBroken,       ///< 경직 붕괴 — 게임이 비틀거림 · 다운 애니메이션을 튼다
        PoiseRecovered     ///< 붕괴가 끝나 게이지가 가득 찼다
    };

    /** @brief 체력 알림 하나입니다. */
    struct VitalityEvent
    {
        float32           _amount{ 0.0f };
        int32             _instigatorId{ -1 };
        VitalityEventType _type{ VitalityEventType::Damaged };
    };
} // namespace sw

namespace sw
{
    /** @brief `applyDamage` 한 번의 결과입니다. */
    struct VitalityDamageResult
    {
        float32 _shieldAbsorbed{ 0.0f }; ///< 실드가 받은 양
        float32 _healthDamage{ 0.0f };   ///< 체력(기절 중이면 출혈 체력)이 받은 양
        uint8   _bIgnored{ SW_FALSE };   ///< 무적 · 죽음이라 아무 일도 없었다
        uint8   _bShieldBroken{ SW_FALSE };
        uint8   _bDowned{ SW_FALSE };
        uint8   _bDied{ SW_FALSE };
        uint8   _bPoiseBroken{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class Vitality
     * @brief 한 개체의 체력입니다. 피해는 실드 → 체력 순서로 깎이고, 체력이 0 이 되면 설정에 따라 기절하거나 죽습니다.
     * @details 넘친 피해는 실드에서 체력으로 넘어가지만, 체력에서 출혈 체력으로는 넘어가지 않습니다(기절 순간의 한 방이 바로 죽이지 않게).
     *          기절 중에는 실드 · 재생 · 경직이 멈추고 `heal` 도 듣지 않습니다 — 되살리는 길은 부활(`startRevive`) 하나입니다.
     *          부활은 여럿이 함께 살릴 수 있게 속도 배율을 받습니다(`startRevive` 를 다시 부르면 배율만 바뀐다).
     */
    class SW_GF_API Vitality
    {
    public:
        Vitality();
        explicit Vitality( const VitalitySettings& settings );

        /** @brief 설정을 두고 가득 찬 상태로 시작합니다(죽음 · 기절 횟수 · 알림도 비운다). */
        void initialize( const VitalitySettings& settings );
        /** @brief 같은 설정으로 가득 찬 상태로 되살립니다(리스폰). 기절 횟수는 남깁니다 — 지우려면 `resetDownCount`. */
        void respawn();
        /**
         * @brief 최대 체력을 바꿉니다(하트 그릇 · 레벨 업 · 장비). 상태 · 알림 · 기절 횟수는 그대로입니다.
         * @param bFill 살아 있으면 새 최대까지 채운다(젤다의 하트 그릇). 아니면 지금 체력을 새 최대로 자르기만 한다.
         */
        void setMaxHealth( float32 maxHealth, bool bFill );

        /**
         * @brief 피해를 줍니다. @p amount 는 실드 → 체력 순서, @p poiseDamage 는 경직 게이지에 갑니다.
         * @param instigatorId 때린 쪽(게임이 정한 번호 — 알림과 죽음 귀속에 실린다)
         */
        VitalityDamageResult applyDamage( float32 amount, float32 poiseDamage = 0.0f, int32 instigatorId = -1 );
        /** @brief 체력을 채웁니다(살아 있을 때만, 최대까지). 실제로 찬 양입니다. */
        float32 heal( float32 amount );
        /** @brief 실드를 채웁니다(살아 있을 때만, 최대까지 — 실드 전지). 실제로 찬 양입니다. */
        float32 addShield( float32 amount );
        /** @brief 시간을 흘립니다 — 무적 시간 · 실드 · 체력 · 경직 재생, 기절 중이면 출혈과 부활 진행. */
        void update( float32 deltaTime );

        /**
         * @brief 기절한 개체를 살리기 시작합니다. 기절 상태가 아니면 false 입니다.
         * @param speedScale 진행 배율(둘이 살리면 2 처럼 — 게임이 정한다). 이미 살리는 중이면 배율과 살리는 쪽만 바뀐다.
         */
        [[nodiscard]] bool startRevive( int32 reviverId, float32 speedScale = 1.0f );
        /** @brief 살리기를 그만둡니다(살리는 쪽이 떠났다). 진행은 `_bKeepReviveProgress` 에 따릅니다. */
        void stopRevive();
        /** @brief @p seconds 동안 피해를 받지 않습니다(이미 남은 것보다 길 때만 늘린다). */
        void setInvulnerable( float32 seconds );
        /** @brief 상태를 가리지 않고 죽입니다(낙사 · 존 밖 · 갈고리 희생). 이미 죽었으면 아무 일도 없습니다. */
        void kill( int32 instigatorId = -1 );
        void resetDownCount() { _downCount = 0; }
        /** @brief 쌓인 알림을 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<VitalityEvent>& outListEvent );
        /** @brief 쌓인 알림을 꺼내지 않고 버립니다(쓰지 않는 쪽 — 받을 목록을 만들어 복사하지 않는다). */
        void discardEvents() { _eventBuffer.clear(); }

        const VitalitySettings& getSettings() const { return _settings; }
        VitalityState           getState() const { return _state; }
        bool                    isAlive() const { return _state == VitalityState::Alive; }
        bool                    isDowned() const { return _state == VitalityState::Downed; }
        bool                    isDead() const { return _state == VitalityState::Dead; }
        float32                 getHealth() const { return _health; }
        float32                 getHealthRatio() const;
        float32                 getShield() const { return _shield; }
        float32                 getDownedHealth() const { return _downedHealth; }
        int32                   getDownCount() const { return _downCount; }
        bool                    isReviving() const { return _bReviving == SW_TRUE; }
        int32                   getReviverId() const { return _reviverId; }
        /** @brief 부활 진행 0..1 입니다. */
        float32 getReviveProgress() const;
        bool    isInvulnerable() const { return _invulnerable.isActive(); }
        float32 getPoise() const { return _poise; }
        bool    isPoiseBroken() const { return _poiseBreak.isActive(); }

        /** @brief 체력 · 보호막 · 다운 · 강인도 · 무적 · 부활 진행 · 상태을 씁니다. 설정은 `initialize` 의 것, 알림은 "일어난 일" 이라 싣지 않고 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void enterDowned( int32 instigatorId );
        void enterDead( int32 instigatorId );
        void finishRevive();
        void pushEvent( VitalityEventType type, float32 amount, int32 instigatorId );

        VitalitySettings           _settings;
        EventBuffer<VitalityEvent> _eventBuffer;
        float32                    _health;
        float32                    _shield;
        float32                    _downedHealth;
        float32                    _poise;
        float32                    _sinceDamage;      ///< 마지막 피해 뒤 지난 시간(재생 지연)
        float32                    _sincePoiseDamage; ///< 마지막 경직 피해 뒤 지난 시간
        Countdown                  _poiseBreak;
        Countdown                  _invulnerable;
        float32                    _reviveElapsed; ///< 배율을 곱해 쌓인 부활 시간
        float32                    _reviveSpeedScale;
        int32                      _reviverId;
        int32                      _lastInstigatorId;
        int32                      _downCount;
        VitalityState              _state;
        uint8                      _bReviving;
    };
} // namespace sw
