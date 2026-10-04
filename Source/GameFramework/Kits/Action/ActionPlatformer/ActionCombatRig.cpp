#include "pch.h"

#include "GameFramework/Kits/Action/ActionPlatformer/ActionCombatRig.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Action/ActionPlatformer/ActionPlatformerCatalog.h"
#include "GameFramework/Movement/PlatformerMotor2D.h"
#include "GameFramework/Utility/RayMath.h"

namespace sw
{
    namespace
    {
        struct ActionCombatRigInternal
        {
            static constexpr float32 kDefaultBulletSpeed = 30.0f; ///< 무기 탄속이 0(히트스캔)일 때 2D 탄의 속도
        };
    } // namespace
} // namespace sw

namespace sw
{
    ActionCombatRig::ActionCombatRig()
        : _pCatalog{ nullptr }
        , _pMoves{ nullptr }
        , _pCombo{ nullptr }
        , _timeline{}
        , _gun{}
        , _listProjectile{}
        , _eventBuffer{}
        , _nextProjectileId{ 1 }
        , _comboIndex{ -1 }
        , _attackBufferFrames{ 0 }
        , _parryFrames{ 0 }
        , _hitstopFrames{ 0 }
        , _bGunEquipped{ SW_FALSE }
    {
    }

    bool ActionCombatRig::initialize( const ActionPlatformerCatalog* pCatalog, const MoveCatalog* pMoves, const hashed_string& comboId )
    {
        _pCatalog = pCatalog;
        _pMoves   = pMoves;
        _pCombo   = pCatalog != nullptr ? pCatalog->findCombo( comboId ) : nullptr;
        _timeline.cancel();
        _listProjectile.clear();
        _eventBuffer.clear();
        _comboIndex         = -1;
        _attackBufferFrames = 0;
        _parryFrames        = 0;
        _hitstopFrames      = 0;
        return _pCombo != nullptr && _pMoves != nullptr;
    }

    void ActionCombatRig::equipGun( const WeaponDef& weapon, int32 reserveAmmo, uint32 seed )
    {
        _gun.equip( weapon, reserveAmmo, seed );
        _bGunEquipped = SW_TRUE;
    }

    void ActionCombatRig::pressAttack()
    {
        if ( _pCatalog != nullptr )
            _attackBufferFrames = MathUtil::max( 1, _pCatalog->getParryRules()._attackBufferFrames );
    }

    void ActionCombatRig::pressParry()
    {
        if ( _pCatalog == nullptr )
            return;
        _parryFrames = _pCatalog->getParryRules()._windowFrames;
        pushEvent( ActionCombatEventType::ParryStarted, hashed_string{}, _parryFrames );
    }

    WeaponFireResult ActionCombatRig::fireGun( const float2& origin, const float2& aim, bool bTriggerJustPressed )
    {
        if ( _bGunEquipped == SW_FALSE )
            return WeaponFireResult::OutOfAmmo;
        const float32 aimLength = aim.getLength();
        const float2  direction = aimLength > 1.0e-4f ? aim * ( 1.0f / aimLength ) : float2{ 1.0f, 0.0f };
        GameRay       ray;
        ray._origin    = float3{ origin._x, origin._y, 0.0f };
        ray._direction = float3{ direction._x, direction._y, 0.0f };
        WeaponShot             shot;
        const WeaponFireResult result = _gun.pullTrigger( ray, bTriggerJustPressed, shot );
        if ( result != WeaponFireResult::Fired )
            return result;
        const WeaponDef& def   = _gun.getDef();
        const float32    speed = def._projectileSpeed > 0.0f ? def._projectileSpeed : ActionCombatRigInternal::kDefaultBulletSpeed;
        for ( const GameRay& pellet : shot._listRay )
        {
            // 퍼짐 원뿔은 3D 라 화면 평면(z = 0)으로 눌러 2D 방향으로 쓴다.
            float2        pelletDirection{ pellet._direction._x, pellet._direction._y };
            const float32 length = pelletDirection.getLength();
            pelletDirection      = length > 1.0e-4f ? pelletDirection * ( 1.0f / length ) : direction;
            ActionProjectile projectile;
            projectile._position = origin;
            projectile._velocity = pelletDirection * speed;
            projectile._damage   = def._damage;
            projectile._lifetime = def._range / speed;
            projectile._team     = ActionTeam::Player;
            (void)spawnProjectile( projectile );
        }
        pushEvent( ActionCombatEventType::GunFired, def._id, static_cast<int32>( shot._listRay.size() ) );
        return result;
    }

    uint32 ActionCombatRig::spawnProjectile( const ActionProjectile& projectile )
    {
        ActionProjectile added = projectile;
        added._id              = _nextProjectileId++;
        _listProjectile.push_back( added );
        return added._id;
    }

    void ActionCombatRig::registerMeleeContact( bool bBlocked )
    {
        if ( _timeline.isPlaying() == false )
            return;
        _timeline.registerContact( bBlocked );
        startHitstop( _timeline.getMove()._hitstop );
    }

