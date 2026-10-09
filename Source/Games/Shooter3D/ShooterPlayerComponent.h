/**
 * @file ShooterPlayerComponent.h
 * @brief 슈터 플레이어(폰) — 무기 셋(연사 · 탄창 · 재장전 · 퍼짐 · 반동), 히트스캔, 체력(`Vitality`), HUD(조준선 · 맞음 표시 · 체력 · 탄약 · 무기 이름), 몸(KayKit 캐릭터) · 탄도선. 의도만 읽는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Combat/Health/Vitality.h"
#include "GameFramework/Base/Actor/Combat/Weapon/Weapon.h"
#include "GameFramework/Base/Actor/Combat/Weapon/WeaponMath.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceSocketRig.h"

namespace sw
{
    class FirstPersonCameraComponent;
    class PawnComponent;
    class ShooterBodyMovementComponent;
    class ShooterDirectorComponent;

    /**
     * @class ShooterPlayerComponent
     * @brief 플레이어 오브젝트(카메라 · `FirstPersonCameraComponent` · `PawnComponent` · `ShooterBodyMovementComponent` · 1인칭 손에 든 총 · HUD(`HudControllerComponent`)와
     *        같은 오브젝트)의 게임 규칙입니다.
     * @details 기본 틱 그룹(`DuringPhysics`)에서 돕니다 — 같은 오브젝트의 1인칭 카메라가 `PrePhysics` 에서 폰의 조종 회전으로 시점을 둔 뒤라, 그 시점으로
     *          걷고 쏘고 `setEyePosition` 으로 눈 자리를 넣습니다. **폰의 의도만 읽습니다**(버튼 Jump · Sprint · Fire · Reload · SwitchWeapon · Weapon1..3, 아날로그
     *          SwitchWeapon) — 사람(플레이어 조종자가 입력 맵 `data/shooter.input.xml` 에서 만든다)이든 자동 플레이 AI(`ShooterAutoAimControllerComponent`)든 같다.
     *          걷기 · 점프는 몸 이동(`ShooterBodyMovementComponent` — 적과 같은 코드)이 한다. 막는 상자 · 적 자리는 디렉터가 `PrePhysics` 에서 적은 것을 읽기만 합니다.
     *
     *          **몸**: 플레이 시작에 몸 프리팹(`_bodyPrefab` — 스킨드 메시 · 애니메이터 · 외형 · `ShooterAvatarComponent`)을 세웁니다. 몸이 이 컴포넌트의
     *          상태(발 · 요 · 속도 · 사격 · 맞음 · 쓰러짐 · 무기)를 읽어 스스로 움직이고, 무기는 외형의 MainHand 칸 아이템(`_listWeaponItem`)이 됩니다.
     *          시점 카메라의 프리셋이 1인칭(`CameraPresetMode::FirstPerson`)이면 몸을 숨기고 손에 든 총 · 조준선을 보이고, 아니면 반대입니다. 눈높이는 몸 소켓
     *          `Eyes` 의 바인드 포즈 높이입니다(없으면 `_eyeHeight`).
     *
     *          **HUD**: 같은 오브젝트의 `HudControllerComponent` 의 뷰모델(`HudViewModel`)에 틱 뒤에 값을 넣습니다 — 조준선(1인칭 · 살아 있을 때) ·
     *          맞음 표시(맞힌 직후 · 1인칭) · 체력 · 탄약(탄창 · 예비) · 무기 이름(무기 표의 이름 = 현지화 키). 위젯은 문서(`game/shooter3d/ui/hud.ui.xml`)의
     *          `{bind:필드}` 가 잇는다 — 이 컴포넌트는 위젯 이름을 모른다.
     *
     *          **탄도선 · 총구 섬광**: 총구(1인칭이면 손에 든 총 × 무기 소켓 `Muzzle`, 아니면 몸 외형의 `MainHand.Muzzle`)에서 맞은 자리까지의 상자 ·
     *          섬광 구를 디렉터의 풀에서 꺼냅니다 — 에디터 없이 App 에서도 보입니다.
     *          적에 주는 피해 · 효과 · 효과음 · 손에 든 총 바꾸기는 쌓아 두고 틱 뒤 한 번에 합니다(게임 스레드).
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Player", Tooltip = "Movement, weapons, hitscan, health, the body and the tracers of the shooter player" )
    class ShooterPlayerComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kWeaponCount = 3;

        ShooterPlayerComponent();
        virtual ~ShooterPlayerComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 적에 맞았다(틱 뒤 게임 스레드). 디렉터의 긴장도 신호가 되고, 쓰러지면 쓰러짐 클립 뒤에 판을 처음부터 다시 시작한다. */
        void takeDamage( float32 amount );
        /** @brief 탄 보상 — 무기마다 탄창 두 개를 채운다(틱 뒤 게임 스레드). */
        void addWaveAmmo();
        /** @brief 수리 보상 — 체력을 채운다(틱 뒤 게임 스레드). */
        void restoreHealth( float32 amount );
        /** @brief 세운 몸을 지웁니다(상태 저장 전). 다음 틱 뒤에 다시 세운다. */
        void despawnViews();

        // ---- 디렉터 · 몸 · 조종자가 읽는 것(이 컴포넌트가 쓰지 않는 그룹) ----
        float3 getEyePosition() const;
        /** @brief 발 자리입니다(몸 이동의 자리 — 없으면 처음 자리). */
        float3             getFeetPosition() const;
        float32            getHealth() const { return _vitality.getHealth(); }
        bool               isAlive() const { return _vitality.isAlive(); }
        const WeaponState& getCurrentWeapon() const { return _arrWeapon[_weaponIndex]; }
        /** @brief 무기 칸 @p weaponIndex(0 소총 · 1 산탄총 · 2 권총)입니다. */
        const WeaponState& getWeapon( int32 weaponIndex ) const { return _arrWeapon[weaponIndex]; }
        int32              getWeaponIndex() const { return _weaponIndex; }
        GameObjectHandle   getDirector() const { return _director; }
        /** @brief 무기의 외형 아이템(외형의 MainHand 칸)입니다. */
        hashed_string getWeaponItem( int32 weaponIndex ) const;
        /** @brief 지금 보는 요(라디안, +Z 에서 +X 쪽)입니다. */
        float32 getLookYaw() const { return _lookYaw; }
        /** @brief 지난 틱의 수평 속도(월드, m/s)입니다. */
        float3 getMoveVelocity() const;
        bool   isOnGround() const;
        /** @brief 마지막으로 쏜 뒤 지난 시간(s)입니다. */
        float32 getTimeSinceShot() const { return _timeSinceShot; }
        /** @brief 맞을 때마다 하나 오르는 수(몸이 움찔 클립을 트는 신호)입니다. */
        uint32 getHitReactionCount() const { return _hitReactionCount; }
        /** @brief 시점 카메라가 1인칭 프리셋이면 true 입니다. */
        bool isFirstPerson() const { return _bFirstPerson == SW_TRUE; }
        /** @brief 지금 무기의 탄(탄창 + 예비)이 탄창 네 개에 얼마나 모자란가 — 0(넉넉) .. 1(없음). 디렉터의 "탄 부족" 신호입니다. */
        float32 computeAmmoShortage() const;
        uint32  getShotCount() const { return _shotCount; }
        /** @brief 세워 둔 몸 오브젝트입니다. 없으면 nullptr 입니다. */
        GameObject* findBodyObject() const;
        uint32      getHitCount() const { return _hitCount; }

    private:
        /** @brief 이번 발의 탄도선 하나 — 틱 뒤에 디렉터의 풀에서 꺼낸다. */
        struct TracerRequest
        {
            float3 _from{};
            float3 _to{};
        };

        /** @brief 적에 줄 피해 하나 — 틱 뒤에 건다. */
        struct EnemyHit
        {
            GameObjectHandle _enemy{};
            float32          _damage{ 0.0f };
        };

        /** @brief 낼 탄착 효과 하나 — 틱 뒤에 디렉터의 풀에서 꺼낸다. */
        struct EffectRequest
        {
            float4  _color{};
            float3  _position{};
            float32 _size{ 0.1f };
            float32 _lifetime{ 0.1f };
        };

    private:
        void equipWeapons();
        /** @brief 의도의 무기 버튼(Weapon1..3 · SwitchWeapon 과 그 아날로그의 부호 — 다음 / 이전) · Reload 를 무기에 넣습니다. */
        void applyWeaponIntent( const PawnComponent& pawn );
        /** @brief 같은 오브젝트의 몸 이동입니다. 없으면 nullptr 입니다. */
        ShooterBodyMovementComponent* findMovement() const;
        void                          fireWeapon( const ShooterDirectorComponent& director, FirstPersonCameraComponent& camera, bool bJustPressed );
        /** @brief 광선 하나 — 가장 가까운 적 · 상자 · 바닥을 찾습니다. 적이면 피해를 쌓습니다. 맞은 거리입니다. */
        float32 traceShot( const ShooterDirectorComponent& director, const GameRay& ray, float32 damage, bool& outHitEnemy );
        /** @brief 총구의 월드 자리입니다 — 1인칭이면 손에 든 총, 아니면 몸 외형의 `MainHand.Muzzle`. 못 찾으면 눈 아래입니다. */
        float3 findMuzzlePosition();
        void   switchWeapon( int32 weaponIndex );
        /** @brief 시점 카메라(대상이 이 오브젝트인 게임 카메라 디렉터)의 프리셋이 1인칭인지 봅니다. */
        bool queryFirstPerson() const;
        /** @brief 같은 오브젝트의 HUD 뷰모델에 조준선 · 맞음 표시 보임과 체력 · 탄약 · 무기 이름을 넣습니다(게임 스레드 — 틱 뒤). HUD 가 없으면 아무것도 하지 않는다. */
        void updateHud();
        void resetRound();
        void scheduleFlush();
        void flushPending();
        bool hasPending() const;
        /** @brief 몸을 세웁니다(게임 스레드, 틱 밖). */
        void spawnBody();
        /** @brief 1인칭 / 그 밖 — 손에 든 총 · 몸 보임을 맞춥니다(게임 스레드). 조준선은 `updateHud` 가 맞춘다. */
        void applyViewMode();

    private:
        PROPERTY( Category = "Player", DisplayName = "Director", Tooltip = "Object with the ShooterDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Player", DisplayName = "Spawn Position", Tooltip = "Feet position at the start and after being overrun", Units = m )
        float3 _spawnPosition;
        PROPERTY( Category = "Player", AssetPath, AssetType = "Prefab", DisplayName = "Body Prefab", Tooltip = "Visible body (skeletal mesh, animator, appearance, ShooterAvatarComponent)" )
        string _bodyPrefab;
        PROPERTY( Category = "Player", DisplayName = "Weapon Items", Tooltip = "Appearance item of each weapon (rifle, shotgun, pistol) worn in the MainHand slot" )
        vector<string> _listWeaponItem;
        PROPERTY( Category = "View", DisplayName = "Eye Height", Tooltip = "Used until the body's Eyes socket is known", Min = 0.0, Units = m )
        float32 _eyeHeight;
        PROPERTY( Category = "Health", DisplayName = "Max Health", Min = 1.0 )
        float32 _maxHealth;
        PROPERTY( Category = "Health", DisplayName = "Regen Delay", Tooltip = "Seconds without a hit before health refills", Min = 0.0, Units = s )
        float32 _regenDelay;
        PROPERTY( Category = "Health", DisplayName = "Regen Per Second", Min = 0.0 )
        float32 _regenPerSecond;
        PROPERTY( Category = "Health", DisplayName = "Down Time", Tooltip = "Seconds the death clip plays before the round starts over", Min = 0.0, Units = s )
        float32 _downTime;
        PROPERTY( Category = "Effects", DisplayName = "Hit Effect Lifetime", Tooltip = "Seconds a hit or muzzle flash sphere stays", Min = 0.0, Units = s )
        float32 _hitEffectLifetime;
        PROPERTY( Category = "Effects", DisplayName = "Tracer Lifetime", Tooltip = "Seconds a tracer stays", Min = 0.0, Units = s )
        float32 _tracerLifetime;
        PROPERTY( Category = "Effects", DisplayName = "Tracer Width", Min = 0.0, Units = m )
        float32 _tracerWidth;

        WeaponState              _arrWeapon[kWeaponCount];
        Vitality                 _vitality;
        AppearanceSocketSetCache _weaponSockets; ///< 1인칭 총구 — 무기 외형의 소켓 에셋
        vector<EnemyHit>         _listPendingHit;
        vector<EffectRequest>    _listPendingEffect;
        vector<TracerRequest>    _listPendingTracer;
        GameSoundQueue           _soundQueue; ///< 낼 소리(틱 뒤 — 오디오는 게임 스레드에서)
        GameObjectHandle         _body;
        float32                  _hitMarkerTimer;
        float32                  _timeSinceShot;
        float32                  _downTimer;     ///< 쓰러진 뒤 지난 시간(쓰러졌을 때만)
        float32                  _bodyEyeHeight; ///< 몸 소켓 Eyes 의 높이(0 = 아직 모름)
        float32                  _lookYaw;
        int32                    _weaponIndex;
        uint32                   _shotCount;
        uint32                   _hitCount;
        uint32                   _hitReactionCount;
        uint8                    _bWeaponModelDirty   : 1; ///< 틱 뒤에 손에 든 총 모델 · 몸의 무기를 바꾼다
        uint8                    _bFlushScheduled     : 1;
        uint8                    _bRoundJustRestarted : 1; ///< 쓰러져 다시 시작한 프레임 — 같은 틱 뒤에 남은 공격은 무시한다
        uint8                    _bFirstPerson        : 1;
        uint8                    _bViewModeDirty      : 1; ///< 틱 뒤에 1인칭 / 그 밖 보임을 맞춘다
        uint8                    _bBodyRequested      : 1; ///< 틱 뒤에 몸을 세운다
        uint8                    _reserved            : 2;
    };
} // namespace sw
