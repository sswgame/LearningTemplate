/**
 * @file ActionCombatRig.h
 * @brief 액션 플랫포머의 싸움 — 근접 콤보(기반 `MoveTimeline` — 다음 기술은 캔슬 창에서, 공격 미리 누르기) · 총(기반 `WeaponState` — 연사 · 탄창 · 재장전, 탄은 2D 투사체) ·
 *        패리 반사(창 안의 적 탄을 되받아친다) · 히트스톱(맞힌 순간 세상이 멈춘다)입니다.
 * @details 한 번 부를 때 한 프레임(`advanceFrame`)씩 갑니다 — 시간 단위가 프레임이라 결정적입니다. 히트스톱 동안은 탄 · 패리 창 · 공격 기억이 모두 멈추고,
 *          기술은 자기 히트스톱만 줄입니다. 맞았는지(히트박스 겹침)는 게임이 보고 `registerMeleeContact` 로 알려 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Combat/FrameData.h"
#include "GameFramework/Base/Combat/Weapon.h"
#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ActionComboDef;

    class ActionPlatformerCatalog;
    class MoveCatalog;
    class PlatformTileMap;

    /** @brief 편입니다. */
    enum class ActionTeam : uint8
    {
        Player = 0,
        Enemy
    };

    /** @brief 2D 투사체 하나입니다. */
    struct ActionProjectile
    {
        float2     _position{};
        float2     _velocity{};
        float32    _damage{ 0.0f };
        float32    _lifetime{ 3.0f }; ///< 남은 시간(초)
        uint32     _id{ 0 };
        ActionTeam _team{ ActionTeam::Enemy };
        uint8      _bReflected{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 싸움 알림의 종류입니다. */
    enum class ActionCombatEventType : uint8
    {
        MoveStarted = 0, ///< `_id` = 기술, `_value` = 콤보 자리
        ComboEnded,      ///< `_value` = 이어 낸 기술 수
        GunFired,        ///< `_value` = 나간 알 수
        ParryStarted,
        ProjectileReflected, ///< `_value` = 탄 id
        HitstopStarted       ///< `_value` = 프레임
    };

    /** @brief 싸움 알림 하나입니다. */
    struct ActionCombatEvent
    {
        hashed_string         _id{};
        int32                 _value{ 0 };
        ActionCombatEventType _type{ ActionCombatEventType::MoveStarted };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ActionCombatRig
     * @brief 플레이어 하나의 근접 · 총 · 패리와 화면 위 투사체입니다.
     */
    class SW_GF_API ActionCombatRig
    {
    public:
        static constexpr float32 kFrameTime = 1.0f / 60.0f; ///< 한 프레임(탄 · 총 시간)

        ActionCombatRig();

        /** @brief 규칙 · 기술 표 · 쓸 콤보를 둡니다. 모르는 콤보면 false 입니다(근접 없이 총 · 패리만). */
        [[nodiscard]] bool initialize( const ActionPlatformerCatalog* pCatalog, const MoveCatalog* pMoves, const hashed_string& comboId );
        /** @brief 총을 듭니다(예비탄 @p reserveAmmo, 퍼짐 씨앗 @p seed). */
        void equipGun( const WeaponDef& weapon, int32 reserveAmmo, uint32 seed = 1u );

        /** @brief 공격 버튼 — `_attackBufferFrames` 동안 기억해 쓸 수 있는 첫 프레임에 냅니다. */
        void pressAttack();
        /** @brief 패리 버튼 — `_windowFrames` 동안 가까운 적 탄을 되받아칩니다. */
        void pressParry();
        /**
         * @brief 총을 쏩니다. 나간 알마다 플레이어 편 투사체가 생깁니다(속도는 무기 탄속, 0 이면 30).
         * @param aim 겨눈 방향(길이는 상관없다)
         */
        WeaponFireResult fireGun( const float2& origin, const float2& aim, bool bTriggerJustPressed );
        /** @brief 투사체를 놓습니다(적의 탄). id 를 돌려줍니다. */
        uint32 spawnProjectile( const ActionProjectile& projectile );
        /** @brief 지금 기술이 맞았습니다(게임이 히트박스 겹침을 보고 부른다) — 기술의 히트스톱을 겁니다. */
        void registerMeleeContact( bool bBlocked );
        /**
         * @brief 한 프레임 갑니다.
         * @param pMap 투사체가 벽에 닿으면 없앤다(nullptr 이면 보지 않는다)
         */
        void advanceFrame( const PlatformTileMap* pMap, const float2& playerPosition );
        /** @brief @p team 편의 투사체 중 @p center 반경 @p radius 안의 것을 꺼냅니다(맞은 것). 꺼낸 수입니다. */
        int32 takeProjectileHits( ActionTeam team, const float2& center, float32 radius, vector<ActionProjectile>& outListHit );
        void  drainEvents( vector<ActionCombatEvent>& outListEvent );

        bool                            isFrozen() const { return _hitstopFrames > 0; }
        int32                           getHitstopFrames() const { return _hitstopFrames; }
        bool                            isParryActive() const { return _parryFrames > 0; }
        int32                           getComboIndex() const { return _comboIndex; }
        const MoveTimeline&             getTimeline() const { return _timeline; }
        const WeaponState&              getGun() const { return _gun; }
        const vector<ActionProjectile>& getProjectiles() const { return _listProjectile; }

    private:
        void startMove( int32 comboIndex );
        void startHitstop( int32 frames );
        void pushEvent( ActionCombatEventType type, const hashed_string& id, int32 value );

        const ActionPlatformerCatalog* _pCatalog;
        const MoveCatalog*             _pMoves;
        const ActionComboDef*          _pCombo;
        MoveTimeline                   _timeline;
        WeaponState                    _gun;
        vector<ActionProjectile>       _listProjectile;
        EventBuffer<ActionCombatEvent> _eventBuffer;
        uint32                         _nextProjectileId;
        int32                          _comboIndex; ///< 지금 기술의 콤보 자리(−1 = 쉬는 중)
        int32                          _attackBufferFrames;
        int32                          _parryFrames;
        int32                          _hitstopFrames;
        uint8                          _bGunEquipped;
    };
} // namespace sw
