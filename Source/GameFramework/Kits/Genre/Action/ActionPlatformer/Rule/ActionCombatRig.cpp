#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Rule/ActionCombatRig.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Catalog/ActionPlatformerCatalog.h"
#include "GameFramework/Runtime/Character/Movement/PlatformerMotor2D.h"
#include "GameFramework/Runtime/Foundation/Utility/Math/RayMath.h"

namespace sw
{
    namespace
    {
        /** @brief 화면의 탄이 묻는 세계입니다. 맞는 것은 벽 칸뿐이고(선분 끝이 벽 칸이면 끝난다), 사람 · 적의 맞음은 게임이 `takeProjectileHits` 로 꺼낸다. */
        class ActionRigProjectileQuery : public IProjectileWorld
        {
        public:
            explicit ActionRigProjectileQuery( const PlatformTileMap* pMap )
                : _pMap{ pMap }
            {
            }

            bool findTargetPoint( const ProjectileState& /*projectile*/, float3& /*outPoint*/ ) const override { return false; } // 유도 대상이 없다

            bool sweep( const ProjectileState& /*projectile*/, const float3& /*from*/, const float3& to, ProjectileHit& outHit ) const override
            {
                if ( _pMap == nullptr || _pMap->getTile( _pMap->computeTileX( to._x ), _pMap->computeTileY( to._y ) ) != PlatformTile::Solid )
                    return false;
                outHit._position = to;
                outHit._kind     = ProjectileHitKind::Surface;
                return true;
            }

        private:
            const PlatformTileMap* _pMap;
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
        , _projectiles{}
        , _eventBuffer{}
        , _comboIndex{ -1 }
        , _attackBufferFrames{ 0 }
        , _parryFrames{ 0 }
        , _hitstopFrames{ 0 }
        , _bGunEquipped{ SW_FALSE }
    {
        _projectiles.setPlaneNormal( float3{ 0.0f, 0.0f, 1.0f } ); // 화면 평면(XY)
    }

    ProjectileState ActionCombatRig::makeProjectile( const float2& position, const float2& velocity, ActionTeam team, float32 damage, float32 lifetime )
    {
        ProjectileState projectile;
        projectile._position         = float3{ position._x, position._y, 0.0f };
        projectile._previousPosition = projectile._position;
        projectile._velocity         = float3{ velocity._x, velocity._y, 0.0f };
        projectile._damage           = damage;
        projectile._lifetime         = lifetime;
        projectile._team             = static_cast<int32>( team );
        return projectile;
    }

    bool ActionCombatRig::initialize( const ActionPlatformerCatalog* pCatalog, const MoveCatalog* pMoves, const hashed_string& comboID )
    {
        _pCatalog = pCatalog;
        _pMoves   = pMoves;
        _pCombo   = pCatalog != nullptr ? pCatalog->findCombo( comboID ) : nullptr;
        // 기술 표에 없는 기술을 든 콤보는 받지 않는다. 받으면 그 차례의 공격이 말없이 사라진다.
        if ( _pCombo != nullptr && _pMoves != nullptr )
        {
            for ( const hashed_string& moveID : _pCombo->_listMove )
            {
                if ( _pMoves->find( moveID ) != nullptr )
                    continue;
                SW_LOG_ERROR_ID( "ActionCombatRig.ComboMoveUnknown", "ActionCombatRig: combo '%#' names an unknown move '%#'", comboID.c_str(), moveID.c_str() );
                _pCombo = nullptr;
                break;
            }
        }
        _timeline.cancel();
        _projectiles.clear();
        _eventBuffer.clear();
        _comboIndex         = -1;
        _attackBufferFrames = 0;
        _parryFrames        = 0;
        _hitstopFrames      = 0;
        return _pCombo != nullptr && _pMoves != nullptr;
    }

    bool ActionCombatRig::equipGun( const WeaponDef& weapon, const ProjectileDef& projectile, int32 reserveAmmo, uint32 seed )
    {
        if ( projectile._speed <= 0.0f )
        {
            SW_LOG_ERROR_ID( "ActionCombatRig.GunNeedsFlyingProjectile", "ActionCombatRig: weapon '%#' loads projectile '%#' with speed 0 - 2D shots must fly (set speed)", weapon._id.c_str(),
                             projectile._id.c_str() );
            return false;
        }
        if ( _gun.equip( weapon, projectile, reserveAmmo, seed ) == false )
            return false;
        _bGunEquipped = SW_TRUE;
        return true;
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
        pushEvent( ActionCombatEventKind::ParryStarted, hashed_string{}, _parryFrames );
    }

