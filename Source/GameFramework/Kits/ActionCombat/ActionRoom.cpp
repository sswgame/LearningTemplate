#include "pch.h"

#include "GameFramework/Kits/ActionCombat/ActionRoom.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"

#include "RuntimeAPI/Service/GameService.h"

#include <algorithm>

namespace sw
{

	namespace
	{

		float32 length2( float32 x, float32 y )
		{
			return x * x + y * y;
		}

		void normalize2( float32& x, float32& y )
		{
			const float32 lenSq = length2( x, y );
			if ( lenSq < 1e-6f )
			{
				x = 0.0f;
				y = 0.0f;
				return;
			}
			const float32 inv = MathUtil::invSqrt( lenSq );
			x *= inv;
			y *= inv;
		}

		AABB makeCircleAabb( float32 x, float32 y, float32 radius )
		{
			AABB box{};
			box._min = float3( x - radius, 0.0f, y - radius );
			box._max = float3( x + radius, 1.0f, y + radius );
			return box;
		}

	} // namespace

	ActionRoom::ActionRoom()
		: _kind{ ActionRoomKind::None }
		, _layers{}
		, _listActor{}
		, _listProjectile{}
		, _attackCooldown{ 0.0f }
		, _dashCooldown{ 0.0f }
		, _invulnTimer{ 0.0f }
		, _bossMaxHp{ 1.0f }
		, _bCleared{ 0 }
		, _reserved{ 0 }
	{
		_layers.resetDefaults();
		_layers.setLayerCollision( kLayerPlayer, kLayerPlayer, false );
		_layers.setLayerCollision( kLayerPlayerAtk, kLayerPlayer, false );
		_layers.setLayerCollision( kLayerPlayerAtk, kLayerProjectile, false );
		_layers.setLayerCollision( kLayerEnemy, kLayerEnemy, false );
		_layers.setLayerCollision( kLayerProjectile, kLayerEnemy, false );
	}

	void ActionRoom::clear()
	{
		_kind = ActionRoomKind::None;
		_listActor.clear();
		_listProjectile.clear();
		_attackCooldown = 0.0f;
		_dashCooldown	= 0.0f;
		_invulnTimer	= 0.0f;
		_bossMaxHp		= 1.0f;
		_bCleared		= 0;
	}

	void ActionRoom::beginEntrance()
	{
		clear();
		_kind = ActionRoomKind::Hall;
		spawnGrunt( 6.0f, 3.0f );
		spawnGrunt( 8.0f, 5.0f );
	}

	void ActionRoom::beginHall()
	{
		clear();
		_kind = ActionRoomKind::Hall;
		spawnGrunt( 5.0f, 2.5f );
		spawnGrunt( 8.0f, 4.0f );
		spawnGrunt( 6.5f, 5.5f );
	}

	void ActionRoom::beginBoss()
	{
		clear();
		_kind = ActionRoomKind::Boss;
		spawnBoss( 9.0f, 5.0f );
		spawnGrunt( 6.0f, 3.0f );
		spawnGrunt( 11.0f, 7.0f );
	}

	float32 ActionRoom::getDashFill() const
	{
		constexpr float32 kDashCd = 0.85f;
		if ( _dashCooldown <= 0.0f )
			return 1.0f;
		return MathUtil::saturate( 1.0f - ( _dashCooldown / kDashCd ) );
	}

	float32 ActionRoom::getBossHpFill() const
	{
		if ( _kind != ActionRoomKind::Boss || _bossMaxHp <= 0.0f )
			return 0.0f;
		for ( const Actor& actor : _listActor )
		{
			if ( actor._kind == ActorKind::Boss && actor._bAlive != 0 )
				return MathUtil::saturate( actor._hp / _bossMaxHp );
		}
		return 0.0f;
	}

	int32 ActionRoom::getAliveEnemyCount() const
	{
		int32 count{ 0 };
		for ( const Actor& actor : _listActor )
		{
			if ( actor._bAlive != 0 )
				++count;
		}
		return count;
	}

