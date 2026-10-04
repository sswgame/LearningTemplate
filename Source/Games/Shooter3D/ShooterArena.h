/**
 * @file ShooterArena.h
 * @brief Shooter3D 의 게임 규칙 — 1인칭 이동 · 충돌, 무기 셋, 히트스캔, 드론 웨이브, 체력, 조준선 · 맞음 표시입니다.
 *
 * @details 무기 규칙(연사 · 탄창 · 재장전 · 퍼짐 · 반동)과 광선 판정은 기반(`GameFramework/Combat`)이 맡고, 여기는 "어디서 누구를 쏘는가" 와 그 모습만 압니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Combat/Weapon.h"
#include "GameFramework/Combat/WeaponMath.h"
#include "GameFramework/Input/FirstPersonLook.h"
#include "GameFramework/Stage/PrimitiveStage.h"

namespace sw
{
    class InputManager;

    /**
     * @class ShooterArena
     * @brief 아레나 한 판입니다. 벽 · 상자는 축 상자이고 드론은 구라서 키트의 광선 판정을 그대로 씁니다.
     */
    class ShooterArena
    {
    public:
        static constexpr int32   kWeaponCount     = 3;
        static constexpr float32 kMaxPlayerHealth = 100.0f;

        ShooterArena();
        ~ShooterArena();

        ShooterArena( const ShooterArena& )            = delete;
        ShooterArena& operator=( const ShooterArena& ) = delete;

        void               initialize( const WeaponCatalog* pCatalog );
        [[nodiscard]] bool spawn();
        void               despawn();
        void               update( float32 deltaTime );

    private:
        /** @brief 막는 상자(벽 · 엄폐물)입니다. */
        struct ArenaBox
        {
            float3 _min{};
            float3 _max{};
        };

        /** @brief 드론 하나입니다. */
        struct ArenaDrone
        {
            GameObjectHandle _object{};
            float3           _position{};
            float32          _health{ 30.0f };
            float32          _maxHealth{ 30.0f };
            float32          _speed{ 3.0f };
            float32          _attackCooldown{ 0.0f };
            float32          _flashTimer{ 0.0f };
            float32          _bobPhase{ 0.0f };
            uint8            _bFlashing{ SW_FALSE }; ///< 지금 흰색으로 칠해져 있다(바뀔 때만 다시 칠한다)
        };

        /** @brief 잠깐 보이는 것(탄착 · 터짐)입니다. */
        struct ArenaEffect
        {
            GameObjectHandle _object{};
            float32          _remaining{ 0.0f };
        };

        /** @brief 잠깐 그리는 탄도선입니다(디버그 선 — 개발 빌드에서만 보인다). */
        struct ArenaTracer
        {
            float3  _from{};
            float3  _to{};
            float32 _remaining{ 0.0f };
        };

        void buildLayout();
        void updateInput( float32 deltaTime, const InputManager* pInput );
        void updateAutoAim( float32 deltaTime, float3& outMove, bool& outTrigger, bool& outJustPressed );
        void movePlayer( const float3& wishDirection, bool bJump, bool bSprint, float32 deltaTime );
        void fireWeapon( bool bTriggerHeld, bool bJustPressed );
        /** @brief 광선 하나 — 가장 가까운 드론 · 상자를 찾아 드론이면 피해를 줍니다. 맞은 거리입니다. */
        float32 traceShot( const GameRay& ray, float32 damage, bool& outHitDrone );
        void    updateDrones( float32 deltaTime );
        void    updateEffects( float32 deltaTime );
        void    updateOverlay();
        void    spawnWave();
        void    spawnEffect( const float3& position, float32 size, const float4& color, float32 lifetime );
        /** @brief 엄폐물 상자(AABB) 하나를 나무 상자 모델 더미로 채웁니다(충돌은 AABB 그대로). */
        void spawnCrateStack( const ArenaBox& box );
        void switchWeapon( int32 weaponIndex );
        void damagePlayer( float32 amount );
        /** @brief 원(XZ)을 상자 밖으로 밀어냅니다. */
        float3 resolveCircle( const float3& position, float32 radius ) const;
        float3 getEyePosition() const;
        void   logStatus( float32 deltaTime );

        PrimitiveStage       _stage;
        FirstPersonLook      _look;
        WeaponState          _arrWeapon[kWeaponCount];
        vector<ArenaBox>     _listBox;
        vector<ArenaDrone>   _listDrone;
        vector<ArenaEffect>  _listEffect;
        vector<ArenaTracer>  _listTracer;
        vector<float3>       _listSpawnPoint;
        const WeaponCatalog* _pCatalog;
        float3               _playerPosition; ///< 발
        float32              _verticalSpeed;
        float32              _playerHealth;
        float32              _damageCooldown; ///< 맞은 뒤 체력이 다시 차기까지(s)
        float32              _waveTimer;      ///< 0 이상이면 다음 웨이브까지 남은 시간
        float32              _hitMarkerTimer;
        float32              _statusTimer;
        GameObjectHandle     _crosshair;
        GameObjectHandle     _hitMarker;
        GameObjectHandle     _viewWeapon; ///< 손에 든 총(1인칭)
        int32                _weaponIndex;
        uint32               _wave;
        uint32               _killCount;
        uint32               _shotCount;
        uint32               _hitCount;
        uint8                _bOnGround;
        uint8                _bMouseLocked;
        uint8                _bSpawned;
    };
} // namespace sw
