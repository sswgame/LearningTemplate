#include "pch.h"

#include "GameFramework/Kits/TurnBattle/SaveGame.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"

#include "Engine/Utility/File/KeyValueFile.h"

#include "GameFramework/Data/GameData.h"
#include "GameFramework/Kits/TurnBattle/SpeciesData.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	namespace
	{
		struct SaveGameInternal
		{
			/** @brief "party{i}.{field}" 문자열을 스택에 조립하여 반환합니다. */
			static fixed_string<constant::kMaxBuffer64> partyKey( int32 index, const utf8* pField )
			{
				fixed_string<constant::kMaxBuffer64> fs;
				fs.append( "party" );
				StringBuilder<constant::kMaxBuffer16> numSb;
				numSb.append( index );
				fs.append( numSb.c_str() );
				fs.append( "." );
				fs.append( pField );
				return fs;
			}

			static size_t partyCap()
			{
				const GameData* pData = game::getService<GameData>();
				const int32		n	  = pData != nullptr ? pData->_maxPartySize : 6;
				return n > 0 ? static_cast<size_t>( n ) : 6u;
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "SaveGame" );

	void SaveGame::clearParty()
	{
		_listParty.clear();
	}

	void SaveGame::setPartyFrom( const vector<PartyMember>& party )
	{
		_listParty.clear();
		const size_t n = party.size() < SaveGameInternal::partyCap() ? party.size() : SaveGameInternal::partyCap();
		_listParty.assign( party.begin(), party.begin() + static_cast<std::ptrdiff_t>( n ) );
	}

	void SaveGame::ensureStarterParty()
	{
		if ( _listParty.empty() )
		{
			const SpeciesCatalog* pCatalog = game::getService<SpeciesCatalog>();
			if ( pCatalog != nullptr )
				_listParty.push_back( pCatalog->makeStarter() );
			else
				_listParty.push_back( PartyMember{} );
		}
	}

	int32 SaveGame::getFlag( string_view key, int32 defaultValue ) const
	{
		const auto it = _mapFlag.find( string( key ) );
		return it != _mapFlag.end() ? it->second : defaultValue;
	}

	void SaveGame::setFlag( string_view key, int32 value )
	{
		_mapFlag[string( key )] = value;
	}

	bool SaveGame::saveToFile( string_view path ) const
	{
		StringBuilder<constant::kMaxBuffer2048> sb;
		sb.append( "map=" ).append( _mapPath.c_str() ).append( "\nx=" ).append( _playerX ).append( "\ny=" ).append( _playerY ).append( "\npartyCount=" ).append( static_cast<int32>( _listParty.size() ) ).append( '\n' );

		for ( size_t partyIndex = 0; partyIndex < _listParty.size() && partyIndex < SaveGameInternal::partyCap(); ++partyIndex )
		{
			const PartyMember& m = _listParty[partyIndex];
			sb.append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".speciesId=" ).append( m._speciesId.c_str() ).append( '\n' ).append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".nickname=" ).append( m._nickname.c_str() ).append( '\n' ).append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".level=" ).append( m._level ).append( '\n' ).append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".hp=" ).append( m._hp ).append( '\n' ).append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".hpMax=" ).append( m._hpMax ).append( '\n' ).append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".pp0=" ).append( m._pp0 ).append( '\n' ).append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".pp1=" ).append( m._pp1 ).append( '\n' ).append( "party" ).append( static_cast<int32>( partyIndex ) ).append( ".exp=" ).append( m._exp ).append( '\n' );
		}

		for ( const auto& [key, val] : _mapFlag )
		{
			sb.append( "flag." ).append( key.c_str() ).append( '=' ).append( val ).append( '\n' );
		}

		const string tempPath = string( path ) + ".tmp";
		if ( FileUtil::writeTextFile( tempPath, sb.view() ) == false )
		{
			SW_LOG_ERROR( "Failed to write temporary save file: %#", tempPath.c_str() );
			return false;
		}

		FileUtil::removeFile( path );
		const bool bCopied = FileUtil::copyFile( tempPath, path );
		FileUtil::removeFile( tempPath );

		if ( bCopied )
			SW_LOG_INFO( "Saved %# (party=%# flags=%#)", path, _listParty.size(), _mapFlag.size() );
		else
			SW_LOG_ERROR( "Failed to commit atomic save to %#", path );

		return bCopied;
	}

	bool SaveGame::loadFromFile( string_view path )
	{
		KeyValueMap map;
		if ( KeyValueFile::loadFile( path, map ) == false )
			return false;

		const utf8* pMapPath = KeyValueFile::get( map, "map", nullptr );
		if ( pMapPath != nullptr && pMapPath[0] != '\0' )
			_mapPath = pMapPath;
		_playerX = KeyValueFile::getInt( map, "x", _playerX );
		_playerY = KeyValueFile::getInt( map, "y", _playerY );

		_listParty.clear();
		int32 count = KeyValueFile::getInt( map, "partyCount", 0 );
		if ( count < 0 )
			count = 0;
		if ( count > static_cast<int32>( SaveGameInternal::partyCap() ) )
			count = static_cast<int32>( SaveGameInternal::partyCap() );

		for ( int32 itemIndex = 0; itemIndex < count; ++itemIndex )
		{
			PartyMember m{};
			const utf8* pSid = KeyValueFile::get( map, SaveGameInternal::partyKey( itemIndex, "speciesId" ).c_str(), nullptr );
			if ( pSid != nullptr && pSid[0] != '\0' )
				m._speciesId = pSid;
			const utf8* pNick = KeyValueFile::get( map, SaveGameInternal::partyKey( itemIndex, "nickname" ).c_str(), nullptr );
			if ( pNick != nullptr && pNick[0] != '\0' )
				m._nickname = pNick;
			m._level = KeyValueFile::getInt( map, SaveGameInternal::partyKey( itemIndex, "level" ).c_str(), m._level );
			m._hp	 = KeyValueFile::getInt( map, SaveGameInternal::partyKey( itemIndex, "hp" ).c_str(), m._hp );
			m._hpMax = KeyValueFile::getInt( map, SaveGameInternal::partyKey( itemIndex, "hpMax" ).c_str(), m._hpMax );
			m._pp0	 = KeyValueFile::getInt( map, SaveGameInternal::partyKey( itemIndex, "pp0" ).c_str(), m._pp0 );
			m._pp1	 = KeyValueFile::getInt( map, SaveGameInternal::partyKey( itemIndex, "pp1" ).c_str(), m._pp1 );
			m._exp	 = KeyValueFile::getInt( map, SaveGameInternal::partyKey( itemIndex, "exp" ).c_str(), m._exp );

			if ( m._nickname.empty() )
			{
				const SpeciesCatalog* pCatalog = game::getService<SpeciesCatalog>();
				const SpeciesDef*	  pDef	   = pCatalog != nullptr ? pCatalog->findSpecies( m._speciesId.c_str() ) : nullptr;
				m._nickname					   = pDef != nullptr ? pDef->_name : m._speciesId;
			}
			m._expNext = 40 + m._level * 10;
			_listParty.push_back( std::move( m ) );
		}

		if ( _listParty.empty() )
			ensureStarterParty();

		_mapFlag.clear();
		constexpr const utf8* kFlagPrefix = "flag.";
		const size_t		  prefixLen	  = StringUtil::strlen( kFlagPrefix );
		for ( const auto& [key, val] : map )
		{
			if ( key.size() > prefixLen && key.compare( 0, prefixLen, kFlagPrefix ) == 0 )
				_mapFlag[key.substr( prefixLen )] = StringUtil::atoi( val.c_str() );
		}

		SW_LOG_INFO( "Loaded %# @ (%#,%#) party=%# flags=%#",
					 _mapPath, _playerX, _playerY, _listParty.size(), _mapFlag.size() );
		return true;
	}
} // namespace sw
