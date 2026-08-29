#include "pch.h"

#include "Engine/Localization/LocalizationManager.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Utility/File/KeyValueFile.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include <mutex>
#include <shared_mutex>
#include <utility>

namespace sw
{
	SW_LOG_CALLER( "LocalizationManager" );

	LocalizationManager::LocalizationManager()
		: _mutex{}
		, _currentLanguage{}
		, _fallbackLanguage{ "en_US" }
		, _mapLanguageTable{}
		, _mapCallback{}
		, _nextCallbackId{ 1 }
	{
	}

	LocalizationManager::~LocalizationManager() = default;

	LocalizationManager::LocalizationManager( LocalizationManager&& other ) noexcept
	{
		std::unique_lock<std::shared_mutex> lock( other._mutex );
		_currentLanguage  = std::move( other._currentLanguage );
		_fallbackLanguage = std::move( other._fallbackLanguage );
		_mapLanguageTable = std::move( other._mapLanguageTable );
		_mapCallback	  = std::move( other._mapCallback );
		_nextCallbackId	  = other._nextCallbackId;
	}

	LocalizationManager& LocalizationManager::operator=( LocalizationManager&& other ) noexcept
	{
		if ( this != &other )
		{
			std::unique_lock<std::shared_mutex> lockThis( _mutex, std::defer_lock );
			std::unique_lock<std::shared_mutex> lockOther( other._mutex, std::defer_lock );
			std::lock( lockThis, lockOther );

			_currentLanguage  = std::move( other._currentLanguage );
			_fallbackLanguage = std::move( other._fallbackLanguage );
			_mapLanguageTable = std::move( other._mapLanguageTable );
			_mapCallback	  = std::move( other._mapCallback );
			_nextCallbackId	  = other._nextCallbackId;
		}
		return *this;
	}

	void LocalizationManager::clear()
	{
		std::unique_lock<std::shared_mutex> lock( _mutex );
		_mapLanguageTable.clear();
		_mapCallback.clear();
		_currentLanguage.clear();
	}

	bool LocalizationManager::loadLanguageFile( string_view languageCode, string_view filePath )
	{
		if ( languageCode.empty() || filePath.empty() )
		{
			SW_LOG_WARNING( "Invalid languageCode or filePath." );
			return false;
		}

		string text;
		if ( FileUtil::readTextFile( filePath, text ) == false )
		{
			SW_LOG_WARNING( "Failed to read language file: %#", string( filePath ).c_str() );
			return false;
		}

		bool bSuccess = loadLanguageFromText( languageCode, filePath, text );
		if ( bSuccess )
			SW_LOG_INFO( "Loaded language '%#' from file '%#'.", string( languageCode ).c_str(), string( filePath ).c_str() );

		return bSuccess;
	}

	bool LocalizationManager::loadLanguageResource( string_view languageCode, string_view assetRelativePath )
	{
		if ( languageCode.empty() || assetRelativePath.empty() )
		{
			SW_LOG_WARNING( "Invalid languageCode or assetRelativePath." );
			return false;
		}

		string text;
		string absPath;
		if ( ResourceUtil::readTextResource( assetRelativePath, text, &absPath ) == false )
		{
			SW_LOG_WARNING( "Failed to read language resource: %#", assetRelativePath );
			return false;
		}

		bool bSuccess = loadLanguageFromText( languageCode, assetRelativePath, text );
		if ( bSuccess )
			SW_LOG_INFO( "Loaded language '%#' from resource '%#'.", string( languageCode ).c_str(), string( absPath ).c_str() );

		return bSuccess;
	}

	bool LocalizationManager::loadLanguageFromText( string_view languageCode, string_view pathHint, string_view text )
	{
		if ( FileUtil::hasExtension( pathHint, ".xml" ) )
			return loadLanguageXml( languageCode, text );
		if ( FileUtil::hasAnyExtension( pathHint, { ".ini", ".kv" } ) )
			return loadLanguageKeyValue( languageCode, text );
		return loadLanguageJson( languageCode, text );
	}

