/**
 * @file ShooterPlayerComponent.h
 * @brief 1인칭 플레이어 — 이동 · 점프 · 충돌, 무기 셋(연사 · 탄창 · 재장전 · 퍼짐 · 반동), 히트스캔, 체력 · 회복, 조준선 · 맞음 표시 · 탄도선.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Combat/Weapon.h"
#include "GameFramework/Combat/WeaponMath.h"

namespace sw
{
    class FirstPersonCameraComponent;
    class InputManager;
    class ShooterDirectorComponent;

    /**
     * @class ShooterPlayerComponent
     * @brief 플레이어 오브젝트(카메라 · `FirstPersonCameraComponent` · 손에 든 총 · 조준선 스프라이트와 같은 오브젝트)의 게임 규칙입니다.
     * @details 기본 틱 그룹(`DuringPhysics`)에서 돕니다 — 같은 오브젝트의 1인칭 카메라가 `PrePhysics` 에서 마우스로 시점을 돌린 뒤라, 그 시점으로 걷고 쏘고
     *          `setEyePosition` 으로 눈 자리를 넣습니다. 막는 상자 · 드론 자리는 디렉터가 `PrePhysics` 에서 적은 것을 읽기만 합니다.
     *          드론에 주는 피해 · 탄착 효과 · 효과음 · 손에 든 총 바꾸기 · 탄도선(디버그 선)은 쌓아 두고 틱 뒤 한 번에 합니다(게임 스레드).
     *          무기 카탈로그는 게임 서비스(`WeaponCatalog`)이고 플레이 시작에 무기를 고쳐 든다(핫 리로드가 다시 만든 카탈로그를 본다).
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Player", Tooltip = "First-person movement, weapons, hitscan, health and the crosshair" )
    class ShooterPlayerComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kWeaponCount = 3;

        ShooterPlayerComponent();
        virtual ~ShooterPlayerComponent() override;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 드론에 맞았다(틱 뒤 게임 스레드). 디렉터의 긴장도 신호가 되고, 바닥나면 판을 처음부터 다시 시작한다. */
        void takeDamage( float32 amount );
        /** @brief 탄 보상 — 무기마다 탄창 두 개를 채운다(틱 뒤 게임 스레드). */
        void addWaveAmmo();
        /** @brief 수리 보상 — 체력을 최대치까지 채운다(틱 뒤 게임 스레드). */
        void restoreHealth( float32 amount );

        // ---- 디렉터가 읽는 것(PrePhysics — 이 컴포넌트가 쓰지 않는 그룹) ----
        float3             getEyePosition() const;
        float32            getHealth() const { return _health; }
        const WeaponState& getCurrentWeapon() const { return _arrWeapon[_weaponIndex]; }
        /** @brief 지금 무기의 탄(탄창 + 예비)이 탄창 네 개에 얼마나 모자란가 — 0(넉넉) .. 1(없음). 디렉터의 "탄 부족" 신호입니다. */
        float32 computeAmmoShortage() const;
        uint32  getShotCount() const { return _shotCount; }
        uint32  getHitCount() const { return _hitCount; }

    private:
        /** @brief 잠깐 그리는 탄도선입니다(디버그 선 — 에디터 게임 뷰에서만 보인다). 고정 칸을 돌려 쓴다. */
        struct Tracer
        {
            float3  _from{};
            float3  _to{};
            float32 _remaining{ 0.0f };
        };

        /** @brief 드론에 줄 피해 하나 — 틱 뒤에 건다. */
        struct DroneHit
        {
            GameObjectHandle _drone{};
            float32          _damage{ 0.0f };
        };

        /** @brief 낼 탄착 효과 하나 — 틱 뒤에 디렉터의 풀에서 꺼낸다. */
        struct EffectRequest
        {
            float4  _color{};
            float3  _position{};
            float32 _size{ 0.1f };
        };

        static constexpr int32 kTracerCount = 32;

    private:
        void equipWeapons();
        void tickInput( const InputManager& input, FirstPersonCameraComponent& camera, float3& outMove, bool& outJump, bool& outSprint,
                        bool& outTrigger, bool& outJustPressed );
        void tickAutoAim( float32 deltaTime, const ShooterDirectorComponent& director, FirstPersonCameraComponent& camera, float3& outMove, bool& outTrigger,
                          bool& outJustPressed );
        void movePlayer( const ShooterDirectorComponent& director, const float3& wishDirection, bool bJump, bool bSprint, float32 deltaTime );
        void fireWeapon( const ShooterDirectorComponent& director, FirstPersonCameraComponent& camera, bool bJustPressed );
        /** @brief 광선 하나 — 가장 가까운 드론 · 상자 · 바닥을 찾습니다. 드론이면 피해를 쌓습니다. 맞은 거리입니다. */
        float32 traceShot( const ShooterDirectorComponent& director, const GameRay& ray, float32 damage, bool& outHitDrone );
        void    switchWeapon( int32 weaponIndex );
        void    updateTracers( float32 deltaTime );
        /** @brief 같은 오브젝트의 조준선 · 맞음 표시 스프라이트를 눈앞에 둡니다. */
        void placeOverlay();
        void resetRound();
        void scheduleFlush();
        void flushPending();
        bool hasPending() const;

    private:
        PROPERTY( Category = "Player", DisplayName = "Director", Tooltip = "Object with the ShooterDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Player", DisplayName = "Spawn Position", Tooltip = "Feet position at the start and after being overrun", Meta = "Units=m" )
        float3 _spawnPosition;
        PROPERTY( Category = "Movement", DisplayName = "Walk Speed", Min = 0.0, Meta = "Units=m/s" )
        float32 _walkSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Sprint Speed", Min = 0.0, Meta = "Units=m/s" )
        float32 _sprintSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Jump Speed", Min = 0.0, Meta = "Units=m/s" )
        float32 _jumpSpeed;
        PROPERTY( Category = "Movement", DisplayName = "Gravity", Min = 0.0, Meta = "Units=m/s2" )
        float32 _gravity;
        PROPERTY( Category = "Movement", DisplayName = "Radius", Tooltip = "Body radius against the blockers", Min = 0.0, Meta = "Units=m" )
        float32 _radius;
        PROPERTY( Category = "Movement", DisplayName = "Eye Height", Min = 0.0, Meta = "Units=m" )
        float32 _eyeHeight;
        PROPERTY( Category = "Health", DisplayName = "Max Health", Min = 1.0 )
        float32 _maxHealth;
        PROPERTY( Category = "Health", DisplayName = "Regen Delay", Tooltip = "Seconds without a hit before health refills", Min = 0.0, Meta = "Units=s" )
        float32 _regenDelay;
        PROPERTY( Category = "Health", DisplayName = "Regen Per Second", Min = 0.0 )
        float32 _regenPerSecond;

        WeaponState           _arrWeapon[kWeaponCount];
        Tracer                _arrTracer[kTracerCount];
        vector<DroneHit>      _listPendingHit;
        vector<EffectRequest> _listPendingEffect;
        vector<const utf8*>   _listPendingSound;
        float3                _position; ///< 발
        float32               _verticalSpeed;
        float32               _health;
        float32               _damageCooldown; ///< 맞은 뒤 체력이 다시 차기까지(s)
        float32               _hitMarkerTimer;
        int32                 _weaponIndex;
        int32                 _nextTracer;
        uint32                _shotCount;
        uint32                _hitCount;
        uint8                 _bOnGround           : 1;
        uint8                 _bWeaponModelDirty   : 1; ///< 틱 뒤에 손에 든 총 모델을 바꾼다
        uint8                 _bTracerAlive        : 1; ///< 그릴 탄도선이 남았다(틱 뒤에 디버그 선으로)
        uint8                 _bFlushScheduled     : 1;
        uint8                 _bRoundJustRestarted : 1; ///< 쓰러져 다시 시작한 프레임 — 같은 틱 뒤에 남은 드론 공격은 무시한다
        uint8                 _reserved            : 3;
    };
} // namespace sw
