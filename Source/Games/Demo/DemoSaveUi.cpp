#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/GameEvents.h"
#include "GameFramework/Data/GameStrings.h"
#include "GameFramework/Kits/Overworld/ZoneRuntime.h"
#include "GameFramework/Kits/TurnBattle/SpeciesData.h"
#include "GameFramework/Transition/TransitionOrchestrator.h"

#include "Games/Demo/DemoGame.h"
#include "Games/Demo/DemoGameHelpers.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	namespace
	{
		struct DemoHotReloadInternal
		{
			static constexpr uint32 kMagic = 0x31485244u; // 'DHR1'

			static void appendBytes( vector<uint8>& bytes, const void* pSrc, size_t size )
			{
				const size_t oldSize = bytes.size();
				bytes.resize( oldSize + size );
				Memory::copy( bytes.data() + oldSize, pSrc, size );
			}

			static void writeU32( vector<uint8>& bytes, uint32 value )
			{
				appendBytes( bytes, &value, sizeof( uint32 ) );
			}

			static void writeI32( vector<uint8>& bytes, int32 value )
			{
				appendBytes( bytes, &value, sizeof( int32 ) );
			}

			static void writeU8( vector<uint8>& bytes, uint8 value )
			{
				appendBytes( bytes, &value, sizeof( uint8 ) );
			}

			static void writeString( vector<uint8>& bytes, string_view text )
			{
				const uint32 length = static_cast<uint32>( text.size() );
				writeU32( bytes, length );
				if ( length > 0 )
					appendBytes( bytes, text.data(), length );
			}

			static bool readBytes( const uint8*& pCursor, const uint8* pEnd, void* pOut, size_t size )
			{
				if ( pCursor + size > pEnd )
					return false;
				Memory::copy( pOut, pCursor, size );
				pCursor += size;
				return true;
			}

			static bool readU32( const uint8*& pCursor, const uint8* pEnd, uint32& outValue )
			{
				return readBytes( pCursor, pEnd, &outValue, sizeof( uint32 ) );
			}

			static bool readI32( const uint8*& pCursor, const uint8* pEnd, int32& outValue )
			{
				return readBytes( pCursor, pEnd, &outValue, sizeof( int32 ) );
			}

			static bool readU8( const uint8*& pCursor, const uint8* pEnd, uint8& outValue )
			{
				return readBytes( pCursor, pEnd, &outValue, sizeof( uint8 ) );
			}

			static bool readString( const uint8*& pCursor, const uint8* pEnd, string& outText )
			{
				uint32 length = 0;
				if ( readU32( pCursor, pEnd, length ) == false )
					return false;
				if ( pCursor + length > pEnd )
					return false;
				outText.assign( reinterpret_cast<const utf8*>( pCursor ), length );
				pCursor += length;
				return true;
			}

			static void writePartyMember( vector<uint8>& bytes, const PartyMember& member )
			{
				writeString( bytes, member._speciesId );
				writeString( bytes, member._nickname );
				writeI32( bytes, member._level );
				writeI32( bytes, member._hp );
				writeI32( bytes, member._hpMax );
				writeI32( bytes, member._pp0 );
				writeI32( bytes, member._pp1 );
				writeI32( bytes, member._exp );
				writeI32( bytes, member._expNext );
			}

			static bool readPartyMember( const uint8*& pCursor, const uint8* pEnd, PartyMember& outMember )
			{
				if ( readString( pCursor, pEnd, outMember._speciesId ) == false )
					return false;
				if ( readString( pCursor, pEnd, outMember._nickname ) == false )
					return false;
				if ( readI32( pCursor, pEnd, outMember._level ) == false )
					return false;
				if ( readI32( pCursor, pEnd, outMember._hp ) == false )
					return false;
				if ( readI32( pCursor, pEnd, outMember._hpMax ) == false )
					return false;
				if ( readI32( pCursor, pEnd, outMember._pp0 ) == false )
					return false;
				if ( readI32( pCursor, pEnd, outMember._pp1 ) == false )
					return false;
				if ( readI32( pCursor, pEnd, outMember._exp ) == false )
					return false;
				return readI32( pCursor, pEnd, outMember._expNext );
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "DemoGame" );

	void DemoGame::syncSaveFromWorld()
	{
		_save._mapPath = _currentMapPath;
		_save._playerX = _player.getTileX();
		_save._playerY = _player.getTileY();
		_save.setPartyFrom( _listParty );
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
		_listParty.clear();
		_listParty.push_back( _speciesCatalog.makeStarter( _data._starterId.c_str(), _data._starterLevel ) );
		_save.clearParty();
		_save.setPartyFrom( _listParty );
		_save.setFlag( "story_intro", 0 );
	}

	void DemoGame::applyPartyFromSave()
	{
		_listParty = _save._listParty;
		if ( _listParty.empty() )
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
		else if ( _actionRoom.isActive() && _listParty.empty() == false )
		{
			const PartyMember& leadMember = _listParty[0];
			_hud.setActionGauges( safeFill( leadMember._hp, leadMember._hpMax ),
								  _actionRoom.getBossHpFill(),
								  _actionRoom.getDashFill() );
			_hud.publishSnapshot( true );
			return;
		}
		else if ( _listParty.empty() == false )
		{
			const PartyMember& leadMember = _listParty[0];
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
			SW_LOG_TRACE( "Warp blocked — clear gate locked (zone=%#)", blockedEvent._zoneId );
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

	bool DemoGame::serializeState( void* pOutBuffer, uint32* pInOutSize )
	{
		if ( pInOutSize == nullptr )
			return false;

		syncSaveFromWorld();

		vector<uint8> extraBytes;
		DemoHotReloadInternal::writeU32( extraBytes, DemoHotReloadInternal::kMagic );
		DemoHotReloadInternal::writeU8( extraBytes, _bTitleHandedOff );
		DemoHotReloadInternal::writeU8( extraBytes, _bBattleReturnPending );
		DemoHotReloadInternal::writeI32( extraBytes, _returnPlayerX );
		DemoHotReloadInternal::writeI32( extraBytes, _returnPlayerY );
		DemoHotReloadInternal::writeI32( extraBytes, _save._playerX );
		DemoHotReloadInternal::writeI32( extraBytes, _save._playerY );
		DemoHotReloadInternal::writeString( extraBytes, _currentMapPath );
		DemoHotReloadInternal::writeString( extraBytes, _returnMapPath );
		DemoHotReloadInternal::writeString( extraBytes, _returnScenePath );
		DemoHotReloadInternal::writeU32( extraBytes, static_cast<uint32>( _listParty.size() ) );
		for ( const PartyMember& member : _listParty )
			DemoHotReloadInternal::writePartyMember( extraBytes, member );
		DemoHotReloadInternal::writeU32( extraBytes, static_cast<uint32>( _save._mapFlag.size() ) );
		for ( const auto& [flagKey, flagValue] : _save._mapFlag )
		{
			DemoHotReloadInternal::writeString( extraBytes, flagKey );
			DemoHotReloadInternal::writeI32( extraBytes, flagValue );
		}

		uint32 sceneSize = 0;
		if ( GameInstanceBase::serializeState( nullptr, &sceneSize ) == false )
			return false;
		vector<uint8> sceneBytes( sceneSize );
		if ( sceneSize > 0 && GameInstanceBase::serializeState( sceneBytes.data(), &sceneSize ) == false )
			return false;

		DemoHotReloadInternal::writeU32( extraBytes, sceneSize );
		if ( sceneSize > 0 )
			DemoHotReloadInternal::appendBytes( extraBytes, sceneBytes.data(), sceneSize );

		if ( pOutBuffer == nullptr )
		{
			*pInOutSize = static_cast<uint32>( extraBytes.size() );
			return true;
		}
		if ( *pInOutSize < extraBytes.size() )
			return false;
		Memory::copy( pOutBuffer, extraBytes.data(), extraBytes.size() );
		*pInOutSize = static_cast<uint32>( extraBytes.size() );
		return true;
	}

	bool DemoGame::deserializeState( const void* pInBuffer, uint32 size )
	{
		if ( pInBuffer == nullptr || size < sizeof( uint32 ) )
			return false;

		const uint8* pCursor = static_cast<const uint8*>( pInBuffer );
		const uint8* pEnd	 = pCursor + size;

		uint32 magic = 0;
		if ( DemoHotReloadInternal::readU32( pCursor, pEnd, magic ) == false )
			return false;
		if ( magic != DemoHotReloadInternal::kMagic )
			return GameInstanceBase::deserializeState( pInBuffer, size );

		uint8 titleHandedOff	  = 0;
		uint8 battleReturnPending = 0;
		if ( DemoHotReloadInternal::readU8( pCursor, pEnd, titleHandedOff ) == false )
			return false;
		if ( DemoHotReloadInternal::readU8( pCursor, pEnd, battleReturnPending ) == false )
			return false;
		if ( DemoHotReloadInternal::readI32( pCursor, pEnd, _returnPlayerX ) == false )
			return false;
		if ( DemoHotReloadInternal::readI32( pCursor, pEnd, _returnPlayerY ) == false )
			return false;
		int32 spawnX = 1;
		int32 spawnY = 1;
		if ( DemoHotReloadInternal::readI32( pCursor, pEnd, spawnX ) == false )
			return false;
		if ( DemoHotReloadInternal::readI32( pCursor, pEnd, spawnY ) == false )
			return false;
		if ( DemoHotReloadInternal::readString( pCursor, pEnd, _currentMapPath ) == false )
			return false;
		if ( DemoHotReloadInternal::readString( pCursor, pEnd, _returnMapPath ) == false )
			return false;
		if ( DemoHotReloadInternal::readString( pCursor, pEnd, _returnScenePath ) == false )
			return false;

		uint32 partyCount = 0;
		if ( DemoHotReloadInternal::readU32( pCursor, pEnd, partyCount ) == false )
			return false;
		_listParty.clear();
		_listParty.reserve( partyCount );
		for ( uint32 partyIndex = 0; partyIndex < partyCount; ++partyIndex )
		{
			PartyMember member{};
			if ( DemoHotReloadInternal::readPartyMember( pCursor, pEnd, member ) == false )
				return false;
			_listParty.push_back( member );
		}
		_save.setPartyFrom( _listParty );

		uint32 flagCount = 0;
		if ( DemoHotReloadInternal::readU32( pCursor, pEnd, flagCount ) == false )
			return false;
		_save._mapFlag.clear();
		for ( uint32 flagIndex = 0; flagIndex < flagCount; ++flagIndex )
		{
			string flagKey;
			int32  flagValue = 0;
			if ( DemoHotReloadInternal::readString( pCursor, pEnd, flagKey ) == false )
				return false;
			if ( DemoHotReloadInternal::readI32( pCursor, pEnd, flagValue ) == false )
				return false;
			_save.setFlag( flagKey, flagValue );
		}

		_bTitleHandedOff	  = titleHandedOff;
		_bBattleReturnPending = battleReturnPending;
		_save._mapPath		  = _currentMapPath;
		_save._playerX		  = spawnX;
		_save._playerY		  = spawnY;

		if ( _currentMapPath.empty() == false )
			loadMap( _currentMapPath, spawnX, spawnY );
		applyPartyFromSave();
		if ( _zones.getActiveRole() == ZoneRole::Gym && _save.getFlag( "clear_gate_unlocked", 0 ) != 0 )
			_zones.setClearGateLocked( false );

		uint32 sceneSize = 0;
		if ( DemoHotReloadInternal::readU32( pCursor, pEnd, sceneSize ) == false )
			return false;
		if ( pCursor + sceneSize > pEnd )
			return false;
		if ( sceneSize == 0 )
			return true;
		return GameInstanceBase::deserializeState( pCursor, sceneSize );
	}
} // namespace sw
