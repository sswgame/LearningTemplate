#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/File/KeyValueFile.h"

#include "GameFramework/Save/ISaveGame.h"

namespace sw
{
	namespace
	{
		constexpr uint32 kSaveBinMagic	 = 0x53415631u; // 'SAV1'
		constexpr uint32 kSaveBinVersion = 0;

		bool readU32Val( const vector<uint8>& listBlob, size_t& offset, uint32& outValue )
		{
			if ( offset + 4 > listBlob.size() )
				return false;
			outValue = static_cast<uint32>( listBlob[offset] ) | ( static_cast<uint32>( listBlob[offset + 1] ) << 8 ) |
					   ( static_cast<uint32>( listBlob[offset + 2] ) << 16 ) | ( static_cast<uint32>( listBlob[offset + 3] ) << 24 );
			offset += 4;
			return true;
		}

		bool readI32Val( const vector<uint8>& listBlob, size_t& offset, int32& outValue )
		{
			uint32 val{ 0 };
			if ( readU32Val( listBlob, offset, val ) == false )
				return false;
			outValue = static_cast<int32>( val );
			return true;
		}

		bool readStrVal( const vector<uint8>& listBlob, size_t& offset, string& outString )
		{
			uint32 len{ 0 };
			if ( readU32Val( listBlob, offset, len ) == false || offset + len > listBlob.size() )
				return false;
			outString.assign( reinterpret_cast<const utf8*>( listBlob.data() + offset ), len );
			offset += len;
			return true;
		}

		void appendU32Val( vector<uint8>& listBlob, uint32 value )
		{
			listBlob.push_back( static_cast<uint8>( value & 0xFFu ) );
			listBlob.push_back( static_cast<uint8>( ( value >> 8 ) & 0xFFu ) );
			listBlob.push_back( static_cast<uint8>( ( value >> 16 ) & 0xFFu ) );
			listBlob.push_back( static_cast<uint8>( ( value >> 24 ) & 0xFFu ) );
		}

		void appendI32Val( vector<uint8>& listBlob, int32 value )
		{
			appendU32Val( listBlob, static_cast<uint32>( value ) );
		}

		void appendStrVal( vector<uint8>& listBlob, string_view str )
		{
			appendU32Val( listBlob, static_cast<uint32>( str.size() ) );
			if ( str.empty() == false )
			{
				const uint8* pBytes = reinterpret_cast<const uint8*>( str.data() );
				listBlob.insert( listBlob.end(), pBytes, pBytes + str.size() );
			}
		}
	} // namespace

	/**
	 * @brief 세이브 슬롯의 게임플레이 플래그 값을 정수로 조회합니다. (없을 시 defaultValue 반환)
	 */
	int32 SaveSlot::getFlag( string_view key, int32 defaultValue ) const
	{
		const auto it = _mapFlags.find( string( key ) );
		if ( it == _mapFlags.end() )
			return defaultValue;
		return it->second;
	}

	/**
	 * @brief 게임플레이 플래그 값을 설정합니다.
	 */
	void SaveSlot::setFlag( string_view key, int32 value )
	{
		_mapFlags[string( key )] = value;
	}

	/**
	 * @brief 세이브 슬롯의 기본 정보(맵 경로, 플레이어 좌표, 플래그들)를 텍스트 파일로 저장합니다.
	 */
	bool SaveSlot::saveCommonToTextFile( string_view path ) const
	{
		StringBuilder<constant::kMaxBuffer1024> sb;

		BLOCK( "Write Header" )
		{
			sb.append( "map=" ).append( _mapPath.c_str() ).append( "\nx=" ).append( _playerX ).append( "\ny=" ).append( _playerY ).append( '\n' );
		}

		BLOCK( "Write Flags" )
		{
			for ( const auto& [key, val] : _mapFlags )
			{
				sb.append( "flag." ).append( key.c_str() ).append( '=' ).append( val ).append( '\n' );
			}
		}

		BLOCK( "Write Checksum" )
		{
			const uint32 checksum = StringUtil::computeHash32( sb.view().data(), sb.view().size(), false );
			sb.append( "checksum=" ).append( checksum ).append( '\n' );
		}

		BLOCK( "Save To File" )
		{
			FileUtil::createDirectory( path );
			const bool ok = FileUtil::writeTextFile( path, sb.view() );
			if ( ok )
				SW_LOG_INFO( "[SaveSlot] Saved text %#", path );
			return ok;
		}
	}