	bool LocalizationManager::loadLanguageJson( string_view languageCode, string_view jsonText )
	{
		if ( languageCode.empty() )
			return false;

		StringTable* pTable = getOrCreateLanguageTable( languageCode );
		if ( pTable == nullptr )
			return false;

		const bool bSuccess = pTable->loadFromJsonText( jsonText );
		if ( bSuccess )
		{
			std::unique_lock<std::shared_mutex> lock( _mutex );
			if ( _currentLanguage.empty() )
				_currentLanguage = languageCode;
		}
		return bSuccess;
	}

	bool LocalizationManager::loadLanguageXml( string_view languageCode, string_view xmlText )
	{
		if ( languageCode.empty() )
			return false;

		StringTable* pTable = getOrCreateLanguageTable( languageCode );
		if ( pTable == nullptr )
			return false;

		const bool bSuccess = pTable->loadFromXmlText( xmlText );
		if ( bSuccess )
		{
			std::unique_lock<std::shared_mutex> lock( _mutex );
			if ( _currentLanguage.empty() )
				_currentLanguage = languageCode;
		}
		return bSuccess;
	}

	bool LocalizationManager::loadLanguageKeyValue( string_view languageCode, string_view kvText )
	{
		if ( languageCode.empty() )
			return false;

		StringTable* pTable = getOrCreateLanguageTable( languageCode );
		if ( pTable == nullptr )
			return false;

		const bool bSuccess = pTable->loadFromKeyValueText( kvText );
		if ( bSuccess )
		{
			std::unique_lock<std::shared_mutex> lock( _mutex );
			if ( _currentLanguage.empty() )
				_currentLanguage = languageCode;
		}
		return bSuccess;
	}

	bool LocalizationManager::loadLanguageDirectory( string_view directoryPath, string_view filterExtension, bool bRecursive )
	{
		if ( directoryPath.empty() || FileUtil::directoryExists( directoryPath ) == false )
		{
			SW_LOG_WARNING( "Directory does not exist: %#", string( directoryPath ).c_str() );
			return false;
		}

		vector<string> filePathList;
		if ( FileUtil::collectFiles( directoryPath, filterExtension, filePathList, bRecursive, false ) == false || filePathList.empty() )
		{
			return false;
		}

		uint32 loadedCount{ 0 };
		for ( const string& filePath : filePathList )
		{
			const string fileName = FileUtil::getFileNamePart( filePath );
			const string langCode = FileUtil::removeExtension( fileName );
			if ( langCode.empty() )
				continue;

			if ( loadLanguageFile( langCode, filePath ) )
				++loadedCount;
		}

		SW_LOG_INFO( "Loaded %# language files from %#", loadedCount, string( directoryPath ).c_str() );
		return loadedCount > 0;
	}

	namespace
	{
		constexpr uint32 kLocPackBinaryMagic   = 0x31434F4C; // 'LOC1'
		constexpr uint32 kLocPackBinaryVersion = 1;
	} // namespace

