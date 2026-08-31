#include "pch.h"

#include "Core/File/BinaryBlob.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Format/KeyValueFile.h"

#include "GameFramework/Save/ISaveGame.h"

namespace sw
{
	SW_LOG_CALLER( "SaveSlot" );

	namespace
	{
		constexpr uint32 kSaveBinMagic	 = 0x53415631u; // 'SAV1'
		constexpr uint32 kSaveBinVersion = 0;
	} // namespace

	/**
	 * @brief 세이브 슬롯의 게임플레이 플래그 값을 정수로 조회합니다. (없을 시 defaultValue 반환)
	 */
	int32 SaveSlot::getFlag( string_view key, int32 defaultValue ) const
	{
		const auto it = _mapFlag.find( string( key ) );
		if ( it == _mapFlag.end() )
			return defaultValue;
		return it->second;
	}

	/**
	 * @brief 게임플레이 플래그 값을 설정합니다.
	 */
	void SaveSlot::setFlag( string_view key, int32 value )
	{
		_mapFlag[string( key )] = value;
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
			for ( const auto& [key, val] : _mapFlag )
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
				SW_LOG_INFO( "Saved text %#", path );
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
				SW_LOG_WARNING( "Failed to load %#", path );
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
					if ( StringUtil::startsWith( key, "flag." ) )
					{
						verifySb.append( key.c_str() ).append( '=' ).append( val.c_str() ).append( '\n' );
					}
				}
				uint64 expectedHashVal{ 0 };
				StringUtil::parseUInt64( pChecksumStr, expectedHashVal, 10 );
				const uint32 expectedHash = static_cast<uint32>( expectedHashVal );
				const uint32 computedHash = StringUtil::computeHash32( verifySb.view().data(), verifySb.view().size(), false );
				if ( expectedHash != computedHash )
				{
					SW_LOG_ERROR( "Checksum mismatch in %# (expected %# != computed %#) — save corrupted!", path, expectedHash, computedHash );
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
			_mapFlag.clear();
			constexpr const utf8* kFlagPrefix = "flag.";
			const size_t		  prefixLen	  = StringUtil::strlen( kFlagPrefix );
			for ( const auto& [key, val] : map )
			{
				if ( key.size() > prefixLen && key.compare( 0, prefixLen, kFlagPrefix ) == 0 )
				{
					int32 flagVal{ 0 };
					StringUtil::parseInt( val, flagVal );
					_mapFlag[key.substr( prefixLen )] = flagVal;
				}
			}
		}

