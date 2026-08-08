/**
 * @file SaveGame.cpp
 */
#include "SaveGame.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include <cstdio>
#include <cstring>

namespace sw
{
	namespace
	{
		std::string findLineValue( const std::string& text, const char* key )
		{
			const size_t pos = text.find( key );
			if ( pos == std::string::npos )
				return {};
			// Require start-of-line (or file start) so "party0.x=" does not match "x=".
			if ( pos > 0 && text[pos - 1] != '\n' )
			{
				// Fall through to scan all occurrences.
				size_t search = 0;
				while ( true )
				{
					const size_t p = text.find( key, search );
					if ( p == std::string::npos )
						return {};
					if ( p == 0 || text[p - 1] == '\n' )
					{
						size_t start = p + std::strlen( key );
						size_t end	 = text.find( '\n', start );
						if ( end == std::string::npos )
							end = text.size();
						return text.substr( start, end - start );
					}
					search = p + 1;
				}
			}
			size_t start = pos + std::strlen( key );
			size_t end	 = text.find( '\n', start );
			if ( end == std::string::npos )
				end = text.size();
			return text.substr( start, end - start );
		}

		void appendPartyMember( std::string& out, size_t index, const PartyMember& m )
		{
			char line[512]{};
			std::snprintf( line, sizeof( line ),
						   "party%zu.speciesId=%s\n"
						   "party%zu.nickname=%s\n"
						   "party%zu.level=%d\n"
						   "party%zu.hp=%d\n"
						   "party%zu.hpMax=%d\n"
						   "party%zu.pp0=%d\n"
						   "party%zu.pp1=%d\n"
						   "party%zu.exp=%d\n",
						   index, m._speciesId.c_str(),
						   index, m._nickname.c_str(),
						   index, m._level,
						   index, m._hp,
						   index, m._hpMax,
						   index, m._pp0,
						   index, m._pp1,
						   index, m._exp );
			out += line;
		}
	} // namespace

	void SaveGame::clearParty()
	{
		_party.clear();
	}

	void SaveGame::setPartyFrom( const std::vector<PartyMember>& party )
	{
		_party.clear();
		const size_t n = party.size() < kMaxPartySize ? party.size() : kMaxPartySize;
		_party.assign( party.begin(), party.begin() + static_cast<std::ptrdiff_t>( n ) );
	}

	void SaveGame::ensureStarterParty()
	{
		if ( _party.empty() )
			_party.push_back( SpeciesCatalog::makeStarter() );
	}

	int32 SaveGame::getFlag( const std::string& key, int32 defaultValue ) const
	{
		const auto it = _flags.find( key );
		return it != _flags.end() ? it->second : defaultValue;
	}

	void SaveGame::setFlag( const std::string& key, int32 value )
	{
		_flags[key] = value;
	}

	bool SaveGame::saveToFile( const std::string& path ) const
	{
		std::string text;
		text.reserve( 1024 );

		char header[256]{};
		std::snprintf( header, sizeof( header ), "map=%s\nx=%d\ny=%d\npartyCount=%d\n",
					   _mapPath.c_str(), _playerX, _playerY, static_cast<int>( _party.size() ) );
		text += header;

		for ( size_t i = 0; i < _party.size() && i < kMaxPartySize; ++i )
			appendPartyMember( text, i, _party[i] );

		for ( const auto& kv : _flags )
		{
			char line[256]{};
			std::snprintf( line, sizeof( line ), "flag.%s=%d\n", kv.first.c_str(), kv.second );
			text += line;
		}

		const bool ok = FileUtil::writeFile( path, reinterpret_cast<const uint8*>( text.data() ),
											 static_cast<uint64>( text.size() ) );
		if ( ok )
			SW_LOG_INFO( "[SaveGame] Saved %# (party=%# flags=%#)", path, _party.size(), _flags.size() );
		return ok;
	}

