#pragma once
/**
 * @file BattleState.h
 * @brief Minimal wild-encounter battle state machine (Fight / Run labels)
 */

#include "Core/Common/Types.h"

namespace sw
{
	enum class BattlePhase : uint8
	{
		Inactive = 0,
		Intro,
		PlayerChoice,
		Resolve,
		Ended
	};

	class BattleState
	{
	public:
		void startWildEncounter( const char* foeName = "Wild Critter" );
		void update( float32 deltaTime );
		void chooseFight();
		void chooseRun();
		void endBattle();

		bool		isActive() const { return _phase != BattlePhase::Inactive && _phase != BattlePhase::Ended; }
		BattlePhase getPhase() const { return _phase; }
		const char* getFoeName() const { return _foeName; }
		const char* getStatusText() const { return _statusText; }

	private:
		BattlePhase _phase = BattlePhase::Inactive;
		float32		_phaseTimer = 0.0f;
		char		_foeName[64]{};
		char		_statusText[128]{};
	};
} // namespace sw
