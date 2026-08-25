#include "pch.h"

#include "Games/Demo/Actors/BossComponent.h"

#include "Engine/Object/Component/TagSystem.h"

#include "GameFramework/Data/UnitStatsComponent.h"

namespace sw
{
	BossComponent::BossComponent()
		: phase{ 1 }
		, stateTimer{ 0.0f }
		, patternCooldown{ 0.0f }
		, currentPattern{ 0 }
		, bossState{ BossAiState::Idle }
		, bLaserActive{ false }
		, phase1Cooldown{ 0.0f }
		, phase2Cooldown{ 0.0f }
		, phase3Cooldown{ 0.0f }
		, phase2HpRatio{ 0.0f }
		, phase3HpRatio{ 0.0f }
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
		phase		   = 1;
		stateTimer	   = 0.0f;
		currentPattern = 0;
		bossState	   = BossAiState::Spawn;
		bLaserActive   = false;
	}

	void BossComponent::onEndPlay()
	{
	}

	void BossComponent::onTick( float32 deltaTime )
	{
		stateTimer += deltaTime;
		updatePhase();
		executePattern( deltaTime );
	}

	void BossComponent::updatePhase()
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
		{
			UnitStatsComponent* pStats = pOwner->getComponent<UnitStatsComponent>().get();
			if ( pStats != nullptr )
			{
				const UnitStatsData* pData = pStats->getStatsData();
				if ( pData != nullptr && pData->maxHp > 0 )
				{
					const float32 hpRatio = static_cast<float32>( pData->hp ) / static_cast<float32>( pData->maxHp );
					const bool	  bPhase3 = ( phase3HpRatio > 0.0f && hpRatio <= phase3HpRatio );
					const bool	  bPhase2 = ( phase2HpRatio > 0.0f && hpRatio <= phase2HpRatio );

					if ( bPhase3 )
					{
						phase			= 3;
						patternCooldown = phase3Cooldown;
					}
					else if ( bPhase2 )
					{
						phase			= 2;
						patternCooldown = phase2Cooldown;
					}
					else
					{
						phase			= 1;
						patternCooldown = phase1Cooldown;
					}
				}
			}
		}
	}

	void BossComponent::executePattern( float32 deltaTime )
	{
		(void)deltaTime;
		if ( stateTimer >= patternCooldown )
		{
			stateTimer	   = 0.0f;
			currentPattern = ( currentPattern + 1 ) % 3;
			switch ( currentPattern )
			{
				case 0:
					bossState	 = BossAiState::Laser;
					bLaserActive = true;
					break;
				case 1:
					bossState	 = BossAiState::BulletHell;
					bLaserActive = false;
					break;
				case 2:
					bossState	 = BossAiState::SwordRain;
					bLaserActive = false;
					break;
			}
		}
	}
} // namespace sw