	/**
	 * @brief 세이브 파일로부터 맵 경로, 좌표, 게임플레이 플래그들을 텍스트 역직렬화 로드합니다.
	 */
	bool SaveSlot::loadCommonFromTextFile( string_view path )
	{
		KeyValueMap map;
		BLOCK( "Load KeyValue File" )
		{
			if ( KeyValueFile::loadFile( path, map ) == false )
			{
				SW_LOG_WARNING( "[SaveSlot] Failed to load %#", path );
				return false;
			}
		}

		BLOCK( "Verify Checksum If Present" )
		{
			const utf8* pChecksumStr = KeyValueFile::get( map, "checksum", nullptr );
			if ( pChecksumStr != nullptr && pChecksumStr[0] != '\0' )
			{
				StringBuilder<constant::kMaxBuffer1024> verifySb;
				const utf8*								pMap = KeyValueFile::get( map, "map", "" );
				const int32								px	 = KeyValueFile::getInt( map, "x", 0 );
				const int32								py	 = KeyValueFile::getInt( map, "y", 0 );
				verifySb.append( "map=" ).append( pMap ).append( "\nx=" ).append( px ).append( "\ny=" ).append( py ).append( '\n' );
				for ( const auto& [key, val] : map )
				{
					if ( key.rfind( "flag.", 0 ) == 0 )
					{
						verifySb.append( key.c_str() ).append( '=' ).append( val.c_str() ).append( '\n' );
					}
				}
				const uint32 expectedHash = static_cast<uint32>( StringUtil::strtoull( pChecksumStr, nullptr, 10 ) );
				const uint32 computedHash = StringUtil::computeHash32( verifySb.view().data(), verifySb.view().size(), false );
				if ( expectedHash != computedHash )
				{
					SW_LOG_ERROR( "[SaveSlot] Checksum mismatch in %# (expected %# != computed %#) — save corrupted!", path, expectedHash, computedHash );
					return false;
				}
			}
		}

		BLOCK( "Parse Basic Info" )
		{
			const utf8* pMapPath = KeyValueFile::get( map, "map", nullptr );
			if ( pMapPath != nullptr && pMapPath[0] != '\0' )
				_mapPath = pMapPath;
			_playerX = KeyValueFile::getInt( map, "x", _playerX );
			_playerY = KeyValueFile::getInt( map, "y", _playerY );
		}

		BLOCK( "Parse Flags" )
		{
			_mapFlags.clear();
			constexpr const utf8* kFlagPrefix = "flag.";
			const size_t		  prefixLen	  = StringUtil::strlen( kFlagPrefix );
			for ( const auto& [key, val] : map )
			{
				if ( key.size() > prefixLen && key.compare( 0, prefixLen, kFlagPrefix ) == 0 )
					_mapFlags[key.substr( prefixLen )] = StringUtil::atoi( val.c_str() );
			}
		}

		SW_LOG_INFO( "[SaveSlot] Loaded text %# (map=%#, pos=%#,%#)", path, _mapPath, _playerX, _playerY );
		return true;
	}

	/**
	 * @brief 세이브 슬롯 데이터를 SAV1 바이너리 포맷으로 직렬화하여 저장합니다.
	 */
	bool SaveSlot::saveCommonToBinaryFile( string_view path ) const
	{
		vector<uint8> listPayload;
		appendStrVal( listPayload, _mapPath );
		appendI32Val( listPayload, _playerX );
		appendI32Val( listPayload, _playerY );
		appendU32Val( listPayload, static_cast<uint32>( _mapFlags.size() ) );

		for ( const auto& [key, val] : _mapFlags )
		{
			appendStrVal( listPayload, key );
			appendI32Val( listPayload, val );
		}

		const uint32 crc = StringUtil::computeCrc32( listPayload.data(), listPayload.size() );

		vector<uint8> listBlob;
		listBlob.reserve( 16 + listPayload.size() );
		appendU32Val( listBlob, kSaveBinMagic );
		appendU32Val( listBlob, kSaveBinVersion );
		appendU32Val( listBlob, crc );
		appendU32Val( listBlob, static_cast<uint32>( listPayload.size() ) );
		listBlob.insert( listBlob.end(), listPayload.begin(), listPayload.end() );

		FileUtil::createDirectory( path );
		const bool ok = FileUtil::writeFile( path, listBlob.data(), listBlob.size() );
		if ( ok )
			SW_LOG_INFO( "[SaveSlot] Saved binary (SAV1, CRC32=0x%#08X) -> %#", crc, path );
		return ok;
	}