		SW_LOG_INFO( "Loaded text %# (map=%#, pos=%#,%#)", path, _mapPath, _playerX, _playerY );
		return true;
	}

	/**
	 * @brief 세이브 슬롯 데이터를 SAV1 바이너리 포맷으로 직렬화하여 저장합니다.
	 */
	bool SaveSlot::saveCommonToBinaryFile( string_view path ) const
	{
		vector<uint8> listPayload;
		BinaryBlob::appendString( listPayload, _mapPath );
		BinaryBlob::appendI32( listPayload, _playerX );
		BinaryBlob::appendI32( listPayload, _playerY );
		BinaryBlob::appendU32( listPayload, static_cast<uint32>( _mapFlag.size() ) );

		for ( const auto& [key, val] : _mapFlag )
		{
			BinaryBlob::appendString( listPayload, key );
			BinaryBlob::appendI32( listPayload, val );
		}

		const uint32 crc = StringUtil::computeCrc32( listPayload.data(), listPayload.size() );

		vector<uint8> listBlob;
		listBlob.reserve( 16 + listPayload.size() );
		BinaryBlob::appendU32( listBlob, kSaveBinMagic );
		BinaryBlob::appendU32( listBlob, kSaveBinVersion );
		BinaryBlob::appendU32( listBlob, crc );
		BinaryBlob::appendU32( listBlob, static_cast<uint32>( listPayload.size() ) );
		listBlob.insert( listBlob.end(), listPayload.begin(), listPayload.end() );

		FileUtil::createDirectory( path );
		const bool ok = FileUtil::writeFile( path, listBlob.data(), listBlob.size() );
		if ( ok )
			SW_LOG_INFO( "Saved binary (SAV1, CRC32=0x%#) -> %#", Fmt( crc, Format( 8, Format::Padding::Zero ).hexUpper() ), path );
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
			SW_LOG_WARNING( "Binary save file unreadable or too small: %#", path );
			return false;
		}

		size_t offset{ 0 };
		uint32 magic{ 0 };
		if ( BinaryBlob::readU32( listBlob, offset, magic ) == false || magic != kSaveBinMagic )
		{
			SW_LOG_WARNING( "Invalid SAV1 magic in %# (0x%#)", path, Fmt( magic, Format( 8, Format::Padding::Zero ).hexUpper() ) );
			return false;
		}

		uint32 version{ 0 };
		if ( BinaryBlob::readU32( listBlob, offset, version ) == false || version > kSaveBinVersion )
		{
			SW_LOG_WARNING( "Unsupported SAV1 version %# in %#", version, path );
			return false;
		}

		uint32 expectedCrc{ 0 };
		if ( BinaryBlob::readU32( listBlob, offset, expectedCrc ) == false )
			return false;

		uint32 payloadSize{ 0 };
		if ( BinaryBlob::readU32( listBlob, offset, payloadSize ) == false || ( offset + payloadSize ) > listBlob.size() )
		{
			SW_LOG_ERROR( "Truncated SAV1 payload in %#", path );
			return false;
		}

		const uint8* pPayload	 = listBlob.data() + offset;
		const uint32 computedCrc = StringUtil::computeCrc32( pPayload, payloadSize );
		if ( expectedCrc != computedCrc )
		{
			SW_LOG_ERROR( "SAV1 CRC32 mismatch in %# (expected 0x%# != computed 0x%#) — save corrupted!", path, Fmt( expectedCrc, Format( 8, Format::Padding::Zero ).hexUpper() ), Fmt( computedCrc, Format( 8, Format::Padding::Zero ).hexUpper() ) );
			return false;
		}

		string loadedMap;
		int32  px{ 0 };
		int32  py{ 0 };
		uint32 flagCount{ 0 };

		if ( BinaryBlob::readString( listBlob, offset, loadedMap ) == false ||
			 BinaryBlob::readI32( listBlob, offset, px ) == false ||
			 BinaryBlob::readI32( listBlob, offset, py ) == false ||
			 BinaryBlob::readU32( listBlob, offset, flagCount ) == false )
		{
			SW_LOG_ERROR( "Corrupted payload fields in %#", path );
			return false;
		}

		if ( offset + static_cast<size_t>( flagCount ) * 5 > listBlob.size() )
		{
			SW_LOG_ERROR( "Invalid flagCount %# in SAV1 payload (overflow)", flagCount );
			return false;
		}

		map<string, int32> mapLoadedFlag;
		for ( uint32 flagIndex = 0; flagIndex < flagCount; ++flagIndex )
		{
			string key;
			int32  val{ 0 };
			if ( BinaryBlob::readString( listBlob, offset, key ) == false ||
				 BinaryBlob::readI32( listBlob, offset, val ) == false )
			{
				SW_LOG_ERROR( "Corrupted flag entry at index %# in %#", flagIndex, path );
				return false;
			}
			mapLoadedFlag[std::move( key )] = val;
		}

		_mapPath = std::move( loadedMap );
		_playerX = px;
		_playerY = py;
		_mapFlag = std::move( mapLoadedFlag );

		SW_LOG_INFO( "Loaded binary SAV1 %# (map=%#, pos=%#,%#, flags=%#)", path, _mapPath, _playerX, _playerY, flagCount );
		return true;
	}

	/**
	 * @brief 공통 필드를 파일로 저장합니다. (.sav/.bin 확장자 시 바이너리, 그 외 텍스트)
	 */
	bool SaveSlot::saveCommonToFile( string_view path ) const
	{
		if ( FileUtil::hasAnyExtension( path, { ".bin", ".sav" } ) )
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

		if ( FileUtil::hasAnyExtension( path, { ".bin", ".sav" } ) )
			return loadCommonFromBinaryFile( path );

		return loadCommonFromTextFile( path );
	}
} // namespace sw
