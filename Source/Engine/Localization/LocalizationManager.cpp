#include "pch.h"

#include "Engine/Localization/LocalizationManager.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"

#include "Engine/FileSystem/AssetIO.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/PseudoLocalizer.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Localization/TextFormatter.h"

namespace sw
{
    SW_LOG_CALLER( "LocalizationManager" );

    namespace
    {
        struct LocalizationManagerInternal
        {
            /**
             * @brief 리소스 id 의 글을 읽습니다(낱개 파일 또는 팩).
             * @details 프로젝트 · 테이블 · 번역 경로는 모두 리소스 id 다. 루트 밖 폴더(테스트 임시 폴더, 도구 작업 폴더)는 `AssetPaths::mountDiskDomain`
             *          으로 붙여 id 로 부른다 — 예전에는 디스크를 먼저 보고 없으면 리소스로 다시 찾아 두 종류의 경로가 한 입구로 들어왔다.
             */
            [[nodiscard]] static bool readLocalizationFile( const AssetID& assetID, string& outText ) { return AssetIO::readTextAsset( assetID, outText ); }

            /** @brief 그 파일이 있는지 확인합니다. 없는 번역 테이블을 읽어 "File not found" 오류를 남기지 않게 먼저 묻습니다. */
            static bool hasLocalizationFile( const AssetID& assetID ) { return AssetIO::hasAsset( assetID ); }

            /** @brief @p code 와 그 부모들을 사슬 끝에 붙입니다(이미 있으면 건너뜀). */
            static void appendWithParents( const CultureTable& cultures, string code, vector<string>& inoutListChain )
            {
                uint32 guard{ 0 };
                while ( code.empty() == false && guard < 8 )
                {
                    ++guard;
                    bool bPresent{ false };
                    for ( const string& existing : inoutListChain )
                    {
                        bPresent = bPresent || existing == code;
                    }
                    if ( bPresent == false )
                        inoutListChain.push_back( code );
                    const CultureInfo* pCulture   = cultures.findCulture( code );
                    const bool         bHasParent = pCulture != nullptr && pCulture->_parentCode.empty() == false;
                    code                          = bHasParent ? pCulture->_parentCode : CultureTable::makeParentCode( code );
                }
            }

            /** @brief 두 리소스 id 가 같은 파일인지 확인합니다(구분자 · 대소문자 무시). */
            static bool isSamePath( string_view lhs, string_view rhs )
            {
                // 둘 다 리소스 id 다(파일 감시 · 데이터 표 편집 · 이 매니저) - 정규화한 id 가 같은지만 본다. 예전에는 꼬리 글자로도 같다고 봐(`a/x` 와 `b/a/x`)
                // 다른 폴더의 같은 이름 파일 저장이 엉뚱한 프로젝트를 다시 읽게 했다.
                return lhs.empty() == false && FileUtil::normalizePath( lhs ) == FileUtil::normalizePath( rhs );
            }
        };
    } // namespace

    string LocalizationManager::normalizeLanguageCode( string_view languageCode )
    {
        return CultureTable::normalizeCode( languageCode );
    }

    LocalizationManager::LocalizationManager()
        : _mutex{}
        , _currentLanguage{}
        , _fallbackLanguage{ CultureTable::normalizeCode( constant::kDefaultLanguage ) }
        , _cultureTable{}
        , _currentCulture{}
        , _listProject{}
        , _mapLooseTable{}
        , _mapBinaryTable{}
        , _pTextArena{ make_unique<LocalizedTextArena>() }
        , _mapLanguageTable{}
        , _mapCultureStatistic{}
        , _listLookupCulture{}
        , _missingMutex{}
        , _uniqueMissingKey{}
        , _textRevision{ 0 }
        , _onLanguageChanged{}
    {
    }

    LocalizationManager::~LocalizationManager() = default;

