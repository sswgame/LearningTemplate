#include "pch.h"

#include "Engine/Localization/LocalizationManager.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/PseudoLocalizer.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "LocalizationManager" );

    namespace
    {
        struct LocalizationManagerInternal
        {
            /** @brief 리소스 경로 또는 절대 경로의 글을 읽습니다(팩 안이면 팩에서). */
            [[nodiscard]] static bool readLocalizationFile( string_view path, string& outText )
            {
                // 디스크에 있는 경로(시험 · 도구의 절대 경로)는 그대로 읽는다 — 리소스 조회는 못 찾으면 오류를 남긴다.
                if ( FileUtil::isRegularFile( path ) )
                    return FileUtil::readTextFile( path, outText );
                return ResourceUtil::readTextResource( path, outText );
            }

            /** @brief 그 파일이 있는지 — 없는 번역 표를 읽어 "File not found" 오류를 남기지 않게 먼저 묻습니다. */
            static bool hasLocalizationFile( string_view path ) { return ResourceUtil::hasResource( path ) || FileUtil::exists( path ); }

            /** @brief 두 경로가 같은 파일인지 — 구분자 · 대소문자를 무시하고, 한쪽이 절대 경로면 꼬리를 맞춰 봅니다. */
            static bool isSamePath( string_view lhs, string_view rhs )
            {
                const string left  = FileUtil::normalizeSeparators( lhs );
                const string right = FileUtil::normalizeSeparators( rhs );
                if ( left.empty() || right.empty() )
                    return false;
                if ( StringUtil::equals( left, right, true ) )
                    return true;
                const string& longer  = left.size() > right.size() ? left : right;
                const string& shorter = left.size() > right.size() ? right : left;
                return StringUtil::endsWith( longer, "/" + shorter, true );
            }

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
        , _mapLanguageTable{}
        , _mapCultureStatistic{}
        , _listLookupCulture{}
        , _mapCallback{}
        , _missingMutex{}
        , _uniqueMissingKey{}
        , _textRevision{ 0 }
        , _nextCallbackID{ 1 }
    {
    }

    LocalizationManager::~LocalizationManager() = default;

    void LocalizationManager::clear()
    {
        {
            std::unique_lock<std::shared_mutex> lock( _mutex );
            _listProject.clear();
            _mapLooseTable.clear();
            _mapCallback.clear();
            _currentLanguage.clear();
            rebuildTablesLocked();
            rebuildLookupChainLocked();
        }
        clearMissingKeys();
        _textRevision.fetch_add( 1, std::memory_order_relaxed );
    }

    // ------------------------------------------------------------------------------
    // 2) 데이터
    // ------------------------------------------------------------------------------
    bool LocalizationManager::loadCultureTable( string_view resourcePath )
    {
        string text;
        if ( LocalizationManagerInternal::readLocalizationFile( resourcePath, text ) == false )
        {
            SW_LOG_ERROR( "Culture table '%#' cannot be read", resourcePath );
            return false;
        }
        return loadCultureTableJSON( text );
    }

    bool LocalizationManager::loadCultureTableJSON( string_view jsonText )
    {
        CultureTable loaded;
        string       error;
        if ( loaded.loadFromJSONText( jsonText, "<cultures>", &error ) == false )
        {
            SW_LOG_ERROR( "Culture table is not loaded: %#", error.c_str() );
            return false;
        }
        {
            std::unique_lock<std::shared_mutex> lock( _mutex );
            _cultureTable = std::move( loaded );
            rebuildTablesLocked();
            rebuildLookupChainLocked();
        }
        _textRevision.fetch_add( 1, std::memory_order_relaxed );
        return true;
    }

    CultureInfo LocalizationManager::resolveCulture( string_view culture ) const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
        const CultureInfo*                  pCulture = _cultureTable.resolveCulture( culture );
        if ( pCulture == nullptr )
            pCulture = _cultureTable.resolveCulture( _fallbackLanguage );
        return pCulture != nullptr ? *pCulture : CultureInfo{};
    }

    bool LocalizationManager::readProject( string_view projectPath, MountedProject& outProject )
    {
        string text;
        string error;
        if ( LocalizationManagerInternal::readLocalizationFile( projectPath, text ) == false )
        {
            SW_LOG_ERROR( "Localization project '%#' cannot be read", projectPath );
            return false;
        }
        if ( outProject._project.loadFromJSONText( text, projectPath, &error ) == false )
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
            const bool        bRead = LocalizationManagerInternal::readLocalizationFile( tablePath, text ) && table.loadFromJSONText( text, tablePath, &error );
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
            if ( LocalizationManagerInternal::hasLocalizationFile( translationPath ) == false )
                continue; // 아직 번역이 없는 문화권 — 수집 · 가져오기가 만든다
            if ( LocalizationManagerInternal::readLocalizationFile( translationPath, text ) == false )
            {
                SW_LOG_ERROR( "Translation table '%#' cannot be read", translationPath.c_str() );
                bAllRead = false;
                continue;
            }
            TranslationTable translation;
            if ( translation.loadFromJSONText( text, translationPath, &error ) == false )
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

    bool LocalizationManager::mountProject( string_view projectPath, LocalizationScope scope )
    {
        MountedProject mounted;
        mounted._scope       = scope;
        const bool bComplete = readProject( projectPath, mounted );
        if ( mounted._project._name.empty() )
            return false;

        string oldLanguage;
        string newLanguage;
        {
            std::unique_lock<std::shared_mutex> lock( _mutex );
            bool                                bReplaced{ false };
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
            std::unique_lock<std::shared_mutex> lock( _mutex );
            const size_t                        before = _listProject.size();
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
        std::shared_lock<std::shared_mutex> lock( _mutex );
        vector<string>                      listName;
        for ( const MountedProject& project : _listProject )
        {
            listName.push_back( project._project._name );
        }
        return listName;
    }

    bool LocalizationManager::reloadProjects()
    {
        vector<string> listPath;
        {
            std::shared_lock<std::shared_mutex> lock( _mutex );
            for ( const MountedProject& project : _listProject )
            {
                listPath.push_back( project._projectPath );
            }
        }
        bool bAllReloaded{ true };
        for ( const string& path : listPath )
        {
            bAllReloaded = reloadChangedFile( path ) && bAllReloaded;
        }
        return bAllReloaded;
    }

    bool LocalizationManager::reloadChangedFile( string_view changedPath )
    {
        string            projectPath;
        LocalizationScope scope{ LocalizationScope::Game };
        {
            std::shared_lock<std::shared_mutex> lock( _mutex );
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
            return false;

        // 다시 읽다 실패하면 예전 내용을 지킨다 — 저장 도중의 반쯤 쓴 파일이 화면의 글을 다 지우면 안 된다.
        MountedProject reloaded;
        reloaded._scope = scope;
        if ( readProject( projectPath, reloaded ) == false )
        {
            SW_LOG_WARNING( "Localization project '%#' is not reloaded - keeping the previous text", projectPath.c_str() );
            return false;
        }
        string language;
        {
            std::unique_lock<std::shared_mutex> lock( _mutex );
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
        return true;
    }

    bool LocalizationManager::isProjectFile( string_view path ) const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
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

    bool LocalizationManager::loadLanguageJSON( string_view languageCode, string_view jsonText )
    {
        TranslationTable table;
        string           error;
        if ( languageCode.empty() || table.loadFromJSONText( jsonText, "<memory>", &error ) == false )
        {
            SW_LOG_WARNING( "Translation JSON for '%#' is not loaded: %#", languageCode, error.c_str() );
            return false;
        }
        string oldLanguage;
        string newLanguage;
        {
            std::unique_lock<std::shared_mutex> lock( _mutex );
            const string                        code  = normalizeLanguageCode( languageCode );
            TranslationTable&                   loose = _mapLooseTable[code];
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
        std::unique_lock<std::shared_mutex> lock( _mutex );
        const string                        code  = normalizeLanguageCode( languageCode );
        TranslationTable&                   loose = _mapLooseTable[code];
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
            std::unique_lock<std::shared_mutex> lock( _mutex );
            if ( _mapLooseTable.erase( normalizeLanguageCode( languageCode ) ) == 0 )
                return;
            rebuildTablesLocked();
            language = _currentLanguage;
        }
        notifyLanguageChanged( language, language );
    }

    bool LocalizationManager::initialize( string_view projectPath, string_view defaultLanguage, string_view fallbackLanguage )
    {
        if ( projectPath.empty() )
            return false;
        unmountProjects( LocalizationScope::Game );
        setFallbackLanguage( fallbackLanguage.empty() ? string_view( constant::kDefaultLanguage ) : fallbackLanguage );
        if ( mountProject( projectPath, LocalizationScope::Game ) == false )
        {
            SW_LOG_WARNING( "Localization project '%#' is not (fully) loaded.", projectPath );
            if ( getMountedProjectNames().empty() )
                return false;
        }

        string preferredLanguage;
        if ( engine::areEngineServicesBound() )
            engine::getCommandLineManager().getArgument( CommandLineArgument::LANGUAGE, preferredLanguage );
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
    // 실행 표 다시 채우기
    // ------------------------------------------------------------------------------
    StringTable& LocalizationManager::getOrCreateTableLocked( const string& code )
    {
        unique_ptr<StringTable>& pTable = _mapLanguageTable[code];
        if ( pTable == nullptr )
            pTable = make_unique<StringTable>();
        return *pTable;
    }

    void LocalizationManager::rebuildTablesLocked()
    {
        // 표 객체는 지우지 않고 비운다 — `getLanguageTable` 이 돌려준 포인터를 든 쪽이 있다.
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

        // 의사 문화권 — 프로젝트 원문을 변환한다(길이 · 하드코딩 찾기의 기준은 원문이다).
        vector<string> listCultureCode;
        _cultureTable.collectCultureCodes( listCultureCode );
        for ( const string& code : listCultureCode )
        {
            const CultureInfo* pCulture = _cultureTable.findCulture( code );
            if ( pCulture == nullptr || pCulture->isPseudo() == false || _listProject.empty() )
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

        // 낱개 표가 마지막 — 프로젝트 위에 덮인다.
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
            std::unique_lock<std::shared_mutex> lock( _mutex );
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
        std::shared_lock<std::shared_mutex> lock( _mutex );
        return _currentLanguage;
    }

    void LocalizationManager::setFallbackLanguage( string_view languageCode )
    {
        string                              code = normalizeLanguageCode( languageCode );
        std::unique_lock<std::shared_mutex> lock( _mutex );
        _fallbackLanguage = std::move( code );
        rebuildLookupChainLocked();
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
        const auto                          it = _mapLanguageTable.find( code );
        if ( it == _mapLanguageTable.end() || it->second->empty() )
            return false;
#if defined( SW_SHIPPING )
        const CultureInfo* pCulture = _cultureTable.findCulture( code );
        if ( pCulture != nullptr && pCulture->isPseudo() )
            return false;
#endif
        return true;
    }

    vector<string> LocalizationManager::getAvailableLanguages() const
    {
        vector<string> listLanguage;
        {
            std::shared_lock<std::shared_mutex> lock( _mutex );
            listLanguage.reserve( _mapLanguageTable.size() );
            for ( const auto& [code, pTable] : _mapLanguageTable )
            {
                if ( pTable->empty() )
                    continue;
#if defined( SW_SHIPPING )
                const CultureInfo* pCulture = _cultureTable.findCulture( code );
                if ( pCulture != nullptr && pCulture->isPseudo() )
                    continue;
#endif
                listLanguage.push_back( code );
            }
        }
        std::sort( listLanguage.begin(), listLanguage.end() );
        return listLanguage;
    }

    size_t LocalizationManager::getLanguageCount() const
    {
        return getAvailableLanguages().size();
    }

    vector<string> LocalizationManager::getLookupChain() const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
        return _listLookupCulture;
    }

    CultureInfo LocalizationManager::getCurrentCulture() const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
        return _currentCulture;
    }

    bool LocalizationManager::isRightToLeft() const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
        return _currentCulture._bRightToLeft;
    }

    vector<string> LocalizationManager::getFontFallback( string_view languageCode ) const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
        vector<string>                      listChain;
        const string                        code = languageCode.empty() ? _currentLanguage : normalizeLanguageCode( languageCode );
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
        std::shared_lock<std::shared_mutex> lock( _mutex );
        const auto                          it = _mapCultureStatistic.find( normalizeLanguageCode( languageCode ) );
        return it != _mapCultureStatistic.end() ? it->second : LocalizationCultureStatistics{};
    }

    uint32 LocalizationManager::getTextRevision() const
    {
        return _textRevision.load( std::memory_order_relaxed );
    }

    // ------------------------------------------------------------------------------
    // 4) 조회 · 포맷
    // ------------------------------------------------------------------------------
    const utf8* LocalizationManager::getString( const hashed_string& key, const utf8* pDefaultText ) const
    {
        // intern 된 키는 해시를 이미 들고 있다. 다시 계산하지 않는다.
        const utf8* pFound = findByHash( key.getHash() );
        if ( pFound != nullptr )
            return pFound;
        reportMissingKey( key.c_str() );
        return pDefaultText;
    }

    const utf8* LocalizationManager::getStringByText( string_view keyText, const utf8* pDefaultText ) const
    {
        if ( keyText.empty() )
            return pDefaultText;
        const utf8* pFound = findByHash( hashed_string::computeHash( keyText ) );
        return pFound != nullptr ? pFound : pDefaultText;
    }

    const utf8* LocalizationManager::findByHash( uint64 keyHash ) const
    {
        std::shared_lock<std::shared_mutex> lock( _mutex );
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

    const utf8* LocalizationManager::getStringFromLanguage( string_view languageCode, const hashed_string& key, const utf8* pDefaultText ) const
    {
        const string                        code = normalizeLanguageCode( languageCode );
        std::shared_lock<std::shared_mutex> lock( _mutex );
        const auto                          it = _mapLanguageTable.find( code );
        if ( it != _mapLanguageTable.end() )
        {
            const utf8* pFound = it->second->getString( key );
            if ( pFound != nullptr )
                return pFound;
        }
        return pDefaultText;
    }

    bool LocalizationManager::hasString( const hashed_string& key ) const
    {
        return findByHash( key.getHash() ) != nullptr;
    }

    bool LocalizationManager::hasStringInLanguage( string_view languageCode, const hashed_string& key ) const
    {
        const string                        code = normalizeLanguageCode( languageCode );
        std::shared_lock<std::shared_mutex> lock( _mutex );
        const auto                          it = _mapLanguageTable.find( code );
        return it != _mapLanguageTable.end() && it->second->contains( key );
    }

    const StringTable* LocalizationManager::getLanguageTable( string_view languageCode ) const
    {
        const string                        code = normalizeLanguageCode( languageCode );
        std::shared_lock<std::shared_mutex> lock( _mutex );
        const auto                          it = _mapLanguageTable.find( code );
        return it != _mapLanguageTable.end() ? it->second.get() : nullptr;
    }

    string LocalizationManager::formatText( string_view pattern, const TextArgumentList& arguments ) const
    {
        CultureInfo culture;
        {
            std::shared_lock<std::shared_mutex> lock( _mutex );
            culture = _currentCulture;
        }
        string text;
        string error;
        if ( TextFormatter::format( pattern, arguments, culture, text, &error ) == false )
            SW_LOG_WARNING( "Text format '%#' (culture '%#'): %#", pattern, culture._code.c_str(), error.c_str() );
        return text;
    }

    string LocalizationManager::getFormattedString( const hashed_string& key, const TextArgumentList& arguments, const utf8* pDefaultText ) const
    {
        const utf8* pPattern = getString( key, pDefaultText );
        return formatText( pPattern != nullptr ? string_view( pPattern ) : string_view{}, arguments );
    }

    void LocalizationManager::reportMissingKey( string_view key ) const
    {
        if ( key.empty() )
            return;
        string language;
        {
            std::shared_lock<std::shared_mutex> lock( _mutex );
            if ( _listProject.empty() && _mapLooseTable.empty() )
                return; // 글을 하나도 올리지 않은 실행(로컬라이제이션 없는 게임 · 시험)은 모든 키가 빠진다 — 알릴 것이 아니다
            language = _currentLanguage;
        }
        bool bFirst{ false };
        {
            std::scoped_lock<std::mutex> lock( _missingMutex );
            bFirst = _uniqueMissingKey.insert( string( key ) ).second;
        }
        if ( bFirst )
            SW_LOG_WARNING( "Missing localized text for key '%#' (culture '%#')", key, language.c_str() );
    }

    vector<string> LocalizationManager::getMissingKeys() const
    {
        std::scoped_lock<std::mutex> lock( _missingMutex );
        return vector<string>( _uniqueMissingKey.begin(), _uniqueMissingKey.end() );
    }

    void LocalizationManager::clearMissingKeys()
    {
        std::scoped_lock<std::mutex> lock( _missingMutex );
        _uniqueMissingKey.clear();
    }

    // ------------------------------------------------------------------------------
    // 5) 콜백
    // ------------------------------------------------------------------------------
    uint32 LocalizationManager::registerLanguageChangedCallback( LanguageChangedCallback callback )
    {
        if ( callback == nullptr )
            return 0;
        std::unique_lock<std::shared_mutex> lock( _mutex );
        const uint32                        callbackID = _nextCallbackID++;
        _mapCallback[callbackID]                       = std::move( callback );
        return callbackID;
    }

    void LocalizationManager::unregisterLanguageChangedCallback( uint32 callbackID )
    {
        if ( callbackID == 0 )
            return;
        std::unique_lock<std::shared_mutex> lock( _mutex );
        _mapCallback.erase( callbackID );
    }

    void LocalizationManager::notifyLanguageChanged( string_view oldLanguage, string_view newLanguage )
    {
        _textRevision.fetch_add( 1, std::memory_order_relaxed );
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
        for ( size_t index = 0; index < listCallback.size(); ++index )
        {
            listCallback[index]( oldLanguage, newLanguage );
        }
    }
} // namespace sw
