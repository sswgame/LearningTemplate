#include "pch.h"

#include "Engine/Localization/LocalizationManager.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "LocalizationManager" );

    string LocalizationManager::normalizeLanguageCode( string_view languageCode )
    {
        string code( StringUtil::trim( languageCode ) );
        for ( utf8& character : code )
        {
            if ( character == '-' )
                character = '_';
            else if ( character >= 'A' && character <= 'Z' )
                character = static_cast<utf8>( character - 'A' + 'a' );
        }
        return code;
    }

    LocalizationManager::LocalizationManager()
        : _mutex{}
        , _currentLanguage{}
        , _fallbackLanguage{ constant::kDefaultLanguage }
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
        _mapCallback      = std::move( other._mapCallback );
        _nextCallbackId   = other._nextCallbackId;
    }

    // `std::lock` 은 시스템 오류일 때 던질 수 있어 noexcept 와 어긋난다고 짚힌다. 뮤텍스를 잠그지
    // 못하는 상황은 복구 대상이 아니므로 종료가 맞고, noexcept 는 그 의도를 적은 것이다.
    // NOLINTNEXTLINE(bugprone-exception-escape)
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
            _mapCallback      = std::move( other._mapCallback );
            _nextCallbackId   = other._nextCallbackId;
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

    // 언어 파일인지는 표를 만들기 **전에** 묻는다 — 읽지 못할 파일 때문에 빈 언어가 등록되지 않게.
    bool LocalizationManager::loadLanguageFile( string_view languageCode, string_view filePath )
    {
        if ( languageCode.empty() || filePath.empty() )
        {
            SW_LOG_WARNING( "Invalid languageCode or filePath." );
            return false;
        }
        if ( StringTable::isLanguageFile( filePath ) == false )
        {
            SW_LOG_WARNING( "Not a language file (expected %#): %#", StringTable::kFileExtension, filePath );
            return false;
        }

        StringTable* pTable = getOrCreateLanguageTable( languageCode );
        if ( pTable == nullptr || pTable->loadFromFile( string( filePath ) ) == false )
        {
            SW_LOG_WARNING( "Failed to load language file: %#", string( filePath ).c_str() );
            return false;
        }

        markLanguageLoaded( languageCode );
        SW_LOG_INFO( "Loaded language '%#' from file '%#'.", string( languageCode ).c_str(), string( filePath ).c_str() );
        return true;
    }

    bool LocalizationManager::loadLanguageResource( string_view languageCode, string_view assetRelativePath )
    {
        if ( languageCode.empty() || assetRelativePath.empty() )
        {
            SW_LOG_WARNING( "Invalid languageCode or assetRelativePath." );
            return false;
        }
        if ( StringTable::isLanguageFile( assetRelativePath ) == false )
        {
            SW_LOG_WARNING( "Not a language file (expected %#): %#", StringTable::kFileExtension, assetRelativePath );
            return false;
        }

        StringTable* pTable = getOrCreateLanguageTable( languageCode );
        if ( pTable == nullptr || pTable->loadFromResource( assetRelativePath ) == false )
        {
            SW_LOG_WARNING( "Failed to load language resource: %#", assetRelativePath );
            return false;
        }

        markLanguageLoaded( languageCode );
        SW_LOG_INFO( "Loaded language '%#' from resource '%#'.", string( languageCode ).c_str(), string( assetRelativePath ).c_str() );
        return true;
    }

    void LocalizationManager::markLanguageLoaded( string_view languageCode )
    {
        string                              code = normalizeLanguageCode( languageCode );
        std::unique_lock<std::shared_mutex> lock( _mutex );
        if ( _currentLanguage.empty() )
            _currentLanguage = std::move( code );
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
            markLanguageLoaded( languageCode );
        return bSuccess;
    }

    bool LocalizationManager::loadLanguageDirectory( string_view directoryPath, string_view filterExtension, bool bRecursive )
    {
        if ( directoryPath.empty() || FileUtil::directoryExists( directoryPath ) == false )
        {
            SW_LOG_WARNING( "Directory does not exist: %#", string( directoryPath ).c_str() );
            return false;
        }

        vector<string> listFilePath;
        if ( FileUtil::collectFiles( directoryPath, filterExtension, listFilePath, bRecursive ) == false || listFilePath.empty() )
            return false;

        uint32 loadedCount{ 0 };
        for ( const string& filePath : listFilePath )
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

    bool LocalizationManager::initialize( string_view directoryOrResourcePath, string_view defaultLanguage, string_view fallbackLanguage )
    {
        if ( directoryOrResourcePath.empty() )
            return false;

        setFallbackLanguage( fallbackLanguage.empty() ? "en_US" : fallbackLanguage );

        bool bLoadedAny{ false };

        string absDirPath = ResourceUtil::getResourcePath( directoryOrResourcePath );
        if ( absDirPath.empty() )
            absDirPath = string( directoryOrResourcePath );

        if ( FileUtil::directoryExists( absDirPath ) )
            bLoadedAny = loadLanguageDirectory( absDirPath, StringTable::kFileExtension, true );
        else
            bLoadedAny = loadLanguageResource( defaultLanguage.empty() ? "default" : defaultLanguage, directoryOrResourcePath );

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
                cmd.getArgument( "lang", preferredLang );
        }

        if ( preferredLang.empty() || hasLanguage( preferredLang ) == false )
            preferredLang = defaultLanguage;

        if ( hasLanguage( preferredLang ) == false )
        {
            if ( hasLanguage( fallbackLanguage ) )
                preferredLang = fallbackLanguage;
            else
            {
                vector<string> listLang = getAvailableLanguages();
                if ( listLang.empty() == false )
                    preferredLang = listLang[0];
            }
        }

        if ( preferredLang.empty() == false && hasLanguage( preferredLang ) )
        {
            setCurrentLanguage( preferredLang );
            SW_LOG_INFO( "Localization setup complete. Active language: '%#', Fallback: '%#'.", string( preferredLang ).c_str(), string( _fallbackLanguage ).c_str() );
        }

        return true;
    }

    void LocalizationManager::registerLanguageTable( string_view languageCode, unique_ptr<StringTable> pStringTable )
    {
        if ( languageCode.empty() || pStringTable == nullptr )
            return;

        const string                        code = normalizeLanguageCode( languageCode );
        std::unique_lock<std::shared_mutex> lock( _mutex );
        _mapLanguageTable[code] = std::move( pStringTable );
        if ( _currentLanguage.empty() )
            _currentLanguage = code;
    }

    void LocalizationManager::unloadLanguage( string_view languageCode )
    {
        const string                        code = normalizeLanguageCode( languageCode );
        std::unique_lock<std::shared_mutex> lock( _mutex );
        _mapLanguageTable.erase( code );
    }

    bool LocalizationManager::setCurrentLanguage( string_view languageCode )
    {
        const string code = normalizeLanguageCode( languageCode );
        string       oldLanguage;
        bool         bChanged{ false };

        {
            std::unique_lock<std::shared_mutex> lock( _mutex );
            if ( _currentLanguage != code )
            {
                oldLanguage      = _currentLanguage;
                _currentLanguage = code;
                bChanged         = true;
            }
        }

        if ( bChanged )
        {
            SW_LOG_INFO( "Language changed: '%#' -> '%#'", oldLanguage.c_str(), code.c_str() );
            notifyLanguageChanged( oldLanguage, code );
        }

        return true;
    }

    string LocalizationManager::getCurrentLanguage() const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
        return _currentLanguage;
    }

    void LocalizationManager::setFallbackLanguage( string_view languageCode )
    {
        string                              code = normalizeLanguageCode( languageCode );
        std::unique_lock<std::shared_mutex> lock( _mutex );
        _fallbackLanguage = std::move( code );
    }

    string LocalizationManager::getFallbackLanguage() const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
        return _fallbackLanguage;
    }

    bool LocalizationManager::hasLanguage( string_view languageCode ) const
    {
        const string                        code = normalizeLanguageCode( languageCode );
        std::shared_lock<std::shared_mutex> lock( _mutex );
        return _mapLanguageTable.find( code ) != _mapLanguageTable.end();
    }

    vector<string> LocalizationManager::getAvailableLanguages() const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
        vector<string>                      listLanguage;
        listLanguage.reserve( _mapLanguageTable.size() );
        for ( const auto& pair : _mapLanguageTable )
        {
            listLanguage.push_back( pair.first );
        }
        return listLanguage;
    }

    size_t LocalizationManager::getLanguageCount() const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
        return _mapLanguageTable.size();
    }

    const utf8* LocalizationManager::getString( const hashed_string& key, const utf8* pDefaultText ) const
    {
        // intern 된 키는 해시를 이미 들고 있다. 다시 계산하지 않는다.
        return findByHash( key.getHash(), pDefaultText );
    }

    const utf8* LocalizationManager::getStringByText( string_view keyText, const utf8* pDefaultText ) const
    {
        if ( keyText.empty() )
            return pDefaultText;
        return findByHash( hashed_string::computeHash( keyText ), pDefaultText );
    }

    const utf8* LocalizationManager::findByHash( uint64 keyHash, const utf8* pDefaultText ) const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );

        // 1) 현재 활성 언어 테이블에서 찾는다
        const auto currentIter = _mapLanguageTable.find( _currentLanguage );
        if ( currentIter != _mapLanguageTable.end() && currentIter->second != nullptr )
        {
            const utf8* pFound = currentIter->second->findByHash( keyHash );
            if ( StringUtil::isNullOrEmpty( pFound ) == false )
                return pFound;
        }

        // 2) 없으면 폴백 언어 테이블에서 찾는다
        if ( _fallbackLanguage.empty() == false && _fallbackLanguage != _currentLanguage )
        {
            const auto fallbackIter = _mapLanguageTable.find( _fallbackLanguage );
            if ( fallbackIter != _mapLanguageTable.end() && fallbackIter->second != nullptr )
            {
                const utf8* pFound = fallbackIter->second->findByHash( keyHash );
                if ( StringUtil::isNullOrEmpty( pFound ) == false )
                    return pFound;
            }
        }

        // 3) 기본 텍스트를 반환한다
        return pDefaultText;
    }

    const utf8* LocalizationManager::getStringFromLanguage( string_view languageCode, const hashed_string& key, const utf8* pDefaultText ) const
    {
        const string                        code = normalizeLanguageCode( languageCode );
        std::shared_lock<std::shared_mutex> lock( _mutex );
        const auto                          iter = _mapLanguageTable.find( code );
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
        const string                        code = normalizeLanguageCode( languageCode );
        std::shared_lock<std::shared_mutex> lock( _mutex );
        const auto                          iter = _mapLanguageTable.find( code );
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
        const string                        code = normalizeLanguageCode( languageCode );
        std::shared_lock<std::shared_mutex> lock( _mutex );
        const auto                          iter = _mapLanguageTable.find( code );
        if ( iter != _mapLanguageTable.end() )
            return iter->second.get();
        return nullptr;
    }

    StringTable* LocalizationManager::getOrCreateLanguageTable( string_view languageCode )
    {
        const string                        code = normalizeLanguageCode( languageCode );
        std::unique_lock<std::shared_mutex> lock( _mutex );
        auto&                               pTable = _mapLanguageTable[code];
        if ( pTable == nullptr )
            pTable = make_unique<StringTable>();
        return pTable.get();
    }

    uint32 LocalizationManager::registerLanguageChangedCallback( LanguageChangedCallback callback )
    {
        if ( callback == nullptr )
            return 0;

        std::unique_lock<std::shared_mutex> lock( _mutex );
        const uint32                        callbackId = _nextCallbackId++;
        _mapCallback[callbackId]                       = std::move( callback );
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
        vector<LanguageChangedCallback> listCallback;
        {
            std::shared_lock<std::shared_mutex> lock( _mutex );
            listCallback.reserve( _mapCallback.size() );
            for ( const auto& pair : _mapCallback )
            {
                if ( pair.second.isBound() )
                    listCallback.push_back( pair.second );
            }
        }

        for ( const auto& callback : listCallback )
        {
            callback( oldLanguage, newLanguage );
        }
    }
} // namespace sw
