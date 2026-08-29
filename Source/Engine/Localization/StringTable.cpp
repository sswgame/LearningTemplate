#include "pch.h"

#include "Engine/Localization/StringTable.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Utility/File/KeyValueFile.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

SW_LOG_CALLER( "StringTable" );
namespace sw
{
	namespace
	{
		constexpr uint32 kStringTableBinaryMagic   = 0x31425453; // 'STB1'
		constexpr uint32 kStringTableBinaryVersion = 1;

		struct StringTableInternal
		{
			static bool loadTextByExtension( StringTable& table, string_view path, string_view text )
			{
				if ( FileUtil::hasExtension( path, ".xml" ) )
					return table.loadFromXmlText( text );
				if ( FileUtil::hasAnyExtension( path, { ".ini", ".kv" } ) )
					return table.loadFromKeyValueText( text );
				return table.loadFromJsonText( text );
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	bool StringTable::loadFromFile( const string& filePath )
	{
		if ( FileUtil::hasExtension( filePath, ".bin" ) )
		{
			return loadFromBinaryFile( filePath );
		}

		string text;
		if ( FileUtil::readTextFile( filePath, text ) == false )
		{
			SW_LOG_WARNING( "Failed to open StringTable file: %#", filePath.c_str() );
			return false;
		}

		bool bSuccess = StringTableInternal::loadTextByExtension( *this, filePath, text );

		if ( bSuccess )
			SW_LOG_INFO( "Loaded %# strings from %#.", _mapTable.size(), filePath.c_str() );

		return bSuccess;
	}

	bool StringTable::saveToBinaryFile( string_view filePath ) const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );

		vector<uint8> buffer;
		buffer.reserve( 16 + _mapTable.size() * 32 );

		auto appendBytes = [&]( const void* pSrc, size_t numBytes )
		{
			const uint8* pByteSrc = static_cast<const uint8*>( pSrc );
			buffer.insert( buffer.end(), pByteSrc, pByteSrc + numBytes );
		};

		const uint32 magic	 = kStringTableBinaryMagic;
		const uint32 version = kStringTableBinaryVersion;
		const uint32 count	 = static_cast<uint32>( _mapTable.size() );

		appendBytes( &magic, sizeof( magic ) );
		appendBytes( &version, sizeof( version ) );
		appendBytes( &count, sizeof( count ) );

		for ( const auto& [hash, str] : _mapTable )
		{
			const uint64 keyHash = hash;
			const uint32 strLen	 = static_cast<uint32>( str.size() );
			appendBytes( &keyHash, sizeof( keyHash ) );
			appendBytes( &strLen, sizeof( strLen ) );
			if ( strLen > 0 )
			{
				appendBytes( str.data(), strLen );
			}
		}

		return FileUtil::writeFile( filePath, buffer.data(), buffer.size() );
	}

	bool StringTable::loadFromBinaryFile( string_view filePath )
	{
		vector<uint8> buffer;
		if ( FileUtil::readFile( filePath, buffer ) == false || buffer.size() < 12 )
		{
			return false;
		}

		const uint8* pPtr = buffer.data();
		const uint8* pEnd = buffer.data() + buffer.size();

		uint32 magic{ 0 };
		uint32 version{ 0 };
		uint32 count{ 0 };

		Memory::copy( &magic, pPtr, sizeof( magic ) );
		pPtr += sizeof( magic );
		Memory::copy( &version, pPtr, sizeof( version ) );
		pPtr += sizeof( version );
		Memory::copy( &count, pPtr, sizeof( count ) );
		pPtr += sizeof( count );

		if ( magic != kStringTableBinaryMagic || version != kStringTableBinaryVersion )
		{
			SW_LOG_WARNING( "Invalid StringTable binary format or version in %#", string( filePath ).c_str() );
			return false;
		}

		std::unique_lock<std::shared_mutex> lock( _mutex );
		_mapTable.reserve( _mapTable.size() + count );

		for ( uint32 index = 0; index < count; ++index )
		{
			if ( pPtr + sizeof( uint64 ) + sizeof( uint32 ) > pEnd )
			{
				SW_LOG_WARNING( "Corrupted StringTable binary file in %#", string( filePath ).c_str() );
				return false;
			}

			uint64 keyHash{ 0 };
			uint32 strLen{ 0 };
			Memory::copy( &keyHash, pPtr, sizeof( keyHash ) );
			pPtr += sizeof( keyHash );
			Memory::copy( &strLen, pPtr, sizeof( strLen ) );
			pPtr += sizeof( strLen );

			if ( pPtr + strLen > pEnd )
			{
				SW_LOG_WARNING( "Corrupted StringTable entry in %#", string( filePath ).c_str() );
				return false;
			}

			string valueStr( reinterpret_cast<const utf8*>( pPtr ), strLen );
			pPtr += strLen;

			_mapTable[keyHash] = std::move( valueStr );
		}

		SW_LOG_INFO( "Loaded %# strings from binary %#.", _mapTable.size(), string( filePath ).c_str() );
		return true;
	}