	bool LocalizationManager::setupLocalization( string_view directoryOrResourcePath, string_view defaultLanguage, string_view fallbackLanguage )
	{
		if ( directoryOrResourcePath.empty() )
			return false;

		setFallbackLanguage( fallbackLanguage.empty() ? "en_US" : fallbackLanguage );

		bool bLoadedAny{ false };

		string absDirPath = ResourceUtil::getResourcePath( directoryOrResourcePath );
		if ( absDirPath.empty() )
			absDirPath = string( directoryOrResourcePath );

		if ( FileUtil::hasExtension( absDirPath, ".bin" ) )
		{
			bLoadedAny = loadFromBinaryPack( absDirPath );
		}
		else if ( FileUtil::directoryExists( absDirPath ) )
		{
			// Check if a pre-cooked binary pack exists in the directory (e.g. localization.loc.bin)
			const string binPackPath = FileUtil::joinPath( absDirPath, "localization.loc.bin" );
			if ( FileUtil::fileExists( binPackPath ) )
			{
				bLoadedAny = loadFromBinaryPack( binPackPath );
			}

			if ( bLoadedAny == false )
			{
				bLoadedAny = loadLanguageDirectory( absDirPath, ".json", true ) || bLoadedAny;
				bLoadedAny = loadLanguageDirectory( absDirPath, ".xml", true ) || bLoadedAny;
				bLoadedAny = loadLanguageDirectory( absDirPath, ".ini", true ) || bLoadedAny;
				bLoadedAny = loadLanguageDirectory( absDirPath, ".kv", true ) || bLoadedAny;
				bLoadedAny = loadLanguageDirectory( absDirPath, ".bin", true ) || bLoadedAny;
			}
		}
		else
		{
			bLoadedAny = loadLanguageResource( defaultLanguage.empty() ? "default" : defaultLanguage, directoryOrResourcePath );
		}

		if ( bLoadedAny == false )
		{
			SW_LOG_WARNING( "Failed to load any localization files from '%#'.", directoryOrResourcePath );
			return false;
		}

		string preferredLang;
		if ( engine::areEngineServicesBound() )
		{
			const CommandLineManager& cmd = engine::getCommandLineManager();
			if ( cmd.getArgument( CommandLineArgument::LANGUAGE, preferredLang ) == false || preferredLang.empty() )
			{
				cmd.getArgument( "lang", preferredLang );
			}
		}

		if ( preferredLang.empty() || hasLanguage( preferredLang ) == false )
			preferredLang = defaultLanguage;

		if ( hasLanguage( preferredLang ) == false )
		{
			if ( hasLanguage( fallbackLanguage ) )
				preferredLang = fallbackLanguage;
			else
			{
				vector<string> listLangs = getAvailableLanguages();
				if ( listLangs.empty() == false )
					preferredLang = listLangs[0];
			}
		}

		if ( preferredLang.empty() == false && hasLanguage( preferredLang ) )
		{
			setCurrentLanguage( preferredLang );
			SW_LOG_INFO( "Localization setup complete. Active language: '%#', Fallback: '%#'.", string( preferredLang ).c_str(), string( _fallbackLanguage ).c_str() );
		}

		return true;
	}

