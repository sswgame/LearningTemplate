#include "pch.h"

#include "Games/Demo/Actors/BossComponent.h"

#include "Engine/Object/Component/TagSystem.h"

#include "GameFramework/Data/UnitStatsComponent.h"

namespace sw
{
	BossComponent::BossComponent()
		: _phase{ 1 }
		, _stateTimer{ 0.0f }
		, _patternCooldown{ 0.0f }
		, _currentPattern{ 0 }
		, _bossState{ BossAiState::Idle }
		, _bLaserActive{ false }
		, _phase1Cooldown{ 0.0f }
		, _phase2Cooldown{ 0.0f }
		, _phase3Cooldown{ 0.0f }
		, _phase2HpRatio{ 0.0f }
		, _phase3HpRatio{ 0.0f }
	{
	}

	void BossComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::DuringPhysics );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
		{
			pOwner->addTag( "Boss"_tag );
			pOwner->addTag( "Monster"_tag );
		}
		_phase			= 1;
		_stateTimer		= 0.0f;
		_currentPattern = 0;
		_bossState		= BossAiState::Spawn;
		_bLaserActive	= false;
	}

	void BossComponent::onEndPlay()
	{
	}

	void BossComponent::onTick( float32 deltaTime )
	{
		_stateTimer += deltaTime;
		updatePhase();
		executePattern( deltaTime );
	}

	void BossComponent::updatePhase()
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
		{
			UnitStatsComponent* pStats = pOwner->getComponent<UnitStatsComponent>();
			if ( pStats != nullptr && pStats->getMaxHp() > 0 )
			{
				float32 hpRatio = static_cast<float32>( pStats->getHp() ) / static_cast<float32>( pStats->getMaxHp() );
				if ( hpRatio <= _phase3HpRatio )
				{
					_phase			 = 3;
					_patternCooldown = _phase3Cooldown;
				}
				else if ( hpRatio <= _phase2HpRatio )
				{
					_phase			 = 2;
					_patternCooldown = _phase2Cooldown;
				}
				else
				{
					_phase			 = 1;
					_patternCooldown = _phase1Cooldown;
				}
			}
		}
	}

	void BossComponent::executePattern( float32 deltaTime )
	{
		(void)deltaTime;
		if ( _stateTimer >= _patternCooldown )
		{
			_stateTimer		= 0.0f;
			_currentPattern = ( _currentPattern + 1 ) % 3;
			switch ( _currentPattern )
			{
				case 0:
					_bossState	  = BossAiState::Laser;
					_bLaserActive = true;
					break;
				case 1:
					_bossState	  = BossAiState::BulletHell;
					_bLaserActive = false;
					break;
				case 2:
					_bossState	  = BossAiState::SwordRain;
					_bLaserActive = false;
					break;
			}
		}
	}
} // namespace sw
