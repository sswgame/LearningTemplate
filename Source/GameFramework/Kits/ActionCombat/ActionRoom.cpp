#include "pch.h"

#include "GameFramework/Kits/ActionCombat/ActionRoom.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	ActionRoom::ActionRoom()
		: _kind{ ActionRoomKind::None }
		, _layers{}
		, _listActor{}
		, _listProjectile{}
		, _attackCooldown{ 0.0f }
		, _dashCooldown{ 0.0f }
		, _invulnTimer{ 0.0f }
		, _bossMaxHp{ 1.0f }
		, _bCleared{ SW_FALSE }
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
		_bCleared		= SW_FALSE;
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
		spawnBoss( 7.0f, 4.0f );
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
			if ( actor._kind == ActorKind::Boss && actor._bAlive == SW_TRUE )
				return MathUtil::saturate( actor._hp / _bossMaxHp );
		}
		return 0.0f;
	}

	int32 ActionRoom::getAliveEnemyCount() const
	{
		int32 count = 0;
		for ( const Actor& actor : _listActor )
		{
			if ( actor._bAlive == SW_TRUE )
				++count;
		}
		return count;
	}

	ActionRoomFrameResult ActionRoom::update( float32 deltaTime, const ActionRoomFrameInput& input )
	{
		ActionRoomFrameResult result{};
		if ( _kind == ActionRoomKind::None )
			return result;

		_attackCooldown = MathUtil::max( 0.0f, _attackCooldown - deltaTime );
		_dashCooldown	= MathUtil::max( 0.0f, _dashCooldown - deltaTime );
		_invulnTimer	= MathUtil::max( 0.0f, _invulnTimer - deltaTime );

		if ( input._bDashPressed == SW_TRUE && _dashCooldown <= 0.0f )
		{
			_dashCooldown		 = 0.85f;
			_invulnTimer		 = 0.22f;
			result._bDashStarted = SW_TRUE;
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
			if ( actor._bAlive == SW_FALSE )
				continue;
			const float4 color = ( actor._kind == ActorKind::Boss )
								   ? float4( 1.0f, 0.25f, 0.2f, 1.0f )
								   : float4( 1.0f, 0.55f, 0.2f, 1.0f );
			dbg.drawSphere( float3( actor._x, 0.5f, actor._y ), actor._radius, color );
		}
		for ( const Projectile& projectile : _listProjectile )
		{
			if ( projectile._bAlive == SW_FALSE )
				continue;
			dbg.drawSphere( float3( projectile._x, 0.4f, projectile._y ), projectile._radius, float4( 1.0f, 0.9f, 0.2f, 1.0f ) );
		}
	}

	AABB ActionRoom::Actor::bounds() const
	{
		return AABB{
			float3{_x - _radius, 0.0f, _y - _radius},
			float3{_x + _radius, 1.0f, _y + _radius}
		   };
	}

	AABB ActionRoom::Projectile::bounds() const
	{
		return AABB{
			float3{_x - _radius, 0.0f, _y - _radius},
			float3{_x + _radius, 1.0f, _y + _radius}
		   };
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
		a._bAlive = SW_TRUE;
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
		a._bAlive	   = SW_TRUE;
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
			if ( actor._bAlive == SW_FALSE )
				continue;
			if ( queryOverlaps( atk, kLayerPlayerAtk, actor.bounds(), kLayerEnemy, _layers ) == false )
				continue;
			const float32 dmg = ( actor._kind == ActorKind::Boss ) ? 18.0f : 34.0f;
			actor._hp -= dmg;
			if ( actor._hp <= 0.0f )
			{
				actor._hp	  = 0.0f;
				actor._bAlive = SW_FALSE;
			}
		}
	}

	void ActionRoom::updateActors( float32 deltaTime, float32 playerX, float32 playerY )
	{
		for ( Actor& actor : _listActor )
		{
			if ( actor._bAlive == SW_FALSE )
				continue;

			const float2 toPlayer = float2{ playerX - actor._x, playerY - actor._y }.normalize();
			actor._x += toPlayer._x * actor._speed * deltaTime;
			actor._y += toPlayer._y * actor._speed * deltaTime;

			if ( actor._kind != ActorKind::Boss )
				continue;

			actor._attackTimer -= deltaTime;
			if ( actor._attackTimer > 0.0f )
				continue;
			actor._attackTimer = 1.6f;

			const float2 projDir = float2{ playerX - actor._x, playerY - actor._y }.normalize();

			Projectile projectile{};
			projectile._x	   = actor._x;
			projectile._y	   = actor._y;
			projectile._vx	   = projDir._x * 4.5f;
			projectile._vy	   = projDir._y * 4.5f;
			projectile._life   = 2.5f;
			projectile._radius = 0.22f;
			projectile._bAlive = SW_TRUE;
			_listProjectile.push_back( projectile );

			Projectile projectile2 = projectile;
			projectile2._vx		   = -projDir._y * 3.2f;
			projectile2._vy		   = projDir._x * 3.2f;
			projectile2._life	   = 1.8f;
			_listProjectile.push_back( projectile2 );
		}
	}

	void ActionRoom::updateProjectiles( float32 deltaTime )
	{
		for ( Projectile& projectile : _listProjectile )
		{
			if ( projectile._bAlive == SW_FALSE )
				continue;
			projectile._x += projectile._vx * deltaTime;
			projectile._y += projectile._vy * deltaTime;
			projectile._life -= deltaTime;
			if ( projectile._life <= 0.0f )
				projectile._bAlive = SW_FALSE;
		}

		_listProjectile.erase(
			std::remove_if( _listProjectile.begin(), _listProjectile.end(),
							[]( const Projectile& proj )
		{ return proj._bAlive == SW_FALSE; } ),
			_listProjectile.end() );
	}

	void ActionRoom::resolvePlayerHits( float32 playerX, float32 playerY, ActionRoomFrameResult& out )
	{
		if ( _invulnTimer > 0.0f )
			return;

		const AABB hurt = playerHurtBox( playerX, playerY );
		for ( const Actor& actor : _listActor )
		{
			if ( actor._bAlive == SW_FALSE )
				continue;
			if ( queryOverlaps( hurt, kLayerPlayer, actor.bounds(), kLayerEnemy, _layers ) == false )
				continue;
			out._damageToPlayer += ( actor._kind == ActorKind::Boss ) ? 12 : 8;
			_invulnTimer = 0.7f;
			return;
		}
		for ( Projectile& projectile : _listProjectile )
		{
			if ( projectile._bAlive == SW_FALSE )
				continue;
			if ( queryOverlaps( hurt, kLayerPlayer, projectile.bounds(), kLayerProjectile, _layers ) == false )
				continue;
			out._damageToPlayer += 10;
			projectile._bAlive = SW_FALSE;
			_invulnTimer	   = 0.7f;
			return;
		}
	}

	void ActionRoom::refreshCleared( ActionRoomFrameResult& out )
	{
		if ( _bCleared == SW_TRUE )
			return;
		if ( getAliveEnemyCount() > 0 )
			return;
		_bCleared			   = SW_TRUE;
		out._bClearedThisFrame = SW_TRUE;
		if ( _kind == ActionRoomKind::Boss )
			out._bBossDefeated = SW_TRUE;
	}

	AABB ActionRoom::playerHurtBox( float32 x, float32 y ) const
	{
		constexpr float32 radius = 0.28f;
		return AABB{
			float3{x - radius, 0.0f, y - radius},
			float3{x + radius, 1.0f, y + radius}
		   };
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
			default:
				break;
		}
		constexpr float32 radius  = 0.45f;
		const float32	  centerX = x + ox;
		const float32	  centerY = y + oy;
		return AABB{
			float3{centerX - radius, 0.0f, centerY - radius},
			float3{centerX + radius, 1.0f, centerY + radius}
		   };
	}
} // namespace sw