    void LocalizationManager::clear()
    {
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            _listProject.clear();
            _mapLooseTable.clear();
            _mapBinaryTable.clear();
            _currentLanguage.clear();
            rebuildTablesLocked();
            rebuildLookupChainLocked();
        }
        _onLanguageChanged.removeAll();
        clearMissingKeys();
        _textRevision.fetch_add( 1, std::memory_order_relaxed );
    }

    // ------------------------------------------------------------------------------
    // 2) 데이터
    // ------------------------------------------------------------------------------
    bool LocalizationManager::loadCultureTable( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        string            text;
        if ( LocalizationManagerInternal::readLocalizationFile( assetID, text ) == false )
        {
            SW_LOG_ERROR( "Culture table '%#' cannot be read", assetPath );
            return false;
        }
        return loadCultureTableXML( text );
    }

    bool LocalizationManager::loadCultureTableXML( string_view xmlText )
    {
        CultureTable loaded;
        string       error;
        if ( loaded.loadFromXMLText( xmlText, "<cultures>", &error ) == false )
        {
            SW_LOG_ERROR( "Culture table is not loaded: %#", error.c_str() );
            return false;
        }
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            _cultureTable = std::move( loaded );
            rebuildTablesLocked();
            rebuildLookupChainLocked();
        }
        _textRevision.fetch_add( 1, std::memory_order_relaxed );
        return true;
    }

    CultureInfo LocalizationManager::resolveCulture( string_view culture ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        const CultureInfo*             pCulture = _cultureTable.resolveCulture( culture );
        if ( pCulture == nullptr )
            pCulture = _cultureTable.resolveCulture( _fallbackLanguage );
        return pCulture != nullptr ? *pCulture : CultureInfo{};
    }

    bool LocalizationManager::readProject( const AssetID& assetID, MountedProject& outProject )
    {
        const string_view projectPath = assetID.view();
        string            text;
        string            error;
        if ( LocalizationManagerInternal::readLocalizationFile( assetID, text ) == false )
        {
            SW_LOG_ERROR( "Localization project '%#' cannot be read", projectPath );
            return false;
        }
        if ( outProject._project.loadFromXMLText( text, projectPath, &error ) == false )
        {
            SW_LOG_ERROR( "Localization project is not loaded: %#", error.c_str() );
            return false;
        }
        outProject._projectPath = string( projectPath );
        outProject._listFilePath.clear();
        outProject._listFilePath.push_back( outProject._projectPath );
        outProject._mapSource.clear();
        outProject._listTranslation.clear();

        bool bAllRead{ true };
        for ( const string& tableName : outProject._project._listStringTable )
        {
            const string tablePath = LocalizationProject::makeSiblingPath( projectPath, tableName );
            outProject._listFilePath.push_back( tablePath );
            SourceStringTable table;
            const bool        bRead = LocalizationManagerInternal::readLocalizationFile( AssetID{ tablePath }, text ) && table.loadFromXMLText( text, tablePath, &error );
            if ( bRead == false )
            {
                SW_LOG_ERROR( "String table '%#' is not loaded: %#", tablePath.c_str(), error.c_str() );
                bAllRead = false;
                continue;
            }
            for ( const auto& [key, entry] : table.getEntries() )
            {
                if ( outProject._mapSource.find( key ) != outProject._mapSource.end() )
                {
                    SW_LOG_ERROR( "String table '%#': key '%#' is already defined by another table of project '%#'", tablePath.c_str(), key.c_str(),
                                  outProject._project._name.c_str() );
                    continue;
                }
                outProject._mapSource[key] = entry;
            }
        }

        for ( const string& culture : outProject._project._listCulture )
        {
            const string translationPath = LocalizationProject::makeTranslationPath( projectPath, culture );
            outProject._listFilePath.push_back( translationPath );
            if ( LocalizationManagerInternal::hasLocalizationFile( AssetID{ translationPath } ) == false )
                continue; // 아직 번역이 없는 문화권이다. 수집과 가져오기가 만든다
            if ( LocalizationManagerInternal::readLocalizationFile( AssetID{ translationPath }, text ) == false )
            {
                SW_LOG_ERROR( "Translation table '%#' cannot be read", translationPath.c_str() );
                bAllRead = false;
                continue;
            }
            TranslationTable translation;
            if ( translation.loadFromXMLText( text, translationPath, &error ) == false )
            {
                SW_LOG_ERROR( "Translation table is not loaded: %#", error.c_str() );
                bAllRead = false;
                continue;
            }
            if ( translation.getCulture() != culture )
            {
                SW_LOG_ERROR( "Translation table '%#' says culture '%#' but the project lists it as '%#'", translationPath.c_str(), translation.getCulture().c_str(),
                              culture.c_str() );
                bAllRead = false;
                continue;
            }
            outProject._listTranslation.push_back( std::move( translation ) );
        }
        return bAllRead;
    }

    bool LocalizationManager::mountProject( const AssetID& assetID, LocalizationScope scope )
    {
        const string_view projectPath = assetID.view();
        MountedProject    mounted;
        mounted._scope       = scope;
        const bool bComplete = readProject( assetID, mounted );
        if ( mounted._project._name.empty() )
            return false;

        string oldLanguage;
        string newLanguage;
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            bool                     bReplaced{ false };
            for ( MountedProject& existing : _listProject )
            {
                if ( existing._project._name == mounted._project._name )
                {
                    existing  = std::move( mounted );
                    bReplaced = true;
                    break;
                }
            }
            if ( bReplaced == false )
                _listProject.push_back( std::move( mounted ) );
            oldLanguage = _currentLanguage;
            if ( _currentLanguage.empty() )
                _currentLanguage = _listProject.back()._project._sourceCulture;
            newLanguage = _currentLanguage;
            rebuildTablesLocked();
            rebuildLookupChainLocked();
        }
        SW_LOG_INFO( "Mounted localization project '%#'.", projectPath );
        notifyLanguageChanged( oldLanguage, newLanguage );
        return bComplete;
    }

    void LocalizationManager::unmountProjects( LocalizationScope scope )
    {
        string language;
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            const size_t             before = _listProject.size();
            for ( size_t index = _listProject.size(); index > 0; --index )
            {
                if ( _listProject[index - 1]._scope == scope )
                    _listProject.erase( _listProject.begin() + static_cast<ptrdiff_t>( index - 1 ) );
            }
            if ( before == _listProject.size() )
                return;
            rebuildTablesLocked();
            rebuildLookupChainLocked();
            language = _currentLanguage;
        }
        notifyLanguageChanged( language, language );
    }

    vector<string> LocalizationManager::getMountedProjectNames() const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        vector<string>                 listName;
        for ( const MountedProject& project : _listProject )
        {
            listName.push_back( project._project._name );
        }
        return listName;
    }

    void LocalizationManager::copyStateFrom( const LocalizationManager& source )
    {
        if ( &source == this )
            return;
        CultureTable                  cultureTable;
        vector<MountedProject>        listProject;
        map<string, TranslationTable> mapLooseTable;
        string                        currentLanguage;
        string                        fallbackLanguage;
        {
            ScopedSharedLock<shared_mutex> lock{ source._mutex };
            cultureTable     = source._cultureTable;
            listProject      = source._listProject;
            mapLooseTable    = source._mapLooseTable;
            currentLanguage  = source._currentLanguage;
            fallbackLanguage = source._fallbackLanguage;
        }
        string oldLanguage;
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            oldLanguage       = _currentLanguage;
            _cultureTable     = std::move( cultureTable );
            _listProject      = std::move( listProject );
            _mapLooseTable    = std::move( mapLooseTable );
            _currentLanguage  = currentLanguage;
            _fallbackLanguage = fallbackLanguage;
            rebuildTablesLocked();
            rebuildLookupChainLocked();
        }
        notifyLanguageChanged( oldLanguage, currentLanguage );
    }

    bool LocalizationManager::loadLanguageXML( string_view languageCode, string_view xmlText )
    {
        TranslationTable table;
        string           error;
        if ( languageCode.empty() || table.loadFromXMLText( xmlText, "<memory>", &error ) == false )
        {
            SW_LOG_WARNING( "Translation XML for '%#' is not loaded: %#", languageCode, error.c_str() );
            return false;
        }
        string oldLanguage;
        string newLanguage;
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            const string             code  = normalizeLanguageCode( languageCode );
            TranslationTable&        loose = _mapLooseTable[code];
            loose.setCulture( code );
            for ( const auto& [key, entry] : table.getEntries() )
            {
                loose.getOrAddEntry( key ) = entry;
            }
            oldLanguage = _currentLanguage;
            if ( _currentLanguage.empty() )
                _currentLanguage = code;
            newLanguage = _currentLanguage;
            rebuildTablesLocked();
            rebuildLookupChainLocked();
        }
        notifyLanguageChanged( oldLanguage, newLanguage );
        return true;
    }

    void LocalizationManager::setString( string_view languageCode, const hashed_string& key, string_view value )
    {
        UniqueLock<shared_mutex> lock{ _mutex };
        const string             code  = normalizeLanguageCode( languageCode );
        TranslationTable&        loose = _mapLooseTable[code];
        loose.setCulture( code );
        TranslationEntry& entry = loose.getOrAddEntry( key.c_str() );
        entry._text             = string( value );
        getOrCreateTableLocked( code ).setStringByHash( key.getHash(), value );
        if ( _currentLanguage.empty() )
        {
            _currentLanguage = code;
            rebuildLookupChainLocked();
        }
    }

    void LocalizationManager::unloadLanguage( string_view languageCode )
    {
        string language;
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            const string             code         = normalizeLanguageCode( languageCode );
            const size_t             erasedLoose  = _mapLooseTable.erase( code );
            const size_t             erasedBinary = _mapBinaryTable.erase( code );
            if ( erasedLoose == 0 && erasedBinary == 0 )
                return;
            rebuildTablesLocked();
            language = _currentLanguage;
        }
        notifyLanguageChanged( language, language );
    }

    bool LocalizationManager::loadBinaryTable( const uint8* pData, size_t size )
    {
        LocalizationBinaryTable table;
        string                  error;
        if ( table.parse( pData, size, &error ) == false )
        {
            SW_LOG_WARNING( "Binary localization table is not loaded: %#", error.c_str() );
            return false;
        }
        // 문화권 검사와 경고는 잠금 밖에서 한다(잠금은 잎 잠금, 결정 5-3).
        const string code = normalizeLanguageCode( table.getCulture() );
        if ( code.empty() )
        {
            SW_LOG_WARNING( "Binary localization table has no culture" );
            return false;
        }
        string oldLanguage;
        string newLanguage;
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            _mapBinaryTable[code] = std::move( table );
            oldLanguage           = _currentLanguage;
            if ( _currentLanguage.empty() )
                _currentLanguage = code;
            newLanguage = _currentLanguage;
            rebuildTablesLocked();
            rebuildLookupChainLocked();
        }
        notifyLanguageChanged( oldLanguage, newLanguage );
        return true;
    }

    bool LocalizationManager::loadBinaryTableResource( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        vector<uint8>     bytes;
        if ( AssetIO::readBinaryAsset( assetID, bytes ) == false )
        {
            SW_LOG_WARNING( "Binary localization table '%#' cannot be read", assetPath );
            return false;
        }
        return loadBinaryTable( bytes.data(), bytes.size() );
    }

    bool LocalizationManager::cookBinaryTable( string_view languageCode, vector<uint8>& outBytes ) const
    {
        const string                   code = normalizeLanguageCode( languageCode );
        LocalizationBinaryTable        table;
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        table.setCulture( code );
        for ( const MountedProject& project : _listProject )
        {
            if ( project._project._sourceCulture == code )
            {
                for ( const auto& [key, entry] : project._mapSource )
                {
                    table.setText( key, entry._source );
                }
            }
            for ( const TranslationTable& translation : project._listTranslation )
            {
                if ( translation.getCulture() != code )
                    continue;
                for ( const auto& [key, entry] : translation.getEntries() )
                {
                    const auto                   sourceIt = project._mapSource.find( key );
                    const SourceTextEntry* const pSource  = sourceIt != project._mapSource.end() ? &sourceIt->second : nullptr;
                    if ( translation.computeState( key, pSource ) == TranslationState::Current )
                        table.setText( key, entry._text );
                }
            }
        }
        if ( table.getTexts().empty() )
            return false;
        table.toBytes( outBytes );
        return true;
    }

    bool LocalizationManager::initialize( const AssetID& assetID, string_view defaultLanguage, string_view fallbackLanguage )
    {
        const string_view projectPath = assetID.view();
        if ( projectPath.empty() )
            return false;
        unmountProjects( LocalizationScope::Game );
        setFallbackLanguage( fallbackLanguage.empty() ? string_view( constant::kDefaultLanguage ) : fallbackLanguage );
        if ( mountProject( assetID, LocalizationScope::Game ) == false )
        {
            SW_LOG_WARNING( "Localization project '%#' is not (fully) loaded.", projectPath );
            if ( getMountedProjectNames().empty() )
                return false;
        }

        string preferredLanguage;
        if ( engine::areEngineServicesBound() )
            engine::getSubsystem<CommandLineManager>().getArgument( CommandLineArgument::LANGUAGE, preferredLanguage );
        if ( preferredLanguage.empty() || hasLanguage( preferredLanguage ) == false )
            preferredLanguage = string( defaultLanguage );
        if ( hasLanguage( preferredLanguage ) == false )
            preferredLanguage = hasLanguage( fallbackLanguage ) ? string( fallbackLanguage ) : string{};
        if ( preferredLanguage.empty() )
        {
            const vector<string> listLanguage = getAvailableLanguages();
            if ( listLanguage.empty() == false )
                preferredLanguage = listLanguage.front();
        }
        if ( preferredLanguage.empty() == false )
        {
            setCurrentLanguage( preferredLanguage );
            SW_LOG_INFO( "Localization setup complete. Active language: '%#', Fallback: '%#'.", preferredLanguage.c_str(), getFallbackLanguage().c_str() );
        }
        return true;
    }

    // ------------------------------------------------------------------------------
    // 실행 테이블 다시 채우기
    // ------------------------------------------------------------------------------
    StringTable& LocalizationManager::getOrCreateTableLocked( const string& code )
    {
        unique_ptr<StringTable>& pTable = _mapLanguageTable[code];
        if ( pTable == nullptr )
            pTable = make_unique<StringTable>( *_pTextArena );
        return *pTable;
    }

    void LocalizationManager::rebuildTablesLocked()
    {
        // 테이블 객체는 지우지 않고 비운다. `getLanguageTable` 이 돌려준 포인터를 가진 쪽이 있다.
        for ( auto& [code, pTable] : _mapLanguageTable )
        {
            pTable->clear();
        }
        _mapCultureStatistic.clear();

        for ( const MountedProject& project : _listProject )
        {
            StringTable& sourceTable = getOrCreateTableLocked( project._project._sourceCulture );
            for ( const auto& [key, entry] : project._mapSource )
            {
                sourceTable.setStringByHash( hashed_string::computeHash( key ), entry._source );
            }

            for ( const TranslationTable& translation : project._listTranslation )
            {
                StringTable&                   table      = getOrCreateTableLocked( translation.getCulture() );
                LocalizationCultureStatistics& statistics = _mapCultureStatistic[translation.getCulture()];
                for ( const auto& [key, entry] : translation.getEntries() )
                {
                    const auto                   sourceIt = project._mapSource.find( key );
                    const SourceTextEntry* const pSource  = sourceIt != project._mapSource.end() ? &sourceIt->second : nullptr;
                    switch ( translation.computeState( key, pSource ) )
                    {
                        case TranslationState::Current:
                        {
                            table.setStringByHash( hashed_string::computeHash( key ), entry._text );
                            ++statistics._currentCount;
                            break;
                        }
                        case TranslationState::Stale:
                        {
                            ++statistics._staleCount;
                            break;
                        }
                        case TranslationState::Review:
                        {
                            ++statistics._reviewCount;
                            break;
                        }
                        case TranslationState::Orphan:
                        {
                            ++statistics._orphanCount;
                            break;
                        }
                        case TranslationState::Missing:
                        {
                            break;
                        }
                    }
                }
            }
        }

        // 의사 문화권: 프로젝트 원문을 변환한다(길이와 하드코딩 찾기의 기준은 원문이다).
        vector<string> listCultureCode;
        _cultureTable.collectCultureCodes( listCultureCode );
        for ( const string& code : listCultureCode )
        {
            const CultureInfo* pCulture              = _cultureTable.findCulture( code );
            const bool         bSkipNonPseudoCulture = pCulture == nullptr || pCulture->isPseudo() == false || _listProject.empty();
            if ( bSkipNonPseudoCulture )
                continue;
            StringTable& pseudoTable = getOrCreateTableLocked( code );
            for ( const MountedProject& project : _listProject )
            {
                for ( const auto& [key, entry] : project._mapSource )
                {
                    pseudoTable.setStringByHash( hashed_string::computeHash( key ), PseudoLocalizer::transform( entry._source, pCulture->_pseudoMode ) );
                }
            }
        }

        // 쿠킹된 이진 표는 프로젝트 위, 낱개 테이블 아래다.
        for ( const auto& [code, binary] : _mapBinaryTable )
        {
            StringTable& table = getOrCreateTableLocked( code );
            for ( const auto& [keyHash, text] : binary.getTexts() )
            {
                table.setStringByHash( keyHash, text );
            }
        }

        // 낱개 테이블이 마지막이다. 프로젝트 위에 덮어쓴다.
        for ( const auto& [code, loose] : _mapLooseTable )
        {
            StringTable& table = getOrCreateTableLocked( code );
            for ( const auto& [key, entry] : loose.getEntries() )
            {
                table.setStringByHash( hashed_string::computeHash( key ), entry._text );
            }
        }
    }

    void LocalizationManager::rebuildLookupChainLocked()
    {
        _listLookupCulture.clear();
        LocalizationManagerInternal::appendWithParents( _cultureTable, _currentLanguage, _listLookupCulture );
        LocalizationManagerInternal::appendWithParents( _cultureTable, _fallbackLanguage, _listLookupCulture );
        for ( const MountedProject& project : _listProject )
        {
            LocalizationManagerInternal::appendWithParents( _cultureTable, project._project._sourceCulture, _listLookupCulture );
        }

        const CultureInfo* pCulture = _cultureTable.resolveCulture( _currentLanguage );
        if ( pCulture == nullptr )
            pCulture = _cultureTable.resolveCulture( _fallbackLanguage );
        _currentCulture = pCulture != nullptr ? *pCulture : CultureInfo{};
    }

    // ------------------------------------------------------------------------------
    // 3) 언어
    // ------------------------------------------------------------------------------
    bool LocalizationManager::setCurrentLanguage( string_view languageCode )
    {
        const string code = normalizeLanguageCode( languageCode );
        string       oldLanguage;
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            if ( _currentLanguage == code )
                return true;
            oldLanguage      = _currentLanguage;
            _currentLanguage = code;
            rebuildLookupChainLocked();
        }
        SW_LOG_INFO( "Language changed: '%#' -> '%#'", oldLanguage.c_str(), code.c_str() );
        notifyLanguageChanged( oldLanguage, code );
        return true;
    }

    string LocalizationManager::getCurrentLanguage() const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        return _currentLanguage;
    }

    void LocalizationManager::setFallbackLanguage( string_view languageCode )
    {
        string                   code = normalizeLanguageCode( languageCode );
        UniqueLock<shared_mutex> lock{ _mutex };
        _fallbackLanguage = std::move( code );
        rebuildLookupChainLocked();
    }

    string LocalizationManager::getFallbackLanguage() const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        return _fallbackLanguage;
    }

    bool LocalizationManager::hasLanguage( string_view languageCode ) const
    {
        const string                   code = normalizeLanguageCode( languageCode );
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        const auto                     it = _mapLanguageTable.find( code );
        if ( it == _mapLanguageTable.end() || it->second->empty() )
            return false;
        if constexpr ( build::kShipping ) // 배포는 가짜 언어를 고르지 않는다
        {
            const CultureInfo* pCulture = _cultureTable.findCulture( code );
            if ( pCulture != nullptr && pCulture->isPseudo() )
                return false;
        }
        return true;
    }

    vector<string> LocalizationManager::getAvailableLanguages() const
    {
        vector<string> listLanguage;
        {
            ScopedSharedLock<shared_mutex> lock{ _mutex };
            listLanguage.reserve( _mapLanguageTable.size() );
            for ( const auto& [code, pTable] : _mapLanguageTable )
            {
                if ( pTable->empty() )
                    continue;
                if constexpr ( build::kShipping )
                {
                    const CultureInfo* pCulture = _cultureTable.findCulture( code );
                    if ( pCulture != nullptr && pCulture->isPseudo() )
                        continue;
                }
                listLanguage.push_back( code );
            }
        }
        VectorUtil::sort( listLanguage );
        return listLanguage;
    }

    size_t LocalizationManager::getLanguageCount() const
    {
        return getAvailableLanguages().size();
    }

    vector<string> LocalizationManager::getLookupChain() const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        return _listLookupCulture;
    }

    bool LocalizationManager::isRightToLeft() const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        return _currentCulture._bRightToLeft;
    }

    vector<string> LocalizationManager::getFontFallback( string_view languageCode ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        vector<string>                 listChain;
        const string                   code = languageCode.empty() ? _currentLanguage : normalizeLanguageCode( languageCode );
        LocalizationManagerInternal::appendWithParents( _cultureTable, code, listChain );
        LocalizationManagerInternal::appendWithParents( _cultureTable, _fallbackLanguage, listChain );
        for ( const string& candidate : listChain )
        {
            const CultureInfo* pCulture = _cultureTable.findCulture( candidate );
            if ( pCulture != nullptr && pCulture->_listFont.empty() == false )
                return pCulture->_listFont;
        }
        return {};
    }

    LocalizationCultureStatistics LocalizationManager::getStatistics( string_view languageCode ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        const auto                     it = _mapCultureStatistic.find( normalizeLanguageCode( languageCode ) );
        return it != _mapCultureStatistic.end() ? it->second : LocalizationCultureStatistics{};
    }

    uint32 LocalizationManager::getTextRevision() const
    {
        return _textRevision.load( std::memory_order_relaxed );
    }

    // ------------------------------------------------------------------------------
    // 4) 조회와 포맷
    // ------------------------------------------------------------------------------
    const utf8* LocalizationManager::getString( const hashed_string& key ) const
    {
        // 빈 키는 "글 없음" 이다. `hashed_string` 의 빈 이름은 "None" 으로 읽히므로 키 이름으로 돌려주지 않는다.
        if ( key.empty() )
            return "";
        const utf8* pFound = findString( key );
        if ( pFound != nullptr )
            return pFound;
        reportMissingKey( key.view() );
        return key.c_str();
    }

    const utf8* LocalizationManager::findString( const hashed_string& key ) const
    {
        // intern 된 키는 해시를 이미 가지고 있다. 다시 계산하지 않는다.
        return findByHash( key.getHash() );
    }

    const utf8* LocalizationManager::findStringByText( string_view keyText ) const
    {
        if ( keyText.empty() )
            return nullptr;
        return findByHash( hashed_string::computeHash( keyText ) );
    }

    const utf8* LocalizationManager::findByHash( uint64 keyHash ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        for ( const string& code : _listLookupCulture )
        {
            const auto it = _mapLanguageTable.find( code );
            if ( it == _mapLanguageTable.end() )
                continue;
            const utf8* pFound = it->second->findByHash( keyHash );
            if ( StringUtil::isNullOrEmpty( pFound ) == false )
                return pFound;
        }
        return nullptr;
    }

    const utf8* LocalizationManager::findStringFromLanguage( string_view languageCode, const hashed_string& key ) const
    {
        const string                   code = normalizeLanguageCode( languageCode );
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        const auto                     it = _mapLanguageTable.find( code );
        return it != _mapLanguageTable.end() ? it->second->findString( key ) : nullptr;
    }

    bool LocalizationManager::hasString( const hashed_string& key ) const
    {
        return findByHash( key.getHash() ) != nullptr;
    }

    const StringTable* LocalizationManager::getLanguageTable( string_view languageCode ) const
    {
        const string                   code = normalizeLanguageCode( languageCode );
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        const auto                     it = _mapLanguageTable.find( code );
        return it != _mapLanguageTable.end() ? it->second.get() : nullptr;
    }

    string LocalizationManager::formatText( string_view pattern, const TextArgumentList& arguments ) const
    {
        CultureInfo culture;
        {
            ScopedSharedLock<shared_mutex> lock{ _mutex };
            culture = _currentCulture;
        }
        string text;
        string error;
        if ( TextFormatter::format( pattern, arguments, culture, text, &error ) == false )
            SW_LOG_WARNING( "Text format '%#' (culture '%#'): %#", pattern, culture._code.c_str(), error.c_str() );
        return text;
    }

    string LocalizationManager::getFormattedString( const hashed_string& key, const TextArgumentList& arguments, const utf8* pFallbackPattern ) const
    {
        const utf8* pPattern = findString( key );
        if ( pPattern == nullptr )
        {
            reportMissingKey( key.view() );
            pPattern = pFallbackPattern;
        }
        return formatText( pPattern != nullptr ? string_view( pPattern ) : string_view{}, arguments );
    }

    void LocalizationManager::reportMissingKey( string_view key ) const
    {
        if ( key.empty() )
            return;
        string language;
        {
            ScopedSharedLock<shared_mutex> lock{ _mutex };
            const bool                     bNoTextLoaded = _listProject.empty() && _mapLooseTable.empty() && _mapBinaryTable.empty();
            if ( bNoTextLoaded )
                return; // 텍스트를 하나도 로드하지 않은 실행(로컬라이제이션 없는 게임, 테스트)은 모든 키가 빠지므로 알릴 필요가 없다
            language = _currentLanguage;
        }
        bool bFirst{ false };
        {
            ScopedLock<mutex> lock{ _missingMutex };
            bFirst = _uniqueMissingKey.insert( string( key ) ).second;
        }
        if ( bFirst )
            SW_LOG_WARNING_ID( "Localization.MissingKey", "Missing localized text for key '%#' (culture '%#')", key, language.c_str() );
    }

    vector<string> LocalizationManager::getMissingKeys() const
    {
        ScopedLock<mutex> lock{ _missingMutex };
        return vector<string>( _uniqueMissingKey.begin(), _uniqueMissingKey.end() );
    }

    void LocalizationManager::clearMissingKeys()
    {
        ScopedLock<mutex> lock{ _missingMutex };
        _uniqueMissingKey.clear();
    }

    // ------------------------------------------------------------------------------
    // 5) 콜백
    // ------------------------------------------------------------------------------
    Subscription LocalizationManager::registerLanguageChangedCallback( LanguageChangedCallback callback )
    {
        return _onLanguageChanged.add( callback ); // 빈 콜백은 빈 구독이다
    }

    void LocalizationManager::notifyLanguageChanged( string_view oldLanguage, string_view newLanguage )
    {
        _textRevision.fetch_add( 1, std::memory_order_relaxed );
        _onLanguageChanged.broadcast( oldLanguage, newLanguage );
    }

    ReloadOutcome LocalizationManager::reloadChangedFile( string_view changedPath )
    {
        string            projectPath;
        LocalizationScope scope{ LocalizationScope::Game };
        {
            ScopedSharedLock<shared_mutex> lock{ _mutex };
            for ( const MountedProject& project : _listProject )
            {
                for ( const string& filePath : project._listFilePath )
                {
                    if ( projectPath.empty() && LocalizationManagerInternal::isSamePath( filePath, changedPath ) )
                    {
                        projectPath = project._projectPath;
                        scope       = project._scope;
                    }
                }
            }
        }
        if ( projectPath.empty() )
            return ReloadOutcome::NotTracked;

        // 다시 읽다 실패하면 기존 내용을 유지한다. 저장 도중의 반쯤 쓴 파일이 화면의 텍스트를 모두 지우면 안 된다.
        MountedProject reloaded;
        reloaded._scope = scope;
        if ( readProject( AssetID{ projectPath }, reloaded ) == false )
        {
            SW_LOG_ERROR( "Localization project '%#' is not reloaded - keeping the previous text", projectPath.c_str() );
            return ReloadOutcome::Failed;
        }
        string language;
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            for ( MountedProject& project : _listProject )
            {
                if ( project._projectPath == projectPath )
                    project = std::move( reloaded );
            }
            rebuildTablesLocked();
            rebuildLookupChainLocked();
            language = _currentLanguage;
        }
        SW_LOG_INFO( "Reloaded localization project '%#' (changed: %#).", projectPath.c_str(), changedPath );
        notifyLanguageChanged( language, language );
        return ReloadOutcome::Reloaded;
    }

    bool LocalizationManager::isProjectFile( string_view path ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        for ( const MountedProject& project : _listProject )
        {
            for ( const string& filePath : project._listFilePath )
            {
                if ( LocalizationManagerInternal::isSamePath( filePath, path ) )
                    return true;
            }
        }
        return false;
    }
} // namespace sw