    void ActionCombatRig::advanceFrame( const PlatformTileMap* pMap, const float2& playerPosition )
    {
        // 히트스톱 — 세상이 멈춘다. 기술은 자기 히트스톱만 같이 줄인다(남은 수가 같으면 함께 풀린다).
        if ( _hitstopFrames > 0 )
        {
            --_hitstopFrames;
            if ( _timeline.isInHitstop() )
                (void)_timeline.advanceFrame();
            return;
        }

        // 근접 — 쉬고 있으면 첫 기술, 기술 중이면 다음 기술의 캔슬 창이 열렸을 때 이어 낸다.
        bool bStarted = false;
        if ( _attackBufferFrames > 0 && _pCombo != nullptr && _pMoves != nullptr )
        {
            const int32 nextIndex = _comboIndex + 1;
            if ( _timeline.isPlaying() == false )
            {
                startMove( 0 );
                bStarted = true;
            }
            else if ( nextIndex < static_cast<int32>( _pCombo->_listMove.size() ) && _timeline.canCancelInto( _pCombo->_listMove[static_cast<size_t>( nextIndex )] ) )
            {
                startMove( nextIndex );
                bStarted = true;
            }
            if ( bStarted )
                _attackBufferFrames = 0;
            else
                --_attackBufferFrames;
        }
        if ( bStarted == false && _timeline.isPlaying() )
        {
            (void)_timeline.advanceFrame();
            if ( _timeline.isPlaying() == false )
            {
                pushEvent( ActionCombatEventType::ComboEnded, hashed_string{}, _comboIndex + 1 );
                _comboIndex = -1;
            }
        }

        if ( _bGunEquipped == SW_TRUE )
            _gun.update( kFrameTime );

        // 패리 — 창 안이면 가까운 적 탄을 왔던 쪽으로 되받아친다(내 편 탄이 되고 더 세다).
        if ( _parryFrames > 0 && _pCatalog != nullptr )
        {
            const ActionParryRules& rules      = _pCatalog->getParryRules();
            bool                    bReflected = false;
            for ( ActionProjectile& projectile : _listProjectile )
            {
                if ( projectile._team != ActionTeam::Enemy || float2::getDistanceSquared( projectile._position, playerPosition ) > rules._radius * rules._radius )
                    continue;
                projectile._velocity   = projectile._velocity * -rules._reflectSpeedScale;
                projectile._damage     = projectile._damage * rules._reflectDamageScale;
                projectile._team       = ActionTeam::Player;
                projectile._bReflected = SW_TRUE;
                bReflected             = true;
                pushEvent( ActionCombatEventType::ProjectileReflected, hashed_string{}, static_cast<int32>( projectile._id ) );
            }
            --_parryFrames;
            if ( bReflected )
                startHitstop( rules._hitstopFrames );
        }

        // 투사체 — 움직이고, 시간이 다 했거나 벽에 닿으면 없앤다.
        for ( size_t index = _listProjectile.size(); index > 0; --index )
        {
            ActionProjectile& projectile = _listProjectile[index - 1];
            projectile._position         = projectile._position + projectile._velocity * kFrameTime;
            projectile._lifetime -= kFrameTime;
            bool bRemove = projectile._lifetime <= 0.0f;
            if ( bRemove == false && pMap != nullptr )
                bRemove = pMap->getTile( pMap->computeTileX( projectile._position._x ), pMap->computeTileY( projectile._position._y ) ) == PlatformTile::Solid;
            if ( bRemove )
                _listProjectile.erase( _listProjectile.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }

    int32 ActionCombatRig::takeProjectileHits( ActionTeam team, const float2& center, float32 radius, vector<ActionProjectile>& outListHit )
    {
        outListHit.clear();
        for ( size_t index = 0; index < _listProjectile.size(); )
        {
            const ActionProjectile& projectile = _listProjectile[index];
            if ( projectile._team == team && float2::getDistanceSquared( projectile._position, center ) <= radius * radius )
            {
                outListHit.push_back( projectile );
                _listProjectile.erase( _listProjectile.begin() + static_cast<ptrdiff_t>( index ) );
                continue;
            }
            ++index;
        }
        return static_cast<int32>( outListHit.size() );
    }

    void ActionCombatRig::drainEvents( vector<ActionCombatEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void ActionCombatRig::startMove( int32 comboIndex )
    {
        const hashed_string& moveId = _pCombo->_listMove[static_cast<size_t>( comboIndex )];
        const MoveFrameData* pMove  = _pMoves->findMove( moveId );
        if ( pMove == nullptr )
            return;
        _timeline.start( *pMove );
        _comboIndex = comboIndex;
        pushEvent( ActionCombatEventType::MoveStarted, moveId, comboIndex );
    }

    void ActionCombatRig::startHitstop( int32 frames )
    {
        if ( frames <= _hitstopFrames )
            return;
        _hitstopFrames = frames;
        pushEvent( ActionCombatEventType::HitstopStarted, hashed_string{}, frames );
    }

    void ActionCombatRig::pushEvent( ActionCombatEventType type, const hashed_string& id, int32 value )
    {
        ActionCombatEvent event;
        event._type  = type;
        event._id    = id;
        event._value = value;
        _eventBuffer.push( event );
    }
} // namespace sw
