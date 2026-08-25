/**
 * @file ActionRoom.h
 * @brief 던전 / 보스 룸용 실시간 클리어 게이트 전투 (던그리드 스타일 아이디어).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Physics/AABB.h"
#include "Engine/Physics/CollisionLayers.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/GameFrameworkMinimal.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) 룸 종류 · 프레임 입출력
	//    Hall=잡몹, Boss=보스. None이면 비활성
	// ------------------------------------------------------------------------------
	/** @brief 액션 룸 전투 종류 */
	enum class ActionRoomKind : uint8
	{
		None = 0,
		Hall,
		Boss
	};

	/** @brief 한 프레임 플레이어 입력 (위치·방향·공격/대시) */
	struct ActionRoomFrameInput
	{
		float32				   _playerX{ 0.0f };
		float32				   _playerY{ 0.0f };
		FacingDir			   _facing = FacingDir::Right;
		uint8				   _bAttackPressed : 1;
		uint8				   _bDashPressed   : 1;
		[[maybe_unused]] uint8 _reserved	   : 6;

		/** @brief 공격·대시 비트를 0으로 둡니다. */
		ActionRoomFrameInput()
			: _bAttackPressed{ 0 }
			, _bDashPressed{ 0 }
			, _reserved{ 0 } {}
	};

	/** @brief 한 프레임 전투 결과 (피격·클리어·대시 시작) */
	struct ActionRoomFrameResult
	{
		int32				   _damageToPlayer{ 0 }; ///< 이번 프레임 플레이어 피해
		uint8				   _bClearedThisFrame : 1;
		uint8				   _bBossDefeated	  : 1;
		uint8				   _bDashStarted	  : 1;
		[[maybe_unused]] uint8 _reserved		  : 5;

		/** @brief 클리어·보스·대시 비트를 0으로 둡니다. */
		ActionRoomFrameResult()
			: _bClearedThisFrame{ 0 }
			, _bBossDefeated{ 0 }
			, _bDashStarted{ 0 }
			, _reserved{ 0 } {}
	};

	// ------------------------------------------------------------------------------
	// 2) ActionRoom — 적/투사체 스폰, 클리어 시 게이트 개방
	// ------------------------------------------------------------------------------
	/** @brief 적·보스 투사체를 스폰하고, 클리어 시 클리어 게이트를 엽니다. */
	class SW_GF_API ActionRoom
	{
	public:
		/** @brief 비활성(None)·게이트 닫힘으로 시작합니다. */
		ActionRoom();

		/** @brief 룸 상태와 액터를 비웁니다. */
		void clear();
		/** @brief 입구 연출을 시작합니다. */
		void beginEntrance();
		/** @brief 홀 전투를 시작합니다. */
		void beginHall();
		/** @brief 보스 전투를 시작합니다. */
		void beginBoss();

		/** @brief 룸이 활성인지 반환합니다. */
		bool isActive() const { return _kind != ActionRoomKind::None; }
		/** @brief 룸 종류를 반환합니다. */
		ActionRoomKind getKind() const { return _kind; }
		/** @brief 클리어 여부를 반환합니다. */
		bool isCleared() const { return _bCleared != 0; }
		/** @brief 플레이어 무적 여부를 반환합니다. */
		bool isPlayerInvulnerable() const { return _invulnTimer > 0.0f; }
		/** @brief 대시 쿨다운 게이지(0~1)를 반환합니다. */
		float32 getDashFill() const;
		/** @brief 보스 HP 게이지(0~1)를 반환합니다. */
		float32 getBossHpFill() const;
		/** @brief 살아있는 적 수를 반환합니다. */
		int32 getAliveEnemyCount() const;

		/** @brief 한 프레임 전투를 갱신합니다. */
		ActionRoomFrameResult update( float32 deltaTime, const ActionRoomFrameInput& input );
		/** @brief 디버그 오버레이를 그립니다. */
		void drawDebug() const;

	private:
		/** @brief 적 액터 종류 */
		enum class ActorKind : uint8
		{
			Grunt = 0,
			Boss
		};

		/** @brief 적 위치·HP·공격 타이머 */
		struct Actor
		{
			ActorKind			   _kind;
			float32				   _x;
			float32				   _y;
			float32				   _hp;
			float32				   _hpMax;
			float32				   _radius;
			float32				   _speed;
			float32				   _attackTimer; ///< 다음 투사체까지
			uint8				   _bAlive	 : 1;
			[[maybe_unused]] uint8 _reserved : 7;

			/** @brief 살아 있는 상태로 둡니다. */
			Actor()
				: _kind{ ActorKind::Grunt }
				, _x{ 0.0f }
				, _y{ 0.0f }
				, _hp{ 1.0f }
				, _hpMax{ 1.0f }
				, _radius{ 0.35f }
				, _speed{ 1.6f }
				, _attackTimer{ 0.0f }
				, _bAlive{ 1 }
				, _reserved{ 0 }
			{
			}

			/** @brief 액터 AABB를 반환합니다. */
			AABB bounds() const;
		};

		/** @brief 적 투사체 */
		struct Projectile
		{
			float32				   _x;
			float32				   _y;
			float32				   _vx;
			float32				   _vy;
			float32				   _life; ///< 남은 수명(초)
			float32				   _radius;
			uint8				   _bAlive	 : 1;
			[[maybe_unused]] uint8 _reserved : 7;

			/** @brief 살아 있는 상태로 둡니다. */
			Projectile()
				: _x{ 0.0f }
				, _y{ 0.0f }
				, _vx{ 0.0f }
				, _vy{ 0.0f }
				, _life{ 0.0f }
				, _radius{ 0.2f }
				, _bAlive{ 1 }
				, _reserved{ 0 }
			{
			}

			/** @brief 투사체 AABB를 반환합니다. */
			AABB bounds() const;
		};

		/** @brief 그런트를 스폰합니다. */
		void spawnGrunt( float32 x, float32 y );
		/** @brief 보스를 스폰합니다. */
		void spawnBoss( float32 x, float32 y );
		/** @brief 플레이어 공격을 시도합니다. */
		void tryPlayerAttack( const ActionRoomFrameInput& input );
		/** @brief 액터를 갱신합니다. */
		void updateActors( float32 deltaTime, float32 playerX, float32 playerY );
		/** @brief 투사체를 갱신합니다. */
		void updateProjectiles( float32 deltaTime );
		/** @brief 플레이어 피격을 처리합니다. */
		void resolvePlayerHits( float32 playerX, float32 playerY, ActionRoomFrameResult& out );
		/** @brief 클리어 상태를 갱신합니다. */
		void refreshCleared( ActionRoomFrameResult& out );
		/** @brief 플레이어 피격 박스를 반환합니다. */
		AABB playerHurtBox( float32 x, float32 y ) const;
		/** @brief 플레이어 공격 박스를 반환합니다. */
		AABB playerAttackBox( float32 x, float32 y, FacingDir facing ) const;

		static constexpr uint8 kLayerPlayer		= 0; ///< 플레이어 히트
		static constexpr uint8 kLayerEnemy		= 1; ///< 적 히트
		static constexpr uint8 kLayerPlayerAtk	= 2; ///< 플레이어 공격
		static constexpr uint8 kLayerProjectile = 3; ///< 적 투사체

		ActionRoomKind		   _kind;
		CollisionLayers		   _layers;
		vector<Actor>		   _listActors;
		vector<Projectile>	   _listProjectiles;
		float32				   _attackCooldown;
		float32				   _dashCooldown;
		float32				   _invulnTimer; ///< 피격 후 무적
		float32				   _bossMaxHp;
		uint8				   _bCleared : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};
} // namespace sw