	bool LocalizationManager::saveToBinaryPack( string_view filePath ) const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );

		vector<uint8> buffer;
		buffer.reserve( 16 + _mapLanguageTable.size() * 1024 );

		auto appendBytes = [&]( const void* pSrc, size_t numBytes )
		{
			const uint8* pByteSrc = static_cast<const uint8*>( pSrc );
			buffer.insert( buffer.end(), pByteSrc, pByteSrc + numBytes );
		};

		const uint32 magic		   = kLocPackBinaryMagic;
		const uint32 version	   = kLocPackBinaryVersion;
		const uint32 languageCount = static_cast<uint32>( _mapLanguageTable.size() );

		appendBytes( &magic, sizeof( magic ) );
		appendBytes( &version, sizeof( version ) );
		appendBytes( &languageCount, sizeof( languageCount ) );

		for ( const auto& [langCode, pTable] : _mapLanguageTable )
		{
			const uint32 codeLen = static_cast<uint32>( langCode.size() );
			appendBytes( &codeLen, sizeof( codeLen ) );
			if ( codeLen > 0 )
			{
				appendBytes( langCode.data(), codeLen );
			}

			// Encode language table
			vector<uint8> tableBuffer;
			if ( pTable != nullptr )
			{
				const string tempFile = FileUtil::joinPath( FileUtil::getTempDirectory(), "temp_st_save.bin" );
				if ( pTable->saveToBinaryFile( tempFile ) )
				{
					FileUtil::readFile( tempFile, tableBuffer );
					FileUtil::removeFile( tempFile );
				}
			}

			const uint32 tableSize = static_cast<uint32>( tableBuffer.size() );
			appendBytes( &tableSize, sizeof( tableSize ) );
			if ( tableSize > 0 )
			{
				appendBytes( tableBuffer.data(), tableSize );
			}
		}

		return FileUtil::writeFile( filePath, buffer.data(), buffer.size() );
	}

	bool LocalizationManager::loadFromBinaryPack( string_view filePath )
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
		uint32 languageCount{ 0 };

		Memory::copy( &magic, pPtr, sizeof( magic ) );
		pPtr += sizeof( magic );
		Memory::copy( &version, pPtr, sizeof( version ) );
		pPtr += sizeof( version );
		Memory::copy( &languageCount, pPtr, sizeof( languageCount ) );
		pPtr += sizeof( languageCount );

		if ( magic != kLocPackBinaryMagic || version != kLocPackBinaryVersion )
		{
			SW_LOG_WARNING( "Invalid Localization binary pack format or version in %#", string( filePath ).c_str() );
			return false;
		}

		for ( uint32 index = 0; index < languageCount; ++index )
		{
			if ( pPtr + sizeof( uint32 ) > pEnd )
			{
				return false;
			}

			uint32 codeLen{ 0 };
			Memory::copy( &codeLen, pPtr, sizeof( codeLen ) );
			pPtr += sizeof( codeLen );

			if ( pPtr + codeLen + sizeof( uint32 ) > pEnd )
			{
				return false;
			}

			string langCode( reinterpret_cast<const utf8*>( pPtr ), codeLen );
			pPtr += codeLen;

			uint32 tableSize{ 0 };
			Memory::copy( &tableSize, pPtr, sizeof( tableSize ) );
			pPtr += sizeof( tableSize );

			if ( pPtr + tableSize > pEnd )
			{
				return false;
			}

			if ( tableSize > 0 )
			{
				const string tempFile = FileUtil::joinPath( FileUtil::getTempDirectory(), "temp_st_load.bin" );
				if ( FileUtil::writeFile( tempFile, pPtr, tableSize ) )
				{
					auto pTable = make_unique<StringTable>();
					if ( pTable->loadFromBinaryFile( tempFile ) )
					{
						registerLanguageTable( langCode, std::move( pTable ) );
					}
					FileUtil::removeFile( tempFile );
				}
			}

			pPtr += tableSize;
		}

		SW_LOG_INFO( "Loaded binary localization pack '%#' (%# languages).", string( filePath ).c_str(), languageCount );
		return true;
	}

	void LocalizationManager::registerLanguageTable( string_view languageCode, unique_ptr<StringTable> pStringTable )
	{
		if ( languageCode.empty() || pStringTable == nullptr )
			return;

		std::unique_lock<std::shared_mutex> lock( _mutex );
		_mapLanguageTable[string( languageCode )] = std::move( pStringTable );
		if ( _currentLanguage.empty() )
			_currentLanguage = languageCode;
	}

	void LocalizationManager::unloadLanguage( string_view languageCode )
	{
		std::unique_lock<std::shared_mutex> lock( _mutex );
		_mapLanguageTable.erase( string( languageCode ) );
	}

	bool LocalizationManager::setCurrentLanguage( string_view languageCode )
	{
		string oldLanguage;
		bool   bChanged{ false };

		{
			std::unique_lock<std::shared_mutex> lock( _mutex );
			if ( _currentLanguage != languageCode )
			{
				oldLanguage		 = _currentLanguage;
				_currentLanguage = languageCode;
				bChanged		 = true;
			}
		}

		if ( bChanged )
		{
			SW_LOG_INFO( "Language changed: '%#' -> '%#'", string( oldLanguage ).c_str(), string( languageCode ).c_str() );
			notifyLanguageChanged( oldLanguage, languageCode );
		}

		return true;
	}

	const string& LocalizationManager::getCurrentLanguage() const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );
		return _currentLanguage;
	}

	void LocalizationManager::setFallbackLanguage( string_view languageCode )
	{
		std::unique_lock<std::shared_mutex> lock( _mutex );
		_fallbackLanguage = languageCode;
	}

	const string& LocalizationManager::getFallbackLanguage() const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );
		return _fallbackLanguage;
	}

	bool LocalizationManager::hasLanguage( string_view languageCode ) const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );
		return _mapLanguageTable.find( string( languageCode ) ) != _mapLanguageTable.end();
	}

	vector<string> LocalizationManager::getAvailableLanguages() const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );
		vector<string>						languageList;
		languageList.reserve( _mapLanguageTable.size() );
		for ( const auto& pair : _mapLanguageTable )
		{
			languageList.push_back( pair.first );
		}
		return languageList;
	}

	size_t LocalizationManager::getLanguageCount() const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );
		return _mapLanguageTable.size();
	}

	const utf8* LocalizationManager::getString( const hashed_string& key, const utf8* pDefaultText ) const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );

		// 1) 현재 활성 언어 테이블에서 검색
		const auto currentIter = _mapLanguageTable.find( _currentLanguage );
		if ( currentIter != _mapLanguageTable.end() && currentIter->second != nullptr )
		{
			const utf8* pFound = currentIter->second->getString( key );
			if ( pFound != nullptr && pFound[0] != '\0' )
				return pFound;
		}

		// 2) 누락 시 Fallback 언어 테이블에서 검색
		if ( _fallbackLanguage.empty() == false && _fallbackLanguage != _currentLanguage )
		{
			const auto fallbackIter = _mapLanguageTable.find( _fallbackLanguage );
			if ( fallbackIter != _mapLanguageTable.end() && fallbackIter->second != nullptr )
			{
				const utf8* pFound = fallbackIter->second->getString( key );
				if ( pFound != nullptr && pFound[0] != '\0' )
					return pFound;
			}
		}

		// 3) 기본 텍스트 반환
		return pDefaultText;
	}

	const utf8* LocalizationManager::getStringFromLanguage( string_view languageCode, const hashed_string& key, const utf8* pDefaultText ) const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );
		const auto							iter = _mapLanguageTable.find( string( languageCode ) );
		if ( iter != _mapLanguageTable.end() && iter->second != nullptr )
		{
			const utf8* pFound = iter->second->getString( key );
			if ( pFound != nullptr )
				return pFound;
		}
		return pDefaultText;
	}

	bool LocalizationManager::hasString( const hashed_string& key ) const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );

		const auto currentIter = _mapLanguageTable.find( _currentLanguage );
		if ( currentIter != _mapLanguageTable.end() && currentIter->second != nullptr && currentIter->second->contains( key ) )
			return true;

		if ( _fallbackLanguage.empty() == false && _fallbackLanguage != _currentLanguage )
		{
			const auto fallbackIter = _mapLanguageTable.find( _fallbackLanguage );
			if ( fallbackIter != _mapLanguageTable.end() && fallbackIter->second != nullptr && fallbackIter->second->contains( key ) )
				return true;
		}

		return false;
	}

	bool LocalizationManager::hasStringInLanguage( string_view languageCode, const hashed_string& key ) const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );
		const auto							iter = _mapLanguageTable.find( string( languageCode ) );
		if ( iter != _mapLanguageTable.end() && iter->second != nullptr )
			return iter->second->contains( key );
		return false;
	}

	void LocalizationManager::setString( string_view languageCode, const hashed_string& key, string_view value )
	{
		StringTable* pTable = getOrCreateLanguageTable( languageCode );
		if ( pTable != nullptr )
			pTable->setString( key, string( value ) );
	}

	const StringTable* LocalizationManager::getLanguageTable( string_view languageCode ) const
	{
		std::shared_lock<std::shared_mutex> lock( _mutex );
		const auto							iter = _mapLanguageTable.find( string( languageCode ) );
		if ( iter != _mapLanguageTable.end() )
			return iter->second.get();
		return nullptr;
	}

	StringTable* LocalizationManager::getOrCreateLanguageTable( string_view languageCode )
	{
		std::unique_lock<std::shared_mutex> lock( _mutex );
		auto&								pTable = _mapLanguageTable[string( languageCode )];
		if ( pTable == nullptr )
			pTable = make_unique<StringTable>();
		return pTable.get();
	}

	uint32 LocalizationManager::registerLanguageChangedCallback( LanguageChangedCallback callback )
	{
		if ( callback == nullptr )
			return 0;

		std::unique_lock<std::shared_mutex> lock( _mutex );
		const uint32						callbackId = _nextCallbackId++;
		_mapCallback[callbackId]					   = std::move( callback );
		return callbackId;
	}

	void LocalizationManager::unregisterLanguageChangedCallback( uint32 callbackId )
	{
		if ( callbackId == 0 )
			return;

		std::unique_lock<std::shared_mutex> lock( _mutex );
		_mapCallback.erase( callbackId );
	}

	void LocalizationManager::notifyLanguageChanged( string_view oldLanguage, string_view newLanguage )
	{
		vector<LanguageChangedCallback> callbackList;
		{
			std::shared_lock<std::shared_mutex> lock( _mutex );
			callbackList.reserve( _mapCallback.size() );
			for ( const auto& pair : _mapCallback )
			{
				if ( pair.second.isBound() )
					callbackList.push_back( pair.second );
			}
		}

		for ( const auto& callback : callbackList )
		{
			callback( oldLanguage, newLanguage );
		}
	}
} // namespace sw