    WeaponFireResult ActionCombatRig::fireGun( const float2& origin, const float2& aim, bool bTriggerJustPressed )
    {
        if ( _bGunEquipped == SW_FALSE )
            return WeaponFireResult::OutOfAmmo;
        const float32 aimLength = aim.getLength();
        const float2  direction = aimLength > 1.0e-4f ? aim * ( 1.0f / aimLength ) : float2{ 1.0f, 0.0f };
        Ray           ray;
        ray._origin    = float3{ origin._x, origin._y, 0.0f };
        ray._direction = float3{ direction._x, direction._y, 0.0f };
        WeaponShot             shot;
        const WeaponFireResult result = _gun.pullTrigger( ray, bTriggerJustPressed, shot );
        if ( result != WeaponFireResult::Fired )
            return result;
        for ( const Ray& pellet : shot._listRay )
        {
            // 퍼짐 원뿔은 3D 이므로 화면 평면(z = 0)으로 눌러서 2D 방향으로 쓴다. 중력은 플랫포머 탄이 받지 않는다(받는 탄은 쓰는 쪽이 정한다).
            float2        pelletDirection{ pellet._direction._x, pellet._direction._y };
            const float32 length = pelletDirection.getLength();
            pelletDirection      = length > 1.0e-4f ? pelletDirection * ( 1.0f / length ) : direction;
            ProjectileState projectile =
                ProjectileState::launch( _gun.getShot(), float3{ origin._x, origin._y, 0.0f }, float3{ pelletDirection._x, pelletDirection._y, 0.0f }, 0.0f );
            projectile._team = static_cast<int32>( ActionTeam::Player );
            (void)spawnProjectile( projectile ); // 투사체 id 는 쓰지 않는다
        }
        pushEvent( ActionCombatEventKind::GunFired, _gun.getDef()._id, static_cast<int32>( shot._listRay.size() ) );
        return result;
    }

    uint32 ActionCombatRig::spawnProjectile( const ProjectileState& projectile )
    {
        return _projectiles.spawn( projectile );
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
        // 히트스톱은 세상을 멈춘다. 기술은 자기 히트스톱만 같이 줄인다(남은 수가 같으면 함께 풀린다).
        if ( _hitstopFrames > 0 )
        {
            --_hitstopFrames;
            if ( _timeline.isInHitstop() )
                (void)_timeline.advanceFrame();
            return;
        }

        // 근접 공격이다. 쉬고 있으면 첫 기술을 내고, 기술 중이면 다음 기술의 캔슬 창이 열렸을 때 이어서 낸다.
        bool       bStarted        = false;
        const bool bCanStartAttack = _attackBufferFrames > 0 && _pCombo != nullptr && _pMoves != nullptr;
        if ( bCanStartAttack )
        {
            const int32 nextIndex = _comboIndex + 1;
            if ( _timeline.isPlaying() == false )
                bStarted = startMove( 0 );
            else if ( nextIndex < static_cast<int32>( _pCombo->_listMove.size() ) && _timeline.canCancelInto( _pCombo->_listMove[static_cast<size_t>( nextIndex )] ) )
                bStarted = startMove( nextIndex );
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
                pushEvent( ActionCombatEventKind::ComboEnded, hashed_string{}, _comboIndex + 1 );
                _comboIndex = -1;
            }
        }

        if ( _bGunEquipped == SW_TRUE )
            _gun.update( kFrameTime );