	ActionRoomFrameResult ActionRoom::update( float32 deltaTime, const ActionRoomFrameInput& input )
	{
		ActionRoomFrameResult result{};
		if ( _kind == ActionRoomKind::None )
			return result;

		_attackCooldown = ( _attackCooldown > deltaTime ) ? ( _attackCooldown - deltaTime ) : 0.0f;
		_dashCooldown	= ( _dashCooldown > deltaTime ) ? ( _dashCooldown - deltaTime ) : 0.0f;
		_invulnTimer	= ( _invulnTimer > deltaTime ) ? ( _invulnTimer - deltaTime ) : 0.0f;

		if ( input._bDashPressed != 0 && _dashCooldown <= 0.0f )
		{
			_dashCooldown		 = 0.85f;
			_invulnTimer		 = 0.35f;
			result._bDashStarted = 1;
		}

		tryPlayerAttack( input );
		updateActors( deltaTime, input._playerX, input._playerY );
		updateProjectiles( deltaTime );
		resolvePlayerHits( input._playerX, input._playerY, result );
		refreshCleared( result );
		return result;
	}

	void ActionRoom::drawDebug() const
	{
		if ( _kind == ActionRoomKind::None )
			return;

		DebugDrawQueue& dbg = *game::getService<DebugDrawQueue>();
		for ( const Actor& actor : _listActor )
		{
			if ( actor._bAlive == 0 )
				continue;
			const float4 color = ( actor._kind == ActorKind::Boss )
								   ? float4( 1.0f, 0.25f, 0.2f, 1.0f )
								   : float4( 1.0f, 0.55f, 0.2f, 1.0f );
			dbg.drawSphere( float3( actor._x, 0.5f, actor._y ), actor._radius, color );
		}
		for ( const Projectile& projectile : _listProjectile )
		{
			if ( projectile._bAlive == 0 )
				continue;
			dbg.drawSphere( float3( projectile._x, 0.4f, projectile._y ), projectile._radius, float4( 1.0f, 0.9f, 0.2f, 1.0f ) );
		}
	}

	AABB ActionRoom::Actor::bounds() const
	{
		return makeCircleAabb( _x, _y, _radius );
	}

	AABB ActionRoom::Projectile::bounds() const
	{
		return makeCircleAabb( _x, _y, _radius );
	}

	void ActionRoom::spawnGrunt( float32 x, float32 y )
	{
		Actor a{};
		a._kind	  = ActorKind::Grunt;
		a._x	  = x;
		a._y	  = y;
		a._hpMax  = 30.0f;
		a._hp	  = a._hpMax;
		a._radius = 0.32f;
		a._speed  = 1.8f;
		_listActor.push_back( a );
	}

	void ActionRoom::spawnBoss( float32 x, float32 y )
	{
		Actor a{};
		a._kind		   = ActorKind::Boss;
		a._x		   = x;
		a._y		   = y;
		a._hpMax	   = 220.0f;
		a._hp		   = a._hpMax;
		a._radius	   = 0.7f;
		a._speed	   = 0.9f;
		a._attackTimer = 1.2f;
		_bossMaxHp	   = a._hpMax;
		_listActor.push_back( a );
	}

	void ActionRoom::tryPlayerAttack( const ActionRoomFrameInput& input )
	{
		if ( _attackCooldown > 0.0f )
			return;
		if ( input._bAttackPressed == 0 )
			return;

		_attackCooldown = 0.28f;
		const AABB atk	= playerAttackBox( input._playerX, input._playerY, input._facing );
		for ( Actor& actor : _listActor )
		{
			if ( actor._bAlive == 0 )
				continue;
			if ( queryOverlaps( atk, kLayerPlayerAtk, actor.bounds(), kLayerEnemy, _layers ) == false )
				continue;
			const float32 dmg = ( actor._kind == ActorKind::Boss ) ? 18.0f : 34.0f;
			actor._hp -= dmg;
			if ( actor._hp <= 0.0f )
			{
				actor._hp	  = 0.0f;
				actor._bAlive = 0;
			}
		}
	}

