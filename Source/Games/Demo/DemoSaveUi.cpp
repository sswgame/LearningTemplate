#include "pch.h"

#include "Core/Event/EventDispatcher.h"

#include "GameFramework/Base/GameEvents.h"
#include "GameFramework/Data/GameStrings.h"
#include "GameFramework/Kits/Overworld/ZoneRuntime.h"
#include "GameFramework/Kits/TurnBattle/SpeciesData.h"
#include "GameFramework/Transition/TransitionOrchestrator.h"

#include "Games/Demo/DemoGame.h"
#include "Games/Demo/DemoGameHelpers.h"

#include "RuntimeAPI/GameService.h"

namespace sw
{
	void DemoGame::syncSaveFromWorld()
	{
		_save._mapPath = _currentMapPath;
		_save._playerX = _player.getTileX();
		_save._playerY = _player.getTileY();
		_save.setPartyFrom( _partyList );
	}

	bool DemoGame::applySaveToWorld()
	{
		applyPartyFromSave();
		if ( loadMap( _save._mapPath, _save._playerX, _save._playerY ) == false )
			return false;
		// 체육관형 게이트만 해제를 유지합니다. 던전 룸은 입장 시 다시 잠급니다.
		if ( _zones.getActiveRole() == ZoneRole::Gym && _save.getFlag( "clear_gate_unlocked", 0 ) != 0 )
			_zones.setClearGateLocked( false );
		return true;
	}

	void DemoGame::initNewGameParty()
	{
		_partyList.clear();
		_partyList.push_back( SpeciesCatalog::makeStarter( _data._starterId.c_str(), _data._starterLevel ) );
		_save.clearParty();
		_save.setPartyFrom( _partyList );
		_save.setFlag( "story_intro", 0 );
	}

	void DemoGame::applyPartyFromSave()
	{
		_partyList = _save._listParty;
		if ( _partyList.empty() )
			initNewGameParty();
	}

	void DemoGame::updateHud()
	{
		_hud.setFadeOverlay( _transitions.fade().getOverlayAlpha() );

		if ( _battle.isActive() )
		{
			const PartyMember& playerMember = _battle.player();
			const PartyMember& foeMember	= _battle.foe();
			_hud.setBattleGauges( safeFill( playerMember._hp, playerMember._hpMax ),
								  safeFill( foeMember._hp, foeMember._hpMax ),
								  safeFill( playerMember._exp, playerMember._expNext > 0 ? playerMember._expNext : 1 ),
								  safeFill( playerMember._pp0, 35 ) );
			_hud.setDialogue( _battle.getStatusText() );
		}
		else if ( _actionRoom.isActive() && _partyList.empty() == false )
		{
			const PartyMember& leadMember = _partyList[0];
			_hud.setActionGauges( safeFill( leadMember._hp, leadMember._hpMax ),
								  _actionRoom.getBossHpFill(),
								  _actionRoom.getDashFill() );
			_hud.publishSnapshot( true );
			return;
		}
		else if ( _partyList.empty() == false )
		{
			const PartyMember& leadMember = _partyList[0];
			_hud.setBattleGauges( safeFill( leadMember._hp, leadMember._hpMax ), 0.0f,
								  safeFill( leadMember._exp, leadMember._expNext > 0 ? leadMember._expNext : 1 ),
								  safeFill( leadMember._pp0, 35 ) );
		}
		_hud.publishSnapshot( _battle.isActive() );
	}

	void DemoGame::beginWarpTransition( string_view mapPath, int32 spawnX, int32 spawnY )
	{
		if ( _zones.isClearGateLocked() )
		{
			const ZoneDef*	 pZone = _zones.getActiveZone();
			WarpBlockedEvent blockedEvent{};
			blockedEvent._zoneId = pZone != nullptr ? pZone->_id : "?";
			blockedEvent._reason = "clear_gate_locked";
			game::getService<EventDispatcher>()->publish( gameEventChannel(), blockedEvent );
			SW_LOG_INFO( "[DemoGame] Warp blocked — clear gate locked (zone=%#)", blockedEvent._zoneId );
			_hud.setDialogue( GameStrings::get( "ui.path_sealed", "The path is sealed..." ) );
			return;
		}
		WarpRequestedEvent requestedEvent{};
		requestedEvent._mapPath = mapPath;
		requestedEvent._spawnX	= spawnX;
		requestedEvent._spawnY	= spawnY;
		game::getService<EventDispatcher>()->publish( gameEventChannel(), requestedEvent );
		_transitions.beginWarp( mapPath, spawnX, spawnY );
	}

	void DemoGame::wireTransitionCallbacks()
	{
		TransitionCallbacks callbacks{};
		callbacks.loadMap = [this]( string_view mapPath, int32 spawnX, int32 spawnY ) -> bool
		{
			const bool		   bSuccess = loadMap( mapPath, spawnX, spawnY );
			WarpCompletedEvent doneEvent{};
			doneEvent._mapPath = mapPath;
			doneEvent._spawnX  = spawnX;
			doneEvent._spawnY  = spawnY;
			game::getService<EventDispatcher>()->publish( gameEventChannel(), doneEvent );
			return bSuccess;
		};
		callbacks.startBattle = [this]()
		{ startBattleLoad(); };
		callbacks.finishBattleReturn = [this]()
		{ finishBattleReturnLoad(); };
		callbacks.setPlayerInputEnabled = [this]( bool bEnable )
		{ _player.setInputEnabled( bEnable ); };
		_transitions.setCallbacks( std::move( callbacks ) );
	}
} // namespace sw
