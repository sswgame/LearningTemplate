/**
 * @file ActionCombatRig.h
 * @brief 액션 플랫포머의 싸움입니다. 근접 콤보(기반 `MoveTimeline` 을 쓰며, 다음 기술은 캔슬 창에서 나가고 공격을 미리 누를 수 있음), 총(기반 `WeaponState` 를 쓰며 연사, 탄창, 재장전을 처리하고 탄은 2D 투사체),
 *        패리 반사(창 안의 적 탄을 되받아침), 히트스톱(맞힌 순간 세상이 멈춤)을 다룹니다.
 * @details 한 번 호출할 때 한 프레임(`advanceFrame`)씩 진행한다. 시간 단위가 프레임이므로 결정적이다. 히트스톱 동안은 탄, 패리 창, 공격 기억이 모두 멈추고, 기술은 자기 히트스톱만 줄인다.
 *          맞았는지(히트박스 겹침)는 게임이 확인해서 `registerMeleeContact` 로 알려 준다.
 */
#pragma once
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Character/Combat/FrameData.h"
#include "GameFramework/Base/Character/Combat/Weapon/Weapon.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ActionComboDef;

    class ActionPlatformerCatalog;
    class Archive;
    class PlatformTileMap;

    /** @brief 진영(편)입니다. */
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
        MoveStarted = 0, ///< `_id` 는 기술이고 `_value` 는 콤보 인덱스
        ComboEnded,      ///< `_value` 는 이어서 낸 기술 수
        GunFired,        ///< `_value` 는 나간 알 수
        ParryStarted,
        ProjectileReflected, ///< `_value` 는 탄 id
        HitstopStarted       ///< `_value` 는 프레임 수
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
     * @brief 플레이어 하나의 근접, 총, 패리와 화면 위의 투사체입니다.
     */
    class SW_KIT_API ActionCombatRig
    {
    public:
        static constexpr uint32  kStateTag     = FourCcUtil::make( "APCR" );
        static constexpr uint32  kStateVersion = 1;
        static constexpr float32 kFrameTime    = 1.0f / 60.0f; ///< 한 프레임의 시간(탄과 총에 쓴다)

        ActionCombatRig();

        /** @brief 규칙, 기술 테이블, 사용할 콤보를 설정합니다. 모르는 콤보면 false 를 반환합니다(근접 없이 총과 패리만 씁니다). */
        [[nodiscard]] bool initialize( const ActionPlatformerCatalog* pCatalog, const MoveCatalog* pMoves, const hashed_string& comboID );
        /** @brief 총을 장비합니다(예비탄 @p reserveAmmo, 퍼짐 시드 @p seed). */
        void equipGun( const WeaponDef& weapon, int32 reserveAmmo, uint32 seed = 1u );

        /** @brief 공격 버튼입니다. `_attackBufferFrames` 동안 기억했다가, 쓸 수 있는 첫 프레임에 기술을 냅니다. */
        void pressAttack();
        /** @brief 패리 버튼입니다. `_windowFrames` 동안 가까운 적 탄을 되받아칩니다. */
        void pressParry();
        /**
         * @brief 총을 쏩니다. 나간 알마다 플레이어 편 투사체가 생깁니다(속도는 무기의 탄속이고, 0이면 30을 씁니다).
         * @param aim 겨눈 방향(길이는 상관없다)
         */
        WeaponFireResult fireGun( const float2& origin, const float2& aim, bool bTriggerJustPressed );
        /** @brief 적의 탄 같은 투사체를 놓고 id 를 반환합니다. */
        uint32 spawnProjectile( const ActionProjectile& projectile );
        /** @brief 지금 기술이 맞았음을 알립니다(게임이 히트박스 겹침을 확인해서 호출합니다). 기술의 히트스톱을 겁니다. */
        void registerMeleeContact( bool bBlocked );
        /**
         * @brief 한 프레임 진행합니다.
         * @param pMap 투사체가 벽에 닿으면 없앤다(nullptr 이면 확인하지 않는다)
         */
        void advanceFrame( const PlatformTileMap* pMap, const float2& playerPosition );
        /** @brief @p team 편의 투사체 중 @p center 를 중심으로 반경 @p radius 안에 있는 것을 꺼내고(맞은 것), 꺼낸 수를 반환합니다. */
        int32 takeProjectileHits( ActionTeam team, const float2& center, float32 radius, vector<ActionProjectile>& outListHit );
        void  drainEvents( vector<ActionCombatEvent>& outListEvent );

        bool                            isFrozen() const { return _hitstopFrames > 0; }
        int32                           getHitstopFrames() const { return _hitstopFrames; }
        bool                            isParryActive() const { return _parryFrames > 0; }
        int32                           getComboIndex() const { return _comboIndex; }
        const MoveTimeline&             getTimeline() const { return _timeline; }
        const WeaponState&              getGun() const { return _gun; }
        const vector<ActionProjectile>& getProjectiles() const { return _listProjectile; }

        /**
         * @brief 콤보 id, 기술 타임라인(기술 id), 콤보 인덱스, 공격 기억, 패리 창, 히트스톱, 다음 탄 id, 총(장비 여부와 `WeaponState`), 투사체를 씁니다.
         *        카탈로그와 기술 테이블은 `initialize` 에서 받은 것이고 무기 정의는 `equipGun` 에서 받은 것이라 싣지 않으며, 알림은 읽을 때 비웁니다.
         */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다. 콤보와 기술은 `initialize` 의 카탈로그에서 id 로 찾습니다. `initialize` 하지 않았거나, 없는 콤보나 기술이거나,
         *        총 장비 여부와 무기가 지금과 다르거나, 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다.
         */
        [[nodiscard]] bool readState( Archive& archive );

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
        uint32                         _nextProjectileID;
        int32                          _comboIndex; ///< 지금 기술의 콤보 인덱스(−1이면 쉬는 중)
        int32                          _attackBufferFrames;
        int32                          _parryFrames;
        int32                          _hitstopFrames;
        uint8                          _bGunEquipped;
    };
} // namespace sw