	/**
	 * @brief SAV1 바이너리 파일로부터 세이브 슬롯 데이터를 역직렬화하고 CRC32를 검증합니다.
	 */
	bool SaveSlot::loadCommonFromBinaryFile( string_view path )
	{
		vector<uint8> listBlob;
		if ( FileUtil::readFile( path, listBlob ) == false || listBlob.size() < 16 )
		{
			SW_LOG_WARNING( "[SaveSlot] Binary save file unreadable or too small: %#", path );
			return false;
		}

		size_t offset{ 0 };
		uint32 magic{ 0 };
		if ( readU32Val( listBlob, offset, magic ) == false || magic != kSaveBinMagic )
		{
			SW_LOG_WARNING( "[SaveSlot] Invalid SAV1 magic in %# (0x%#08X)", path, magic );
			return false;
		}

		uint32 version{ 0 };
		if ( readU32Val( listBlob, offset, version ) == false || version > kSaveBinVersion )
		{
			SW_LOG_WARNING( "[SaveSlot] Unsupported SAV1 version %# in %#", version, path );
			return false;
		}

		uint32 expectedCrc{ 0 };
		if ( readU32Val( listBlob, offset, expectedCrc ) == false )
			return false;

		uint32 payloadSize{ 0 };
		if ( readU32Val( listBlob, offset, payloadSize ) == false || ( offset + payloadSize ) > listBlob.size() )
		{
			SW_LOG_ERROR( "[SaveSlot] Truncated SAV1 payload in %#", path );
			return false;
		}

		const uint8* pPayload	 = listBlob.data() + offset;
		const uint32 computedCrc = StringUtil::computeCrc32( pPayload, payloadSize );
		if ( expectedCrc != computedCrc )
		{
			SW_LOG_ERROR( "[SaveSlot] SAV1 CRC32 mismatch in %# (expected 0x%#08X != computed 0x%#08X) — save corrupted!", path, expectedCrc, computedCrc );
			return false;
		}

		string loadedMap;
		int32  px{ 0 };
		int32  py{ 0 };
		uint32 flagCount{ 0 };

		if ( readStrVal( listBlob, offset, loadedMap ) == false ||
			 readI32Val( listBlob, offset, px ) == false ||
			 readI32Val( listBlob, offset, py ) == false ||
			 readU32Val( listBlob, offset, flagCount ) == false )
		{
			SW_LOG_ERROR( "[SaveSlot] Corrupted payload fields in %#", path );
			return false;
		}

		if ( offset + static_cast<size_t>( flagCount ) * 5 > listBlob.size() )
		{
			SW_LOG_ERROR( "[SaveSlot] Invalid flagCount %# in SAV1 payload (overflow)", flagCount );
			return false;
		}

		map<string, int32> loadedFlags;
		for ( uint32 flagIndex = 0; flagIndex < flagCount; ++flagIndex )
		{
			string key;
			int32  val{ 0 };
			if ( readStrVal( listBlob, offset, key ) == false ||
				 readI32Val( listBlob, offset, val ) == false )
			{
				SW_LOG_ERROR( "[SaveSlot] Corrupted flag entry at index %# in %#", flagIndex, path );
				return false;
			}
			loadedFlags[std::move( key )] = val;
		}

		_mapPath  = std::move( loadedMap );
		_playerX  = px;
		_playerY  = py;
		_mapFlags = std::move( loadedFlags );

		SW_LOG_INFO( "[SaveSlot] Loaded binary SAV1 %# (map=%#, pos=%#,%#, flags=%#)", path, _mapPath, _playerX, _playerY, flagCount );
		return true;
	}

	/**
	 * @brief 공통 필드를 파일로 저장합니다. (.sav/.bin 확장자 시 바이너리, 그 외 텍스트)
	 */
	bool SaveSlot::saveCommonToFile( string_view path ) const
	{
		if ( FileUtil::hasExtension( path, ".bin" ) || FileUtil::hasExtension( path, ".sav" ) )
			return saveCommonToBinaryFile( path );
		return saveCommonToTextFile( path );
	}

	/**
	 * @brief 파일에서 공통 필드를 불러옵니다. (매직 헤더 감지 후 바이너리 또는 텍스트 로드)
	 */
	bool SaveSlot::loadCommonFromFile( string_view path )
	{
		vector<uint8> listHeader;
		if ( FileUtil::readFile( path, listHeader, 0, 4 ) && listHeader.size() >= 4 )
		{
			const uint32 magic = static_cast<uint32>( listHeader[0] ) | ( static_cast<uint32>( listHeader[1] ) << 8 ) |
								 ( static_cast<uint32>( listHeader[2] ) << 16 ) | ( static_cast<uint32>( listHeader[3] ) << 24 );
			if ( magic == kSaveBinMagic )
				return loadCommonFromBinaryFile( path );
		}

		if ( FileUtil::hasExtension( path, ".bin" ) || FileUtil::hasExtension( path, ".sav" ) )
			return loadCommonFromBinaryFile( path );

		return loadCommonFromTextFile( path );
	}
} // namespace sw