	void ActionRoom::updateActors( float32 deltaTime, float32 playerX, float32 playerY )
	{
		for ( Actor& actor : _listActor )
		{
			if ( actor._bAlive == 0 )
				continue;

			float32 dx = playerX - actor._x;
			float32 dy = playerY - actor._y;
			normalize2( dx, dy );
			actor._x += dx * actor._speed * deltaTime;
			actor._y += dy * actor._speed * deltaTime;

			if ( actor._kind != ActorKind::Boss )
				continue;

			actor._attackTimer -= deltaTime;
			if ( actor._attackTimer > 0.0f )
				continue;
			actor._attackTimer = 1.6f;

			float32 vx = playerX - actor._x;
			float32 vy = playerY - actor._y;
			normalize2( vx, vy );
			Projectile projectile{};
			projectile._x	   = actor._x;
			projectile._y	   = actor._y;
			projectile._vx	   = vx * 4.5f;
			projectile._vy	   = vy * 4.5f;
			projectile._life   = 2.5f;
			projectile._radius = 0.22f;
			_listProjectile.push_back( projectile );

			// 패턴 아이디어: int16 방사 버스트 (두 번째 샷 오프셋)
			Projectile projectile2 = projectile;
			projectile2._vx		   = -vy * 3.2f;
			projectile2._vy		   = vx * 3.2f;
			projectile2._life	   = 1.8f;
			_listProjectile.push_back( projectile2 );
		}
	}

	void ActionRoom::updateProjectiles( float32 deltaTime )
	{
		for ( Projectile& projectile : _listProjectile )
		{
			if ( projectile._bAlive == 0 )
				continue;
			projectile._life -= deltaTime;
			if ( projectile._life <= 0.0f )
			{
				projectile._bAlive = 0;
				continue;
			}
			projectile._x += projectile._vx * deltaTime;
			projectile._y += projectile._vy * deltaTime;
		}

		_listProjectile.erase(
			std::remove_if( _listProjectile.begin(), _listProjectile.end(),
							[]( const Projectile& proj )
		{ return proj._bAlive == 0; } ),
			_listProjectile.end() );
	}

	void ActionRoom::resolvePlayerHits( float32 playerX, float32 playerY, ActionRoomFrameResult& out )
	{
		if ( _invulnTimer > 0.0f )
			return;

		const AABB hurt = playerHurtBox( playerX, playerY );
		for ( const Actor& actor : _listActor )
		{
			if ( actor._bAlive == 0 )
				continue;
			if ( queryOverlaps( hurt, kLayerPlayer, actor.bounds(), kLayerEnemy, _layers ) == false )
				continue;
			out._damageToPlayer += ( actor._kind == ActorKind::Boss ) ? 12 : 8;
			_invulnTimer = 0.7f;
			return;
		}
		for ( Projectile& projectile : _listProjectile )
		{
			if ( projectile._bAlive == 0 )
				continue;
			if ( queryOverlaps( hurt, kLayerPlayer, projectile.bounds(), kLayerProjectile, _layers ) == false )
				continue;
			out._damageToPlayer += 10;
			projectile._bAlive = 0;
			_invulnTimer	   = 0.7f;
			return;
		}
	}

	void ActionRoom::refreshCleared( ActionRoomFrameResult& out )
	{
		if ( _bCleared != 0 )
			return;
		if ( getAliveEnemyCount() > 0 )
			return;
		_bCleared			   = 1;
		out._bClearedThisFrame = 1;
		if ( _kind == ActionRoomKind::Boss )
			out._bBossDefeated = 1;
	}

	AABB ActionRoom::playerHurtBox( float32 x, float32 y ) const
	{
		return makeCircleAabb( x, y, 0.28f );
	}

	AABB ActionRoom::playerAttackBox( float32 x, float32 y, FacingDir facing ) const
	{
		float32 ox{ 0.0f };
		float32 oy{ 0.0f };
		switch ( facing )
		{
			case FacingDir::Up:
				oy = -0.85f;
				break;
			case FacingDir::Down:
				oy = 0.85f;
				break;
			case FacingDir::Left:
				ox = -0.85f;
				break;
			case FacingDir::Right:
				ox = 0.85f;
				break;
		}
		return makeCircleAabb( x + ox, y + oy, 0.45f );
	}
} // namespace sw
