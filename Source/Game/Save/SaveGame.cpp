/**
 * @file SaveGame.cpp
 */
#include "SaveGame.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	bool SaveGame::saveToFile( const std::string& path ) const
	{
		char buffer[256]{};
		std::snprintf( buffer, sizeof( buffer ), "map=%s\nx=%d\ny=%d\n", _mapPath.c_str(), _playerX, _playerY );
		const bool ok = FileUtil::writeFile( path, reinterpret_cast<const uint8*>( buffer ), static_cast<uint64>( std::strlen( buffer ) ) );
		if ( ok )
			SW_LOG_INFO( "[SaveGame] Saved to %#", path );
		return ok;
	}

	bool SaveGame::loadFromFile( const std::string& path )
	{
		std::vector<uint8> data;
		if ( FileUtil::readFile( path, data ) == false || data.empty() )
			return false;

		std::string text( data.begin(), data.end() );
		auto		findValue = [&]( const char* key ) -> std::string
		{
			const size_t pos = text.find( key );
			if ( pos == std::string::npos )
				return {};
			size_t start = pos + std::strlen( key );
			size_t end	 = text.find( '\n', start );
			if ( end == std::string::npos )
				end = text.size();
			return text.substr( start, end - start );
		};

		const std::string map = findValue( "map=" );
		const std::string x	  = findValue( "x=" );
		const std::string y	  = findValue( "y=" );
		if ( map.empty() == false )
			_mapPath = map;
		if ( x.empty() == false )
			_playerX = std::atoi( x.c_str() );
		if ( y.empty() == false )
			_playerY = std::atoi( y.c_str() );

		SW_LOG_INFO( "[SaveGame] Loaded %# @ (%#,%#)", _mapPath, _playerX, _playerY );
		return true;
	}
} // namespace sw