        // 패리다. 창 안이면 가까운 적 탄을 왔던 쪽으로 되받아친다(내 편 탄이 되고 더 세진다).
        if ( _parryFrames > 0 && _pCatalog != nullptr )
        {
            const ActionParryRules& rules      = _pCatalog->getParryRules();
            const float3            player     = float3{ playerPosition._x, playerPosition._y, 0.0f };
            bool                    bReflected = false;
            for ( ProjectileState& projectile : _projectiles.accessProjectiles() )
            {
                const bool bEnemy = projectile._team == static_cast<int32>( ActionTeam::Enemy );
                if ( bEnemy == false || float3::getDistanceSquared( projectile._position, player ) > rules._radius * rules._radius )
                    continue;
                projectile.deflect( static_cast<int32>( ActionTeam::Player ), rules._reflectSpeedScale, rules._reflectDamageScale );
                bReflected = true;
                pushEvent( ActionCombatEventKind::ProjectileReflected, hashed_string{}, static_cast<int32>( projectile._id ) );
            }
            --_parryFrames;
            if ( bReflected )
                startHitstop( rules._hitstopFrames );
        }

        // 투사체는 움직이다가, 시간 · 사거리가 다 되거나 벽에 닿으면 없앤다(공통 발사체 시뮬레이션).
        const ActionRigProjectileQuery query( pMap );
        (void)_projectiles.step( kFrameTime, query ); // 사건은 쓰지 않는다
    }

    int32 ActionCombatRig::takeProjectileHits( ActionTeam team, const float2& center, float32 radius, vector<ProjectileState>& outListHit )
    {
        outListHit.clear();
        if ( radius < 0.0f )
            return 0; // 제곱하면 음수 반경이 양수 반경처럼 맞는다
        const float3 point = float3{ center._x, center._y, 0.0f };
        for ( ProjectileState& projectile : _projectiles.accessProjectiles() )
        {
            if ( projectile._team == static_cast<int32>( team ) && float3::getDistanceSquared( projectile._position, point ) <= radius * radius )
            {
                outListHit.push_back( projectile );
                projectile._bAlive = SW_FALSE;
            }
        }
        _projectiles.removeDead();
        return static_cast<int32>( outListHit.size() );
    }

    void ActionCombatRig::pollEvents( vector<ActionCombatEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    bool ActionCombatRig::startMove( int32 comboIndex )
    {
        const hashed_string& moveID = _pCombo->_listMove[static_cast<size_t>( comboIndex )];
        const MoveFrameData* pMove  = _pMoves->find( moveID );
        if ( pMove == nullptr || _timeline.start( *pMove ) == false )
            return false;
        _comboIndex = comboIndex;
        pushEvent( ActionCombatEventKind::MoveStarted, moveID, comboIndex );
        return true;
    }

    void ActionCombatRig::startHitstop( int32 frames )
    {
        if ( frames <= _hitstopFrames )
            return;
        _hitstopFrames = frames;
        pushEvent( ActionCombatEventKind::HitstopStarted, hashed_string{}, frames );
    }

    void ActionCombatRig::pushEvent( ActionCombatEventKind type, const hashed_string& id, int32 value )
    {
        ActionCombatEvent event;
        event._type  = type;
        event._id    = id;
        event._value = value;
        _eventBuffer.push( event );
    }

    void ActionCombatRig::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr && _pMoves != nullptr ) == false )
            return;
        io.reference( _pCombo, [this]( const hashed_string& comboID )
        { return _pCatalog->findCombo( comboID ); } );
        _timeline.serializeState( io, _pMoves );
        io.value( _comboIndex );
        io.value( _attackBufferFrames );
        io.value( _parryFrames );
        io.value( _hitstopFrames );
        io.expect( _bGunEquipped );
        const int32 comboMoveCount = _pCombo != nullptr ? static_cast<int32>( _pCombo->_listMove.size() ) : 0;
        if ( io.check( -1 <= _comboIndex && _comboIndex < comboMoveCount && 0 <= _attackBufferFrames && 0 <= _parryFrames && 0 <= _hitstopFrames ) == false )
            return;
        io.value( _gun );
        // 탄 수치 · id(다음 id 아래여야 새 탄과 겹치지 않는다)는 시뮬레이션이 보고, 편은 여기서 본다.
        io.value( _projectiles );
        if ( io.isWriting() || io.isOk() == false )
            return;
        for ( const ProjectileState& projectile : _projectiles.getProjectiles() )
        {
            const bool bTeamValid = projectile._team == static_cast<int32>( ActionTeam::Player ) || projectile._team == static_cast<int32>( ActionTeam::Enemy );
            io.check( bTeamValid && projectile._position._z == 0.0f && projectile._velocity._z == 0.0f );
        }
        _eventBuffer.clear();
    }

} // namespace sw