	bool SaveGame::loadFromFile( const std::string& path )
	{
		std::vector<uint8> data;
		if ( FileUtil::readFile( path, data ) == false || data.empty() )
			return false;

		const std::string text( data.begin(), data.end() );

		const std::string map = findLineValue( text, "map=" );
		const std::string x	  = findLineValue( text, "x=" );
		const std::string y	  = findLineValue( text, "y=" );
		if ( map.empty() == false )
			_mapPath = map;
		if ( x.empty() == false )
			_playerX = std::atoi( x.c_str() );
		if ( y.empty() == false )
			_playerY = std::atoi( y.c_str() );

		_party.clear();
		const std::string countStr = findLineValue( text, "partyCount=" );
		int32			  count	   = countStr.empty() ? 0 : std::atoi( countStr.c_str() );
		if ( count < 0 )
			count = 0;
		if ( count > static_cast<int32>( kMaxPartySize ) )
			count = static_cast<int32>( kMaxPartySize );

		for ( int32 i = 0; i < count; ++i )
		{
			char keyBuf[64]{};
			PartyMember m{};

			std::snprintf( keyBuf, sizeof( keyBuf ), "party%d.speciesId=", i );
			const std::string sid = findLineValue( text, keyBuf );
			if ( sid.empty() == false )
				m._speciesId = sid;

			std::snprintf( keyBuf, sizeof( keyBuf ), "party%d.nickname=", i );
			const std::string nick = findLineValue( text, keyBuf );
			if ( nick.empty() == false )
				m._nickname = nick;

			std::snprintf( keyBuf, sizeof( keyBuf ), "party%d.level=", i );
			const std::string lv = findLineValue( text, keyBuf );
			if ( lv.empty() == false )
				m._level = std::atoi( lv.c_str() );

			std::snprintf( keyBuf, sizeof( keyBuf ), "party%d.hp=", i );
			const std::string hp = findLineValue( text, keyBuf );
			if ( hp.empty() == false )
				m._hp = std::atoi( hp.c_str() );

			std::snprintf( keyBuf, sizeof( keyBuf ), "party%d.hpMax=", i );
			const std::string hpMax = findLineValue( text, keyBuf );
			if ( hpMax.empty() == false )
				m._hpMax = std::atoi( hpMax.c_str() );

			std::snprintf( keyBuf, sizeof( keyBuf ), "party%d.pp0=", i );
			const std::string pp0 = findLineValue( text, keyBuf );
			if ( pp0.empty() == false )
				m._pp0 = std::atoi( pp0.c_str() );

			std::snprintf( keyBuf, sizeof( keyBuf ), "party%d.pp1=", i );
			const std::string pp1 = findLineValue( text, keyBuf );
			if ( pp1.empty() == false )
				m._pp1 = std::atoi( pp1.c_str() );

			std::snprintf( keyBuf, sizeof( keyBuf ), "party%d.exp=", i );
			const std::string exp = findLineValue( text, keyBuf );
			if ( exp.empty() == false )
				m._exp = std::atoi( exp.c_str() );

			if ( m._nickname.empty() )
				m._nickname = SpeciesCatalog::findSpecies( m._speciesId.c_str() )->_name;
			m._expNext = 40 + m._level * 10;
			_party.push_back( std::move( m ) );
		}

		if ( _party.empty() )
			ensureStarterParty();

		_flags.clear();
		size_t search = 0;
		const std::string flagPrefix = "flag.";
		while ( true )
		{
			const size_t p = text.find( flagPrefix, search );
			if ( p == std::string::npos )
				break;
			if ( p > 0 && text[p - 1] != '\n' )
			{
				search = p + 1;
				continue;
			}
			const size_t keyStart = p + flagPrefix.size();
			const size_t eq		  = text.find( '=', keyStart );
			const size_t lineEnd  = text.find( '\n', keyStart );
			if ( eq == std::string::npos || ( lineEnd != std::string::npos && eq > lineEnd ) )
			{
				search = p + 1;
				continue;
			}
			const std::string key = text.substr( keyStart, eq - keyStart );
			const size_t	  valEnd = ( lineEnd == std::string::npos ) ? text.size() : lineEnd;
			const std::string val = text.substr( eq + 1, valEnd - ( eq + 1 ) );
			if ( key.empty() == false )
				_flags[key] = std::atoi( val.c_str() );
			search = valEnd == text.size() ? valEnd : valEnd + 1;
		}

		SW_LOG_INFO( "[SaveGame] Loaded %# @ (%#,%#) party=%# flags=%#",
					 _mapPath, _playerX, _playerY, _party.size(), _flags.size() );
		return true;
	}
} // namespace sw