	bool StringTable::loadFromJsonText( string_view jsonText )
	{
		jsonText = FileUtil::skipUtf8Bom( jsonText );

		JsonDocument doc;
		if ( doc.parse( jsonText ) == false )
		{
			SW_LOG_WARNING( "Failed to parse StringTable JSON text." );
			return false;
		}

		const JsonValue root = doc.root();
		if ( root.isObject() == false )
		{
			SW_LOG_WARNING( "StringTable root is not an object in JSON text." );
			return false;
		}

		std::unique_lock<std::shared_mutex> lock( _mutex );
		for ( const string& key : root.memberNames() )
		{
			const JsonValue value = root.get( key, false );
			if ( value.isValid() == false || value.isObject() || value.isArray() )
				continue;
			_mapTable[hashed_string( key.c_str() ).getHash()] = value.asString();
		}

		return true;
	}

	bool StringTable::loadFromXmlText( string_view xmlText )
	{
		xmlText = FileUtil::skipUtf8Bom( xmlText );

		XmlDocument doc;
		if ( doc.parse( xmlText ) == false )
		{
			SW_LOG_WARNING( "Failed to parse StringTable XML text." );
			return false;
		}

		XmlNode root = doc.root( "GameStrings" );
		if ( root.isValid() == false )
			root = doc.root( "Strings" );
		if ( root.isValid() == false )
			root = doc.root();

		if ( root.isValid() == false )
		{
			SW_LOG_WARNING( "StringTable root node not found in XML text." );
			return false;
		}

		std::unique_lock<std::shared_mutex> lock( _mutex );
		for ( XmlNode strNode = root.child(); strNode; strNode = strNode.next() )
		{
			const utf8* pKey = strNode.attr( "key" );
			if ( pKey == nullptr || pKey[0] == '\0' )
				pKey = strNode.attr( "id" );
			if ( pKey == nullptr || pKey[0] == '\0' )
				continue;

			const utf8* pValue = strNode.attr( "value" );
			if ( pValue != nullptr && pValue[0] != '\0' )
				_mapTable[hashed_string( pKey ).getHash()] = pValue;
			else
				_mapTable[hashed_string( pKey ).getHash()] = strNode.text();
		}

		return true;
	}

	bool StringTable::loadFromKeyValueText( string_view kvText )
	{
		kvText = FileUtil::skipUtf8Bom( kvText );

		KeyValueMap map;
		if ( KeyValueFile::parse( kvText, map ) == false )
		{
			SW_LOG_WARNING( "Failed to parse StringTable KeyValue text." );
			return false;
		}

		std::unique_lock<std::shared_mutex> lock( _mutex );
		for ( const auto& pair : map )
		{
			_mapTable[hashed_string( pair.first.c_str() ).getHash()] = pair.second;
		}

		return true;
	}

	bool StringTable::loadFromResource( string_view assetRelativePath )
	{
		string text;
		string absPath;
		if ( ResourceUtil::readTextResource( assetRelativePath, text, &absPath ) == false )
		{
			SW_LOG_WARNING( "Failed to read resource StringTable: %#", assetRelativePath );
			return false;
		}

		return StringTableInternal::loadTextByExtension( *this, assetRelativePath, text );
	}

	const utf8* StringTable::getString( const hashed_string& key ) const
	{
		std::shared_lock<std::shared_mutex>					lock( _mutex );
		const unordered_map<uint64, string>::const_iterator iter = _mapTable.find( key.getHash() );
		if ( iter != _mapTable.end() )
			return iter->second.c_str();
		return nullptr;
	}

	const utf8* StringTable::getString( const hashed_string& key, const utf8* pDefaultText ) const
	{
		const utf8* pFound = getString( key );
		return pFound != nullptr ? pFound : pDefaultText;
	}

	bool StringTable::contains( const hashed_string& key ) const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );
		return _mapTable.find( key.getHash() ) != _mapTable.end();
	}

	void StringTable::setString( const hashed_string& key, const string& value )
	{
		std::unique_lock<std::shared_mutex> lock( _mutex );
		_mapTable[key.getHash()] = value;
	}

	void StringTable::clear()
	{
		std::unique_lock<std::shared_mutex> lock( _mutex );
		_mapTable.clear();
	}

	size_t StringTable::size() const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );
		return _mapTable.size();
	}

	bool StringTable::empty() const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );
		return _mapTable.empty();
	}
} // namespace sw
