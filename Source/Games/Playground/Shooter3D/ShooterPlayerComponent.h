/**
 * @file ShooterPlayerComponent.h
 * @brief 슈터 플레이어(폰)를 구현합니다. 무기 세 종류(연사, 탄창, 재장전, 퍼짐, 반동), 히트스캔, 체력(`HealthStateMachine`), HUD(조준선, 맞음 표시, 체력, 탄약, 무기 이름), 몸(KayKit 캐릭터), 탄도선을 맡으며 입력은 의도만 읽습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/Radians.h"
#include "Core/Math/Ray.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/Runtime/Character/Combat/Health/HealthStateMachine.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/Weapon.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFrameworkComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSocketRig.h"

namespace sw
{
    class ShooterHUDViewModel;
    class FirstPersonCameraComponent;
    class GameObjectManager;
    class PawnComponent;
    class ShooterBodyMovementComponent;
    class ShooterGameModeComponent;
    class ShooterNetPawnComponent;

    /**
     * @class ShooterPlayerComponent
     * @brief 플레이어 오브젝트의 게임 규칙입니다. 같은 오브젝트에 카메라, `FirstPersonCameraComponent`, `PawnComponent`, `ShooterBodyMovementComponent`,
     *        1인칭 손에 든 총, HUD(클라이언트 전용 `HUDControllerComponent`)가 있습니다.
     * @details 기본 틱 그룹(`DuringPhysics`)에서 돈다. 같은 오브젝트의 1인칭 카메라가 `PrePhysics` 에서 폰의 조종 회전으로 시점을 정한 뒤라, 그 시점으로
     *          걷고 쏘고 `setEyePosition` 으로 눈 위치를 넣는다. **폰의 의도만 읽는다**(버튼 Jump, Sprint, Fire, Reload, SwitchWeapon, Weapon1..3, 아날로그
     *          SwitchWeapon). 사람(플레이어 조종자가 입력 맵 `data/shooter.input.xml` 에서 만든다)이든 자동 플레이 AI(`ShooterAutoAimControllerComponent`)든 같다.
     *          걷기와 점프는 몸 이동(`ShooterBodyMovementComponent`, 적과 같은 코드)이 한다. 막는 상자와 적 위치는 디렉터가 `PrePhysics` 에서 기록한 것을 읽기만 한다.
     *
     *          **몸**: 플레이가 시작되면 몸 프리팹(`_bodyPrefab`, 스킨드 메시, 애니메이터, 외형, `ShooterAvatarComponent`)을 스폰한다. 몸이 이 컴포넌트의
     *          상태(발, 요, 속도, 사격, 맞음, 쓰러짐, 무기)를 읽어 스스로 움직이고, 무기는 외형의 MainHand 슬롯 아이템(무기 표의 `item`)이 된다.
     *          시점 카메라의 프리셋이 1인칭(`CameraPresetMode::FirstPerson`)이면 몸의 머리 · 머리 장비 · 몸 무기만 숨기고(`ShooterAvatarComponent::setFirstPerson`)
     *          손에 든 총과 조준선을 보이며, 아니면 반대로 한다. 눈높이는 몸 소켓
     *          `Eyes` 의 바인드 포즈 높이에 체형의 다리 길이가 몸을 올린 높이(몸 리그 `rest_leg_lift`)를 더한 값이다(없으면 `_eyeHeight`) — 1인칭 카메라가
     *          머리를 따른다(언리얼 트루 1인칭 · 스카이림 종족 키). 애니메이션 머리 흔들림은 섞지 않아 조준 원점이 흔들리지 않는다.
     *
     *          **HUD**: 게임의 뷰모델(`ShooterHUDViewModel`)을 이 컴포넌트가 들고 틱이 끝난 뒤 값을 넣으며, `IHUDViewModelBinder` 로 같은 오브젝트의 HUD 에 건다(전용 서버는 창구가 없어 걸지 않는다). 조준선(1인칭이고 살아 있을 때),
     *          맞음 표시(맞힌 직후, 1인칭), 체력, 탄약(탄창과 예비), 무기 이름(무기의 장비 아이템 표의 이름이 곧 현지화 키)이 대상이다. 위젯은 문서(`game/playground/shooter3d/ui/hud.ui.xml`)의
     *          `{bind:필드}` 가 연결한다. 이 컴포넌트는 위젯 이름을 모른다.
     *
     *          **탄도선과 총구 섬광**: 총구(1인칭이면 손에 든 총과 무기 소켓 `Muzzle`, 아니면 몸 외형의 `MainHand.Muzzle`)에서 맞은 위치까지의 상자와
     *          섬광 구를 디렉터의 풀에서 꺼낸다. 에디터 없이 App 에서도 보인다.
     *          적에게 주는 피해, 효과, 효과음, 손에 든 총 교체는 쌓아 두었다가 틱이 끝난 뒤 한 번에 처리한다(게임 스레드).
     *          **히트스캔**: 틱 안에서는 막는 상자 · 바닥까지의 거리만 재고(`traceCover`), 적은 틱 뒤에 그 거리 안에서 물리 질의로 래그돌 히트박스를 묻는다
     *          (`resolveShots`, 레이어 `_hitboxLayer`). 피해는 장전한 탄의 피해(`WeaponState::getShot`) × 맞은 바디의 히트 존 배율이다.
     *          **근접**(버튼 Melee): 눈앞의 짧은 호를 칼날로 쓸어(`MeleeHit::sweep`, 틱 뒤 · 게임 스레드) 히트박스를 맞힌다. 네트 클라이언트는 맞음을 찾아 서버에 주장하고, 서버는 자기 스윕과 주장을 맞춰 정한다(`MeleeSweepHistory`).
     *          **탄**: 사거리 · 피해 · 궤적(탄도선 색 · 두께)은 장전한 탄(`ProjectileCatalog`, projectiles.xml)이고, 탄착 효과(색 · 크기)는 탄과 표면(`Body` · `Cover`)의 맞음 표다.
     *          탄은 무기가 받는 것만 넣는다(`WeaponDef::_accepts`). 적을 맞히면 그 탄의 맞음 동작(`<OnHit>`)을 적의 태그 문맥으로 부른다.
     */
    REFLECT( Category = "Shooter", DisplayName = "Shooter Player", Tooltip = "Movement, weapons, hitscan, health, the body and the tracers of the shooter player" )
    class ShooterPlayerComponent : public GameFrameworkComponent
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( ShooterPlayerComponent );

        static constexpr int32 kWeaponCount = 3;

        /**
         * @brief 의도 @p intent(버튼 차례는 폰 @p pawn 의 스키마)가 고른 무기 슬롯입니다 — 버튼 Weapon1..3 은 그 슬롯, SwitchWeapon 은 같은 이름 아날로그의 부호대로 앞 · 뒤로 한 칸(끝에서 돈다).
         * @return 발동한 것이 없거나 방향이 0 이면 @p currentSlot 그대로입니다. 둘 다 발동하면 SwitchWeapon 이 이긴다.
         */
        [[nodiscard]] static int32 chooseWeaponSlot( const PawnComponent& pawn, const ControlIntent& intent, int32 currentSlot );
        /**
         * @brief 발에서 눈까지의 높이를 고릅니다. 네트가 움직이는 몸은 설정값 하나다 — 서버의 몸에는 보이는 몸이 없어 소켓 높이를 쓰면 발사 자리가 갈린다.
         * @details 혼자 하는 몸은 보이는 몸의 눈 소켓(@p bodyEyeHeight — 조립 전이면 0)에 다리 올림을 더한 값입니다.
         */
        [[nodiscard]] static float32 chooseEyeHeight( float32 settingHeight, float32 bodyEyeHeight, float32 bodyLegLift, bool bNetDriven );

        ShooterPlayerComponent();
        virtual ~ShooterPlayerComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;
        void onWorldOriginShifted( const float3& delta ) override;

        /** @brief 적 · 다른 플레이어에게 맞았습니다(틱이 끝난 뒤 게임 스레드에서 부릅니다). 디렉터의 긴장도 신호가 되고, 쓰러지면 쓰러짐 클립이 끝난 뒤 다시 시작합니다(`finishDown`). */
        void takeDamage( float32 amount );
        /** @brief 규칙 설정(체력 · 무기 · 근접 · 몸 · 게임 모드)을 @p source 에서 옮기고 처음 자리를 @p spawnPosition 으로 둡니다 — 서버가 접속에 세운 몸이며 화면 · HUD 가 없습니다. 플레이를 시작하기 전에 부릅니다. */
        void copySetup( const ShooterPlayerComponent& source, const float3& spawnPosition );
        /** @brief 이 사람이 쓰러뜨린 수를 하나 셉니다(서버 · 혼자, 틱이 끝난 뒤 게임 스레드). */
        void recordKill() { ++_killCount; }
        /** @brief 매치의 기록(처치 · 쓰러짐)을 지우고 처음 자리에서 다시 시작합니다(틱 뒤 · 게임 스레드) — 앞사람이 나간 자리를 다음 사람이 이어받지 않게. */
        void resetMatch();
        /** @brief 판을 처음부터 다시 엽니다(틱 뒤 · 게임 스레드) — 디렉터가 적을 걷고 감독을 다시 돌리며, 플레이어는 처음 자리에서 다시 시작합니다. */
        void restartRound();
        /**
         * @brief 서버 판정 결과를 받습니다(클라이언트의 넷 폰이 소유자 상태에서 부른다). 맞힌 발 수가 오르면 맞음 표시 · 소리를 내고, HUD 체력은 이 값을 보입니다.
         * @details 클라이언트의 사격은 탄도선 · 탄착만 예측한다 — 맞음을 전달하지 않고 표시도 서버가 인정한 뒤에 낸다(언리얼 Lyra 의 서버 확인 맞음 표시와 같은 자리, 기억에 의존).
         */
        void applyServerReport( uint32 confirmedHitCount, uint32 killCount, float32 health, bool bOut );
        /**
         * @brief 네트 걸음 하나의 발동입니다(서버 · 클라이언트 예측, 틱 뒤 · 게임 스레드) — 이동을 마친 자리에서 무기를 @p step 만큼 진행하고 의도의 발사 · 근접을 바로 판정합니다.
         * @details 이동과 발동이 명령 하나의 한 걸음이다(Valve usercmd — `PlayerRunCommand` 안에서 이동 뒤 `ItemPostFrame`, 언리얼 ServerMove 의 저장된 이동마다 처리). 프레임 틱의 사격은
         *          합친 의도 · 컴포넌트 틱 차례에 달렸다 — 네트가 움직이는 몸은 그 길을 쓰지 않는다.
         * @param commandTick 이 명령의 입력 틱입니다(근접 주장을 맞추는 번호).
         */
        void stepNetCommand( GameObjectManager& manager, const ControlIntent& intent, float32 step, uint32 commandTick );
        /** @brief 이 플레이어의 몸 프리팹을 하나 세웁니다(게임 스레드, 틱 밖) — 자기 몸과 남의 모습이 같은 몸이다. 프리팹에 `ShooterAvatarComponent` 가 없으면 오류를 남기고 빈 핸들입니다. */
        GameObjectHandle spawnBodyObject( GameObjectManager& manager, const utf8* pName ) const;
        /** @brief `spawnBodyObject` 로 세운 몸을 외형 부품까지 지웁니다. 이미 없으면 아무 일도 없습니다. */
        static void destroyBodyObject( GameObjectManager& manager, GameObjectHandle body );
        /** @brief 지금 근접 창에서 이미 맞힌 오브젝트입니다. 서버가 주장을 인정할 때 스윕과 같은 목록을 써 창 하나에 한 번만 맞게 합니다. */
        vector<GameObjectHandle>& getMeleeHitTargets() { return _listMeleeHitTarget; }
        /** @brief 근접이 닿는 거리입니다(자동 플레이 AI 가 휘두를지 정한다). */
        float32 getMeleeReach() const { return _meleeReach; }
        /** @brief 맞힌 발을 하나 셉니다(근접 스윕 · 인정된 맞음 주장). 맞음 표시 · 소리를 냅니다. */
        void recordMeleeHit();
        /** @brief 탄 보상입니다. 무기마다 탄창 두 개를 채웁니다(틱이 끝난 뒤 게임 스레드에서 부릅니다). */
        void addWaveAmmo();
        /** @brief 수리 보상입니다. 체력을 채웁니다(틱이 끝난 뒤 게임 스레드에서 부릅니다). */
        void restoreHealth( float32 amount );
        /** @brief 치트 `cheat.shooter.god` 입니다. 켜면 피해를 받지 않습니다(`HealthStateMachine::setDamageImmune`, 판을 다시 시작해도 남는다). */
        void setDamageImmune( bool bImmune ) { _healthMachine.setDamageImmune( bImmune ); }
        bool isDamageImmune() const { return _healthMachine.isDamageImmune(); }
        /** @brief 스폰한 몸을 지웁니다(상태를 저장하기 전). 다음 틱이 끝난 뒤 다시 스폰합니다. */
        void despawnViews();
        /**
         * @brief 지금 무기에 탄 @p projectileID 를 장전합니다(치트 `cheat.shooter.projectile`, 시나리오). 빈 id 는 그 무기의 기본 탄입니다.
         * @return 탄 카탈로그에 없거나 무기가 받지 않는 탄이면 오류를 남기고 false 이며 장전은 그대로입니다.
         */
        [[nodiscard]] bool loadProjectile( const hashed_string& projectileID );

        // ---- 디렉터, 몸, 조종자가 읽는 것(이 컴포넌트가 쓰지 않는 그룹) ----
        /** @brief 눈 자리입니다(씬 자리 — 카메라와 물리 질의가 그대로 쓴다). */
        float3 getEyePosition() const;
        /** @brief 발 위치입니다(절대 — 몸 이동의 위치이며, 없으면 처음 위치). */
        AbsolutePosition getFeetPosition() const;
        /** @brief 그릴 발 자리입니다 — 시뮬레이션 자리에 예측 보정을 녹이는 오프셋을 더한 것(몸 · 카메라). 판정(사격 · 감독)은 `getFeetPosition` 입니다. */
        AbsolutePosition   getDisplayFeetPosition() const;
        float32            getHealth() const { return _healthMachine.getHealth(); }
        float32            getMaxHealth() const { return _maxHealth; }
        bool               isAlive() const { return _healthMachine.isAlive(); }
        const WeaponState& getCurrentWeapon() const { return _arrWeapon[_weaponIndex]; }
        /** @brief 무기 슬롯 @p weaponIndex(0 소총, 1 산탄총, 2 권총)의 무기입니다. */
        const WeaponState& getWeapon( int32 weaponIndex ) const { return _arrWeapon[weaponIndex]; }
        int32              getWeaponIndex() const { return _weaponIndex; }
        /** @brief 무기의 외형 아이템(외형의 MainHand 슬롯)입니다. 무기 표의 `item` 이며, 범위 밖 슬롯이면 빈 이름입니다. */
        hashed_string getWeaponItem( int32 weaponIndex ) const;
        /** @brief 지금 보는 요(라디안, +Z 에서 +X 쪽)입니다. */
        float32 getLookYaw() const { return _lookYaw; }
        /** @brief 지난 틱의 수평 속도(월드, m/s)입니다. */
        float3 getMoveVelocity() const;
        bool   isOnGround() const;
        /** @brief 지금 무기가 다음 발을 쏠 수 있으면 true 입니다 — 연사 쿨다운 · 재장전이 끝났고 탄창에 탄이 있다(시나리오 감시 값 `Shooter.FireReady`). */
        bool isFireReady() const { return getCurrentWeapon().isCoolingDown() == false && getCurrentWeapon().isReloading() == false && getCurrentWeapon().getMagazineAmmo() > 0; }
        /** @brief 마지막으로 쏜 뒤 지난 시간(s)입니다. */
        float32 getTimeSinceShot() const { return _timeSinceShot; }
        /** @brief 맞을 때마다 하나씩 오르는 수입니다(몸이 움찔 클립을 트는 신호). */
        uint32 getHitReactionCount() const { return _hitReactionCount; }
        /** @brief 시점 카메라가 1인칭 프리셋이면 true 입니다. */
        bool isFirstPerson() const { return _bFirstPerson == SW_TRUE; }
        /** @brief 지금 무기의 탄(탄창과 예비)이 탄창 네 개에 비해 얼마나 모자란지 반환합니다. 0(넉넉)부터 1(없음)까지이며 디렉터의 "탄 부족" 신호입니다. */
        float32 computeAmmoShortage() const;
        uint32  getShotCount() const { return _shotCount; }
        /** @brief 마지막으로 그리게 넘긴 탄도선의 두께입니다(탄의 궤적 칸). 쏜 적이 없으면 0 입니다. */
        float32 getLastTracerWidth() const { return _lastTracerWidth; }
        /** @brief 마지막 탄도선의 시작점이 눈에서 조준 방향으로 얼마나 앞인지(m)입니다. 1인칭이면 시점 총의 총구라 총 앞이고, 쏜 적이 없으면 0 입니다. */
        float32 getLastTracerStartAhead() const { return _lastTracerStartAhead; }
        /** @brief 스폰해 둔 몸 오브젝트입니다. 없으면 nullptr 입니다. */
        const GameObject* findBodyObject() const;
        /** @brief 위와 같되 쓰기 가능한 몸입니다 — 틱 밖 · 틱 뒤(`flushPending`)에서만 부른다(쓰기 가능한 매니저를 꺼낸다). */
        GameObject* findMutableBodyObject();
        uint32      getHitCount() const { return _hitCount; }
        /** @brief 매치 서버가 알려 준 이 판의 처치 수입니다(접속하지 않았으면 0). */
        uint32 getServerKillCount() const { return _serverKillCount; }
        /** @brief 매치 서버가 알려 준 체력입니다(아직 받지 않았으면 -1). */
        float32 getServerHealth() const { return _bServerHealth == SW_TRUE ? _serverHealth : -1.0f; }
        /** @brief 매치 서버가 이 사람을 쓰러뜨려 다시 태어나기를 기다리면 true 입니다(클라이언트). 걷지도 쏘지도 않는다. */
        bool isServerDown() const { return _bServerHealth == SW_TRUE && _serverHealth <= 0.0f; }
        /** @brief 이 사람의 처치 수입니다(서버 · 혼자). */
        uint32 getKillCount() const { return _killCount; }
        /** @brief 매치에서 정해진 횟수만큼 쓰러져 이 사람의 판이 끝났으면 true 입니다(서버). */
        bool isOut() const { return _bOut == SW_TRUE; }

    private:
        /** @brief 이번 발의 탄도선 하나입니다. 틱이 끝난 뒤 디렉터의 풀에서 꺼냅니다. */
        struct TracerRequest
        {
            float4  _color{};
            float3  _from{};
            float3  _to{};
            float32 _width{ 0.0f };
        };

        /**
         * @brief 이번 틱에 쏜 광선 하나입니다. 틱 안에서는 막는 상자 · 바닥까지의 거리만 재고, 적 판정(물리 질의)은 틱이 끝난 뒤 게임 스레드에서 합니다.
         * @details 물리 씬은 한 스레드에서만 다룬다(`CharacterRayHit`). 같은 방아쇠의 광선(산탄)은 같은 `_volley` 라 소리 · 맞음 수를 한 번만 낸다.
         */
        struct PendingShot
        {
            Ray             _ray{};
            float3          _muzzle{};              ///< 탄도선 시작점(`_bHasMuzzle` 일 때만 뜻이 있다)
            hashed_string   _projectileID{};        ///< 장전한 탄(맞음 표 · 맞음 동작을 찾는다)
            float4          _tracerColor{};         ///< 탄의 궤적 색
            float32         _tracerWidth{ 0.0f };   ///< 탄의 궤적 두께
            float32         _coverDistance{ 0.0f }; ///< 막는 상자 · 바닥 · 사거리 중 가까운 것(m)
            float32         _range{ 0.0f };         ///< 탄 사거리(m) — 이보다 짧으면 무언가에 닿았다
            float32         _damage{ 0.0f };
            int32           _pierceCount{ 0 }; ///< 탄이 더 꿰뚫는 적 수(`ResolvedShot::_pierceCount`)
            uint32          _volley{ 0 };
            ControlViewTick _viewTick{ ControlViewTick::makePresent() }; ///< 쏜 사람이 보던 시각(서버가 이 시각으로 되감는다)
            bool            _bHasMuzzle{ false };                        ///< 총구를 찾았다. 아니면 판정만 하고 탄도선을 내지 않는다(대신 쓸 자리는 없다)
        };

        /** @brief 근접 창의 칼날 한 번입니다 — 걸음(네트) · 프레임(혼자)마다 하나다. */
        struct PendingSwing
        {
            ControlViewTick _viewTick{ ControlViewTick::makePresent() };
            float3          _eye{};
            float32         _yaw{ 0.0f };
            float32         _pitch{ 0.0f };
            float32         _fraction{ 0.0f };
            uint32          _step{ 0 }; ///< 휘두른 입력 틱(네트가 아니면 0)
        };

        /** @brief 낼 탄착 효과 하나입니다. 틱이 끝난 뒤 디렉터의 풀에서 꺼냅니다. */
        struct EffectRequest
        {
            float4  _color{};
            float3  _position{};
            float32 _size{ 0.1f };
            float32 _lifetime{ 0.1f };
        };

    private:
        /**
         * @brief 무기 슬롯 셋을 씬 데이터(`_listWeaponID`)대로 채웁니다. 1인칭 총은 몸의 MainHand 부품(그 무기 아이템의 외형)을 따라 그린다(`syncViewWeapon`).
         * @details 목록 길이가 슬롯 수와 다르거나 id 가 무기 카탈로그에 없거나 그 무기에 `item` 이 없으면 오류를 남기고 false 다. 다른 무기로 대체하지 않는다.
         */
        [[nodiscard]] bool equipWeapons();
        /** @brief 의도의 무기 버튼(`chooseWeaponSlot`)과 Reload 를 무기에 반영합니다. */
        void applyWeaponIntent( const PawnComponent& pawn, const ControlIntent& intent );
        /** @brief 의도 하나의 발사 · 근접을 광선 · 칼날로 쌓습니다(`resolveShots` · `resolveMelee` 가 판정한다). 혼자 하는 프레임과 네트 걸음이 같은 이 함수를 쓴다. */
        void runIntent( const ShooterGameModeComponent& gameMode, FirstPersonCameraComponent& camera, const PawnComponent& pawn, const ControlIntent& intent, float32 step, uint32 commandTick );
        /** @brief 무기의 연사 · 재장전 시간을 흘립니다. */
        void advanceWeapons( float32 step );
        /** @brief 발에서 눈까지의 높이입니다(`chooseEyeHeight`). */
        float32 computeEyeHeight() const;
        /** @brief 발사 · 근접의 눈 자리입니다(씬) — 예측 보정 오프셋이 없는 시뮬레이션 자리다. */
        float3 computeAimOrigin() const;
        /** @brief 같은 오브젝트의 몸 이동입니다. 없으면 nullptr 입니다. */
        ShooterBodyMovementComponent* findMovement() const;
        /** @brief 방아쇠를 당깁니다. 눈에서 @p aimDirection 으로 쏘고, 반동은 @p camera 에 준다. */
        void fireWeapon( const ShooterGameModeComponent& gameMode, FirstPersonCameraComponent& camera, const float3& aimDirection, ControlViewTick viewTick, bool bJustPressed );
        /** @brief 광선 하나가 막는 상자 · 바닥에 닿는 거리입니다(닿지 않으면 사거리). 틱 안에서 부르며 물리를 묻지 않습니다. */
        float32 traceCover( const ShooterGameModeComponent& gameMode, const Ray& ray ) const;
        /**
         * @brief 쌓인 광선을 적의 히트박스(래그돌 바디, 레이어 `_hitboxLayer`)에 물리 질의로 쏘고, 맞은 적에 맞음을 보내며 탄도선 · 탄착 · 소리를 쌓습니다(게임 스레드).
         * @details 질의 거리는 막는 상자까지라 엄폐물 뒤의 적은 맞지 않는다. 피해 배율은 맞은 바디의 히트 존(머리 ×2 등)이 정한다(`CharacterRayHit::makeHitInfo`).
         */
        void resolveShots( GameObjectManager& manager );
        /** @brief 다른 플레이어 @p inoutVictim 을 때립니다(서버, 틱 뒤 · 게임 스레드). 쓰러뜨렸으면 처치로 셉니다. 산 사람을 때렸으면 true 입니다. */
        bool strikePlayer( ShooterPlayerComponent& inoutVictim, float32 damage );
        /** @brief 근접 창의 이번 프레임 칼날을 쓸어 맞힙니다(틱 뒤 · 게임 스레드). 클라이언트는 맞음을 서버에 주장합니다. */
        void resolveMelee( GameObjectManager& manager );
        /**
         * @brief 총구의 월드 위치를 찾습니다. 1인칭이면 손에 든 총(무기 외형 소켓 에셋의 `Muzzle`), 아니면 몸 외형의 `MainHand.Muzzle` 입니다.
         * @return 소켓 · 외형 데이터가 없으면 무엇이 빠졌는지 오류를 남기고 false 입니다 — 다른 자리로 대신하지 않는다(탄도선 · 섬광을 내지 않는다).
         */
        [[nodiscard]] bool findMuzzlePosition( float3& outMuzzle );
        /**
         * @brief 몸(남의 오브젝트)에서 읽는 값 — 눈높이 · 다리 올림 · 3인칭 총구 — 을 사본으로 받습니다. 틱 뒤(게임 스레드)에 매 프레임 부른다.
         * @details 몸의 리그 · 포즈 · 외형은 같은 틱 그룹에서 몸의 틱이 쓰므로 플레이어 틱(워커)에서 읽지 않는다 — 틱은 지난 프레임의 사본을 쓴다.
         */
        void refreshBodySnapshot();
        void switchWeapon( int32 weaponIndex );
        /** @brief 시점 카메라(대상이 이 오브젝트인 게임 카메라 디렉터)의 프리셋이 1인칭인지 확인합니다. */
        bool queryFirstPerson() const;
        /** @brief 같은 오브젝트의 HUD 뷰모델에 조준선과 맞음 표시의 가시성, 체력, 탄약, 무기 이름을 넣습니다(틱이 끝난 뒤 게임 스레드에서). HUD 가 없으면 아무것도 하지 않습니다. */
        void updateHUD();
        void resetRound();
        /** @brief 쓰러짐 클립이 끝났습니다(틱 뒤 · 게임 스레드) — 혼자면 판을 끝내거나 다시 열고, 서버면 다시 태어나게 하거나 매치의 판을 끝냅니다. */
        void finishDown( GameObjectManager& manager, ShooterGameModeComponent* pGameMode );
        /** @brief 이 씬의 네트 세션입니다(씬의 플레이어 오브젝트에 있다 — 서버가 세운 몸도 같은 것을 쓴다). 없으면 nullptr 입니다. */
        ShooterNetPawnComponent* findNetPawn( GameObjectManager& manager ) const;
        void                     scheduleFlush();
        void                     flushPending();
        bool                     hasPending() const;
        /** @brief 몸을 스폰합니다(게임 스레드, 틱 밖). */
        void spawnBody();
        /** @brief 1인칭인지 아닌지에 따라 손에 든 총과 몸의 가시성을 맞춥니다(게임 스레드). 조준선은 `updateHUD` 가 맞춥니다. */
        void applyViewMode();
        /** @brief 1인칭 총의 메시 · 머티리얼을 몸의 MainHand 부품에서 가져옵니다. 몸이 아직 조립하지 않았으면 다음 틱에 다시 본다. */
        void syncViewWeapon();

    private:
        PROPERTY( Category = "Player", DisplayName = "Spawn Position", Tooltip = "Feet position at the start and after being overrun", Units = m )
        float3 _spawnPosition;
        PROPERTY( Category = "Player", DisplayName = "Body Prefab", Tooltip = "Visible body (skeletal mesh, animator, appearance, ShooterAvatarComponent)" )
        AssetRef<PrefabAsset> _bodyPrefab;
        PROPERTY( Category = "Player", DisplayName = "Weapon IDs", Tooltip = "Weapon catalog id of each slot (keys Weapon1..Weapon3) - every id must be in weapons.xml" )
        vector<CatalogKey<WeaponDef>> _listWeaponID;
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
        PROPERTY( DefaultName, Category = "Weapons", DisplayName = "Hitbox Layer", Tooltip = "Physics layer of the enemy hitboxes the shots query (the ragdoll bodies)" )
        hashed_string _hitboxLayer{ "Ragdoll" };
        PROPERTY( Category = "Weapons", DisplayName = "Shot Impulse", Tooltip = "Impulse a bullet gives the body it hits (hit reaction, ragdoll push)", Min = 0.0, Units = "N*s" )
        float32 _shotImpulse;
        PROPERTY( Category = "Melee", DisplayName = "Melee Damage", Tooltip = "Base damage of a melee hit (the hit zone multiplies it)", Min = 0.0 )
        float32 _meleeDamage;
        PROPERTY( Category = "Melee", DisplayName = "Melee Impulse", Tooltip = "Impulse a melee hit gives the body it hits", Min = 0.0, Units = "N*s" )
        float32 _meleeImpulse;
        PROPERTY( Category = "Melee", DisplayName = "Melee Reach", Tooltip = "Blade length from the eye", Min = 0.0, Units = m )
        float32 _meleeReach;
        PROPERTY( Category = "Melee", DisplayName = "Melee Radius", Tooltip = "Blade thickness", Min = 0.0, Units = m )
        float32 _meleeRadius;
        PROPERTY( Category = "Melee", DisplayName = "Melee Arc", Tooltip = "Yaw the blade sweeps across the look direction", Min = 0.0 )
        Radians _meleeArc;
        PROPERTY( Category = "Melee", DisplayName = "Melee Duration", Tooltip = "Seconds of one swing", Min = 0.001, Units = s )
        float32 _meleeDuration;
        PROPERTY( Category = "Effects", DisplayName = "Hit Effect Lifetime", Tooltip = "Seconds a hit or muzzle flash sphere stays", Min = 0.0, Units = s )
        float32 _hitEffectLifetime;
        PROPERTY( Category = "Effects", DisplayName = "Tracer Lifetime", Tooltip = "Seconds a tracer stays", Min = 0.0, Units = s )
        float32 _tracerLifetime;
        PROPERTY( Category = "Effects", DisplayName = "Muzzle Flash Color", Tooltip = "HDR colour of the muzzle flash (above 1 glows)" )
        float4 _muzzleFlashColor;

        WeaponState                     _arrWeapon[kWeaponCount];
        HealthStateMachine              _healthMachine;
        AppearanceSocketSetCache        _weaponSockets; ///< 1인칭 총구. 무기 외형의 소켓 에셋.
        vector<PendingShot>             _listPendingShot;
        vector<PendingSwing>            _listPendingSwing;
        vector<float3>                  _listMeleePrevious;  ///< 지난 프레임의 칼날(근접 창의 첫 프레임은 비었다)
        vector<float3>                  _listMeleeCurrent;   ///< 이번 프레임의 칼날
        vector<GameObjectHandle>        _listMeleeHitTarget; ///< 이번 근접 창에서 이미 맞힌 오브젝트
        vector<EffectRequest>           _listPendingEffect;
        vector<TracerRequest>           _listPendingTracer;
        GameSoundQueue                  _soundQueue; ///< 낼 소리(틱이 끝난 뒤 처리한다. 오디오는 게임 스레드에서 다룬다).
        GameObjectHandle                _body;
        float32                         _hitMarkerTimer;
        float32                         _timeSinceShot;
        float32                         _downTimer;     ///< 쓰러진 뒤 지난 시간(쓰러졌을 때만).
        float32                         _bodyEyeHeight; ///< 몸 소켓 Eyes 의 높이(0은 아직 모른다는 뜻).
        float32                         _bodyLegLift;   ///< 체형의 다리 길이가 몸을 올린 높이(몸 리그 `rest_leg_lift`) — 눈높이가 몸을 따른다
        float3                          _bodyMuzzle;    ///< 몸 외형의 총구 소켓 월드 위치(지난 틱 뒤 사본, `_bBodyMuzzleFound` 일 때만 뜻이 있다)
        float32                         _lookYaw;
        float32                         _lastTracerWidth;      ///< 마지막 탄도선 두께(감시 값 `Shooter.TracerWidth`)
        float32                         _lastTracerStartAhead; ///< 마지막 탄도선 시작점의 눈 앞 거리(감시 값 `Shooter.TracerStartAhead`)
        float32                         _serverHealth;         ///< 서버가 알려 준 체력(`_bServerHealth` 일 때 HUD 가 이 값을 보인다)
        unique_ptr<ShooterHUDViewModel> _hudViewModel;         ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다). 처음 `updateHUD` 에 만든다
        float32                         _meleeElapsed;         ///< 근접 창이 시작하고 지난 시간
        int32                           _weaponIndex;
        uint32                          _shotCount;
        uint32                          _hitCount;
        uint32                          _hitReactionCount;
        uint32                          _killCount;               ///< 이 사람의 처치 수
        int32                           _downCount;               ///< 매치에서 쓰러진 횟수(서버)
        uint32                          _serverKillCount;         ///< 서버가 알려 준 이 판의 처치 수(`_bServerHealth` 일 때만 뜻이 있다 — 접속한 매치의 결과)
        uint8                           _bWeaponModelDirty   : 1; ///< 틱이 끝난 뒤 몸의 무기(MainHand 칸)를 바꾼다.
        uint8                           _bViewWeaponSynced   : 1; ///< 1인칭 총이 몸의 MainHand 부품을 따랐다. 아니면 틱이 끝날 때마다 다시 본다(몸 조립은 틱 뒤에 미뤄진다)
        uint8                           _bFlushScheduled     : 1;
        uint8                           _bRoundJustRestarted : 1; ///< 쓰러져 다시 시작한 프레임. 같은 틱이 끝난 뒤에 남은 공격은 무시한다.
        uint8                           _bFirstPerson        : 1;
        uint8                           _bViewModeDirty      : 1; ///< 틱이 끝난 뒤 1인칭 여부에 따른 가시성을 맞춘다.
        uint8                           _bBodyRequested      : 1; ///< 틱이 끝난 뒤 몸을 스폰한다.
        uint8                           _bServerHealth       : 1; ///< 네트 클라이언트 — 체력은 서버의 값이다
        uint8                           _bMeleeActive        : 1; ///< 근접 창이 열려 있다
        uint8                           _bBodyMuzzleFound    : 1; ///< 지난 틱 뒤에 몸 외형의 총구 소켓을 찾았다(`refreshBodySnapshot`)
        uint8                           _bServerOut          : 1; ///< 네트 클라이언트 — 매치 서버가 이 사람의 판을 끝냈다
        uint8                           _bOut                : 1; ///< 서버 — 매치에서 정해진 횟수만큼 쓰러졌다
        uint8                           _bRemoteBody         : 1; ///< 서버가 접속에 세운 몸이다 — 이 화면의 플레이어가 아니다(1인칭 · HUD · 총구 연출이 없다)
        [[maybe_unused]] uint8          _reserved            : 2;
    };
} // namespace sw
