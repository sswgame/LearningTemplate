#include "pch.h"

#include "Engine/DevTools/LocalizationTools.h"

#include "Core/Container/set.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/MarkupTagScanner.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Localization/CultureInfo.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/PortableObjectFile.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Localization/TranslationMemory.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    SW_LOG_CALLER( "LocalizationTools" );

    namespace
    {
        struct LocalizationToolsInternal
        {
            static constexpr const utf8* kArrCodeExtension[] = { ".h", ".cpp", ".inl" };
            static constexpr const utf8* kDialogueSuffix     = ".dialogue.json";

            /** @brief 리소스 경로 또는 절대 경로를 디스크의 절대 경로로 풉니다. */
            static string resolveAbsolutePath( string_view path )
            {
                if ( FileUtil::exists( path ) || FileUtil::isDirectory( path ) )
                    return FileUtil::normalizeSeparators( path );
                // 리소스 루트 아래의 전역 id(`game/shooter3d/…`)는 활성 게임과 상관없이 그 자리다 — 도구는 모든 팩을 다룬다.
                const string underRoot = FileUtil::joinPath( ResourceUtil::getRootFolderPath(), path );
                if ( FileUtil::exists( underRoot ) || FileUtil::isDirectory( underRoot ) )
                    return FileUtil::normalizeSeparators( underRoot );
                const string resolved = ResourceUtil::getResourcePath( path );
                return resolved.empty() ? string( path ) : FileUtil::normalizeSeparators( resolved );
            }

            /** @brief 글자 수(UTF-8 코드 포인트)입니다. */
            static uint32 countCharacters( string_view text )
            {
                uint32 count{ 0 };
                for ( size_t offset = 0; offset < text.size(); )
                {
                    (void)StringUtil::decodeUtf8( text, offset );
                    ++count;
                }
                return count;
            }

            static void applyAssetRule( const LocalizationAssetRule& rule, const XmlNode& node, TextGatherer& gatherer, string_view origin, uint32 depth )
            {
                if ( depth > 64 )
                    return;
                const utf8* pName = node.getName();
                for ( const string& element : rule._listElement )
                {
                    if ( pName == nullptr || element != pName )
                        continue;
                    const string_view value = node.getAttributeText( rule._attribute.c_str(), false );
                    if ( value.empty() )
                        continue;
                    if ( rule._bKeyReference )
                        gatherer.addKeyReference( value, origin );
                    else
                        gatherer.addTextOrKey( value, rule._context, origin );
                }
                for ( XmlNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    applyAssetRule( rule, child, gatherer, origin, depth + 1 );
                }
            }

            /** @brief 번역 하나를 원문에 대어 검사합니다 — 구문 · 자리표시자 이름 · 리치 텍스트 태그 열 · 최대 길이. */
            static void validateTranslation( const string& culture, const string& key, const SourceTextEntry& source, const TranslationEntry& translation,
                                             TextGatherReport& inoutReport )
            {
                const string location = culture + ":" + key;
                string       error;
                if ( TextFormatter::validatePattern( translation._text, &error ) == false )
                {
                    inoutReport._listIssue.push_back( { location, "translation is not a valid message pattern: " + error, true } );
                    return;
                }
                vector<string> listSourceName;
                vector<string> listTranslationName;
                TextFormatter::collectArgumentNames( source._source, listSourceName );
                TextFormatter::collectArgumentNames( translation._text, listTranslationName );
                if ( listSourceName != listTranslationName )
                    inoutReport._listIssue.push_back( { location, "translation arguments differ from the source arguments", false } );
                if ( MarkupTagScanner::hasSameTags( source._source, translation._text ) == false )
                    inoutReport._listIssue.push_back( { location, "translation rich text tags differ from the source tags", false } );
                const uint32 length = countCharacters( translation._text );
                if ( source._maxLength != 0 && length > source._maxLength )
                    inoutReport._listIssue.push_back( { location, "translation is " + to_string( length ) + " characters, over maxLength " + to_string( source._maxLength ), false } );
            }

            /** @brief 번역 메모리 경로(`<프로젝트 폴더>/tm/<culture>.tm.json`)입니다. */
            static string makeMemoryPath( string_view projectPath, string_view culture )
            {
                return LocalizationProject::makeSiblingPath( projectPath, string( TranslationMemory::kFolderName ) + "/" + CultureTable::normalizeCode( culture ) +
                                                                              TranslationMemory::kExtension );
            }

            /** @brief 번역 메모리를 읽습니다(없으면 빈 메모리). 못 읽으면 문제로 알리고 false 입니다. */
            [[nodiscard]] static bool loadMemory( const string& path, string_view culture, TranslationMemory& outMemory, TextGatherReport& inoutReport )
            {
                outMemory = TranslationMemory{};
                outMemory.setCulture( culture );
                string error;
                if ( FileUtil::exists( path ) && outMemory.loadFromFile( path, &error ) == false )
                {
                    inoutReport._listIssue.push_back( { path, error, true } );
                    return false;
                }
                return true;
            }

            /**
             * @brief 문화권 번역 표 하나를 수집 결과에 맞춥니다 — 해시 없는 번역에 해시를 찍고, 지금 번역을 번역 메모리에 쌓고, 원문 표에 없는 키의 번역을 빼고,
             *        없는 · 낡은 번역을 메모리로 미리 채운 뒤 상태를 세고 검사합니다.
             */
            static void updateTranslation( const string& projectPath, const string& culture, const map<string, const SourceTextEntry*>& mapSource, bool bWrite,
                                           LocalizationGatherResult& inoutResult )
            {
                LocalizationCultureReport& cultureReport = inoutResult._listCulture.emplace_back();
                cultureReport._culture                   = culture;
                const string     path                    = LocalizationProject::makeTranslationPath( projectPath, culture );
                const string     memoryPath              = makeMemoryPath( projectPath, culture );
                TranslationTable translation;
                translation.setCulture( culture );
                string error;
                if ( FileUtil::exists( path ) && translation.loadFromFile( path, &error ) == false )
                {
                    inoutResult._report._listIssue.push_back( { path, error, true } );
                    return;
                }
                TranslationMemory memory;
                if ( loadMemory( memoryPath, culture, memory, inoutResult._report ) == false )
                    return;
                const string memoryBefore = memory.toJsonText();

                // 1) 해시 없는 번역은 지금 원문의 번역으로 받고, 지금 번역은 메모리에 쌓고, 원문이 없는 키의 번역은 뺀다.
                vector<string> listOrphan;
                for ( auto& [key, entry] : translation.getMutableEntries() )
                {
                    const auto sourceIt = mapSource.find( key );
                    if ( sourceIt == mapSource.end() )
                    {
                        listOrphan.push_back( key );
                        continue;
                    }
                    if ( entry._sourceHash == 0 )
                    {
                        entry._sourceHash       = LocalizationTextUtil::computeSourceHash( sourceIt->second->_source );
                        cultureReport._bChanged = true;
                    }
                    if ( translation.computeState( key, sourceIt->second ) == TranslationState::Current )
                        (void)memory.addPair( sourceIt->second->_source, entry._text );
                }
                for ( const string& key : listOrphan )
                {
                    (void)translation.removeEntry( key ); // 번역이 없던 고아 키면 false — 할 일이 없다
                    ++cultureReport._orphanCount;
                    cultureReport._bChanged = true;
                }

                // 2) 없는 번역 · 낡은 번역을 메모리로 채운다 — 같은 원문은 그대로, 비슷한 원문은 검토 표시(없는 칸만).
                for ( const auto& [key, pSource] : mapSource )
                {
                    const TranslationState state = translation.computeState( key, pSource );
                    if ( state != TranslationState::Missing && state != TranslationState::Stale )
                        continue;
                    TranslationMemoryMatch match;
                    if ( memory.findBestMatch( pSource->_source, match ) == false )
                        continue;
                    if ( match._bExact == false && state == TranslationState::Stale )
                        continue; // 낡은 번역은 옛 글을 그대로 둔다 — 번역가가 PO 에서 옛 원문과 함께 본다
                    TranslationEntry& entry = translation.getOrAddEntry( key );
                    entry._text             = match._text;
                    entry._sourceHash       = LocalizationTextUtil::computeSourceHash( pSource->_source );
                    entry._bReview          = match._bExact == false;
                    cultureReport._bChanged = true;
                    if ( match._bExact )
                        ++cultureReport._prefilledExact;
                    else
                        ++cultureReport._prefilledFuzzy;
                }

                // 3) 상태 · 검사
                for ( const auto& [key, pSource] : mapSource )
                {
                    const TranslationEntry* pEntry = translation.findEntry( key );
                    if ( pEntry != nullptr )
                        validateTranslation( culture, key, *pSource, *pEntry, inoutResult._report );
                    switch ( translation.computeState( key, pSource ) )
                    {
                        case TranslationState::Current:
                        {
                            ++cultureReport._currentCount;
                            break;
                        }
                        case TranslationState::Stale:
                        {
                            ++cultureReport._staleCount;
                            break;
                        }
                        case TranslationState::Review:
                        {
                            ++cultureReport._reviewCount;
                            break;
                        }
                        case TranslationState::Missing:
                        {
                            ++cultureReport._missingCount;
                            break;
                        }
                        case TranslationState::Orphan:
                        {
                            break;
                        }
                    }
                }

                const bool bMemoryChanged = memory.toJsonText() != memoryBefore;
                if ( cultureReport._bChanged == false && bMemoryChanged == false )
                    return;
                cultureReport._bChanged = true;
                if ( bWrite == false )
                {
                    inoutResult._bOutOfDate = true;
                    return;
                }
                const bool bHasTranslations = translation.getEntries().empty() == false || FileUtil::exists( path );
                if ( bHasTranslations && translation.saveToFile( path ) == false )
                    inoutResult._report._listIssue.push_back( { path, "translation table could not be written", true } );
                if ( bMemoryChanged && memory.saveToFile( memoryPath ) == false )
                    inoutResult._report._listIssue.push_back( { memoryPath, "translation memory could not be written", true } );
            }

            /** @brief PO 하나를 만듭니다 — 원문 표의 모든 키, 사전 순. */
            static PortableObjectFile makePortableObject( const LocalizationProject& project, string_view culture, const map<string, const SourceTextEntry*>& mapSource,
                                                          const TranslationTable& translation, const TranslationMemory& memory, LocalizationExchangeResult& outResult )
            {
                PortableObjectFile file;
                file._language    = string( culture );
                file._projectName = project._name;
                for ( const auto& [key, pSource] : mapSource )
                {
                    PortableObjectEntry& entry = file._listEntry.emplace_back();
                    entry._context             = key;
                    entry._source              = pSource->_source;
                    if ( pSource->_context.empty() == false )
                        entry._listExtractedComment.push_back( "Context: " + pSource->_context );
                    if ( pSource->_comment.empty() == false )
                        entry._listExtractedComment.push_back( pSource->_comment );
                    if ( pSource->_maxLength != 0 )
                        entry._listExtractedComment.push_back( "Max length: " + to_string( pSource->_maxLength ) );
                    for ( const string& origin : pSource->_listOrigin )
                    {
                        entry._listReference.push_back( origin );
                    }
                    ++outResult._entryCount;

                    const TranslationEntry* pTranslation = translation.findEntry( key );
                    if ( pTranslation == nullptr )
                        continue;
                    entry._translation       = pTranslation->_text;
                    entry._translatorComment = pTranslation->_translatorComment;
                    ++outResult._translatedCount;
                    const TranslationState state = translation.computeState( key, pSource );
                    entry._bFuzzy                = state == TranslationState::Review || state == TranslationState::Stale;
                    outResult._fuzzyCount += entry._bFuzzy ? 1u : 0u;
                    if ( state == TranslationState::Stale )
                    {
                        entry._previousSource = memory.findSourceOfText( pTranslation->_text );
                        ++outResult._staleCount;
                    }
                }
                return file;
            }

            /** @brief 프로젝트 · 원문 표들을 읽어 키 → 원문 표 줄을 만듭니다. */
            [[nodiscard]] static bool loadProjectSources( string_view projectPath, LocalizationProject& outProject, vector<SourceStringTable>& outListTable,
                                                          map<string, const SourceTextEntry*>& outMapSource, string& outAbsolutePath )
            {
                outAbsolutePath = resolveAbsolutePath( projectPath );
                string error;
                if ( outProject.loadFromFile( outAbsolutePath, &error ) == false )
                {
                    SW_LOG_ERROR( "Localization project is not loaded: %#", error.c_str() );
                    return false;
                }
                outListTable.assign( outProject._listStringTable.size(), SourceStringTable{} );
                outMapSource.clear();
                for ( size_t tableIndex = 0; tableIndex < outListTable.size(); ++tableIndex )
                {
                    const string tablePath = LocalizationProject::makeSiblingPath( outAbsolutePath, outProject._listStringTable[tableIndex] );
                    if ( outListTable[tableIndex].loadFromFile( tablePath, &error ) == false )
                    {
                        SW_LOG_ERROR( "String table is not loaded: %#", error.c_str() );
                        return false;
                    }
                }
                for ( const SourceStringTable& table : outListTable )
                {
                    for ( const auto& [key, entry] : table.getEntries() )
                    {
                        outMapSource.emplace( key, &entry );
                    }
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string LocalizationTools::findRepositoryRoot()
    {
        return FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( ResourceUtil::getRootFolderPath() ) );
    }

    void LocalizationTools::collectProjectPaths( vector<string>& outListProjectPath, bool bAllGames )
    {
        outListProjectPath.clear();
        const string engineProject = engine::areEngineServicesBound() ? engine::getEngineDefaultAssets()._localizationProject : EngineDefaultAssets{}._localizationProject;
        if ( engineProject.empty() == false )
            outListProjectPath.push_back( LocalizationToolsInternal::resolveAbsolutePath( engineProject ) );

        const string   localizationFolder = FileUtil::joinPath( path::kDataFolder, path::kLocalizationFolder );
        vector<string> listGameFolder;
        if ( bAllGames )
        {
            vector<string> listPack;
            if ( FileUtil::collectFolders( FileUtil::joinPath( ResourceUtil::getRootFolderPath(), "game" ), listPack, false ) )
            {
                std::sort( listPack.begin(), listPack.end() );
                for ( const string& packFolder : listPack )
                {
                    listGameFolder.push_back( FileUtil::joinPath( packFolder, localizationFolder ) );
                }
            }
        }
        else
            listGameFolder.push_back( ResourceUtil::getDomainFolderPath( GameConfig::getActive()._packRoot, localizationFolder ) );

        for ( const string& gameFolder : listGameFolder )
        {
            vector<string> listFile;
            if ( gameFolder.empty() || FileUtil::isDirectory( gameFolder ) == false || FileUtil::collectFiles( gameFolder, ".json", listFile, false ) == false )
                continue;
            for ( const string& filePath : listFile )
            {
                if ( StringUtil::endsWith( filePath, LocalizationProject::kExtension, true ) )
                    outListProjectPath.push_back( FileUtil::normalizeSeparators( filePath ) );
            }
        }
    }

    void LocalizationTools::gatherAssetFile( const LocalizationProject& project, TextGatherer& gatherer, string_view fileText, string_view origin )
    {
        if ( StringUtil::endsWith( origin, LocalizationToolsInternal::kDialogueSuffix, true ) )
        {
            DialogueGraphAsset dialogue;
            if ( dialogue.parseJson( fileText ) == false )
            {
                gatherer.addIssue( origin, "dialogue cannot be parsed", true );
                return;
            }
            dialogue.collectLocalizableText( gatherer, origin );
            return;
        }
        if ( StringUtil::endsWith( origin, ".xml", true ) == false )
            return;

        for ( const LocalizationAssetRule& rule : project._listAssetRule )
        {
            if ( StringUtil::endsWith( origin, rule._fileSuffix, true ) == false )
                continue;
            XmlDocument document;
            if ( document.parse( fileText, origin ) == false )
            {
                gatherer.addIssue( origin, "XML cannot be parsed: " + document.getLastError(), true );
                return;
            }
            LocalizationToolsInternal::applyAssetRule( rule, document.getRoot(), gatherer, origin, 0 );
        }
        gatherer.gatherReflectedXml( fileText, origin );
    }

    bool LocalizationTools::gatherProject( string_view projectPath, string_view repositoryRoot, bool bWrite, LocalizationGatherResult& outResult )
    {
        outResult                    = LocalizationGatherResult{};
        const string        absolute = LocalizationToolsInternal::resolveAbsolutePath( projectPath );
        LocalizationProject project;
        string              error;
        if ( project.loadFromFile( absolute, &error ) == false )
        {
            SW_LOG_ERROR( "Localization project is not loaded: %#", error.c_str() );
            return false;
        }
        outResult._projectName = project._name;

        vector<SourceStringTable> listTable( project._listStringTable.size() );
        for ( size_t tableIndex = 0; tableIndex < listTable.size(); ++tableIndex )
        {
            const string tablePath = LocalizationProject::makeSiblingPath( absolute, project._listStringTable[tableIndex] );
            if ( FileUtil::exists( tablePath ) == false )
            {
                listTable[tableIndex].setCulture( project._sourceCulture ); // 첫 수집이 만든다
                continue;
            }
            if ( listTable[tableIndex].loadFromFile( tablePath, &error ) == false )
            {
                SW_LOG_ERROR( "String table is not loaded: %#", error.c_str() );
                return false;
            }
        }
        SourceStringTable&               gatherTable = listTable.front();
        vector<const SourceStringTable*> listOtherTable;
        for ( size_t tableIndex = 1; tableIndex < listTable.size(); ++tableIndex )
        {
            listOtherTable.push_back( &listTable[tableIndex] );
        }
        outResult._gatherTablePath = LocalizationProject::makeSiblingPath( absolute, project._listStringTable.front() );

        // 1) 코드
        TextGatherer gatherer;
        for ( const string& codeRoot : project._listCodeRoot )
        {
            const string   codeFolder = FileUtil::joinPath( repositoryRoot, codeRoot );
            vector<string> listFile;
            if ( FileUtil::isDirectory( codeFolder ) == false || FileUtil::collectFiles( codeFolder, "", listFile, true ) == false )
            {
                gatherer.addIssue( codeRoot, "code root does not exist", true );
                continue;
            }
            std::sort( listFile.begin(), listFile.end() );
            for ( const string& filePath : listFile )
            {
                bool bCode{ false };
                for ( const utf8* pExtension : LocalizationToolsInternal::kArrCodeExtension )
                {
                    bCode = bCode || FileUtil::hasExtension( filePath, pExtension );
                }
                string text;
                if ( bCode == false || FileUtil::readTextFile( filePath, text ) == false )
                    continue;
                string relative;
                if ( FileUtil::makeRelativePath( repositoryRoot, filePath, relative ) == false )
                    relative = filePath;
                gatherer.gatherCodeText( text, FileUtil::normalizeSeparators( relative ) );
                gatherer.countFile();
            }
        }

        // 2) 데이터
        for ( const string& assetRoot : project._listAssetRoot )
        {
            const string   assetFolder = LocalizationToolsInternal::resolveAbsolutePath( assetRoot );
            vector<string> listFile;
            if ( FileUtil::isDirectory( assetFolder ) == false || FileUtil::collectFiles( assetFolder, "", listFile, true ) == false )
            {
                gatherer.addIssue( assetRoot, "asset root does not exist", true );
                continue;
            }
            std::sort( listFile.begin(), listFile.end() );
            for ( const string& filePath : listFile )
            {
                const bool bCandidate = FileUtil::hasExtension( filePath, ".xml" ) || StringUtil::endsWith( filePath, LocalizationToolsInternal::kDialogueSuffix, true );
                string     text;
                if ( bCandidate == false || FileUtil::readTextFile( filePath, text ) == false )
                    continue;
                gatherAssetFile( project, gatherer, FileUtil::skipUtf8Bom( text ), ResourceUtil::toResourceId( filePath ) );
                gatherer.countFile();
            }
        }
        outResult._fileCount = gatherer.getFileCount();

        // 3) 합치기 · 쓰기
        outResult._report    = gatherer.mergeInto( gatherTable, listOtherTable );
        const string newText = gatherTable.toJsonText();
        string       diskText;
        const bool   bSameOnDisk = FileUtil::exists( outResult._gatherTablePath ) && FileUtil::readTextFile( outResult._gatherTablePath, diskText ) && diskText == newText;
        if ( bSameOnDisk == false )
        {
            if ( bWrite )
                outResult._bWritten = gatherTable.saveToFile( outResult._gatherTablePath );
            else
                outResult._bOutOfDate = true;
        }

        // 4) 번역 표 — 해시 찍기 · 상태 · 검사
        map<string, const SourceTextEntry*> mapSource;
        for ( const SourceStringTable& table : listTable )
        {
            for ( const auto& [key, entry] : table.getEntries() )
            {
                mapSource.emplace( key, &entry );
            }
        }
        for ( const string& culture : project._listCulture )
        {
            LocalizationToolsInternal::updateTranslation( absolute, culture, mapSource, bWrite, outResult );
        }
        return true;
    }

    void LocalizationTools::logGatherResult( const LocalizationGatherResult& result )
    {
        const TextGatherReport& report = result._report;
        SW_LOG_INFO( "[GatherText] project '%#': %# files, +%# added, ~%# changed, -%# removed, %# unchanged%#", result._projectName.c_str(), result._fileCount,
                     report._listAdded.size(), report._listChanged.size(), report._listRemoved.size(), report._unchangedCount,
                     result._bOutOfDate ? " (OUT OF DATE)" : ( result._bWritten ? " (written)" : "" ) );
        // 줄을 모아 한 번에 쓴다 — 정보 로그가 빠지는 배포 구성에서도 같은 코드다.
        string details;
        for ( const string& key : report._listAdded )
        {
            details.append( "\n  + " ).append( key );
        }
        for ( const string& key : report._listChanged )
        {
            details.append( "\n  ~ " ).append( key );
        }
        for ( const string& key : report._listRemoved )
        {
            details.append( "\n  - " ).append( key );
        }
        for ( const LocalizationCultureReport& culture : result._listCulture )
        {
            details.append( "\n  " ).append( culture._culture ).append( ": " ).append( to_string( culture._currentCount ) ).append( " current, " );
            details.append( to_string( culture._staleCount ) ).append( " stale, " ).append( to_string( culture._reviewCount ) ).append( " review, " );
            details.append( to_string( culture._missingCount ) ).append( " missing, " ).append( to_string( culture._orphanCount ) ).append( " orphan removed, " );
            details.append( to_string( culture._prefilledExact ) ).append( " exact + " ).append( to_string( culture._prefilledFuzzy ) ).append( " fuzzy from translation memory" );
        }
        if ( details.empty() == false )
            SW_LOG_INFO( "[GatherText] %#%#", result._projectName.c_str(), details.c_str() );
        for ( const TextGatherIssue& issue : report._listIssue )
        {
            if ( issue._bError )
                SW_LOG_ERROR( "[GatherText] %#: %#", issue._location.c_str(), issue._message.c_str() );
            else
                SW_LOG_WARNING( "[GatherText] %#: %#", issue._location.c_str(), issue._message.c_str() );
        }
    }

    bool LocalizationTools::runGatherCommand( bool bCheckOnly, string_view projectArgument )
    {
        // 배포본은 속성 메타(Localizable)를 싣지 않아 UI 문서의 글을 못 알아본다 — 그대로 쓰면 원문 표에서 그 글을 모두 지운다.
#if defined( SW_SHIPPING )
        constexpr bool kHasPropertyMeta = false;
#else
        constexpr bool kHasPropertyMeta = true;
#endif
        if constexpr ( kHasPropertyMeta == false )
        {
            SW_LOG_ERROR( "[GatherText] the shipping build carries no property metadata - gather text from a development build" );
            return false;
        }
        vector<string> listProjectPath;
        if ( projectArgument.empty() || projectArgument == kAllProjects )
            collectProjectPaths( listProjectPath, projectArgument == kAllProjects );
        else
            listProjectPath.push_back( string( projectArgument ) );
        if ( listProjectPath.empty() )
        {
            SW_LOG_ERROR( "[GatherText] no localization project to gather" );
            return false;
        }

        const string repositoryRoot = findRepositoryRoot();
        bool         bSucceeded{ true };
        for ( const string& projectPath : listProjectPath )
        {
            LocalizationGatherResult result;
            if ( gatherProject( projectPath, repositoryRoot, bCheckOnly == false, result ) == false )
            {
                bSucceeded = false;
                continue;
            }
            logGatherResult( result );
            bSucceeded = bSucceeded && result._report.hasErrors() == false && ( bCheckOnly == false || result._bOutOfDate == false );
        }
        return bSucceeded;
    }

    bool LocalizationTools::exportProjectPo( string_view projectPath, vector<LocalizationExchangeResult>& outListResult )
    {
        outListResult.clear();
        LocalizationProject                 project;
        vector<SourceStringTable>           listTable;
        map<string, const SourceTextEntry*> mapSource;
        string                              absolute;
        if ( LocalizationToolsInternal::loadProjectSources( projectPath, project, listTable, mapSource, absolute ) == false )
            return false;

        bool bAllWritten{ true };
        for ( const string& culture : project._listCulture )
        {
            LocalizationExchangeResult& result = outListResult.emplace_back();
            result._culture                    = culture;
            result._path                       = LocalizationProject::makeSiblingPath( absolute, string( PortableObjectFile::kFolderName ) + "/" + culture + PortableObjectFile::kExtension );
            const string     translationPath   = LocalizationProject::makeTranslationPath( absolute, culture );
            TranslationTable translation;
            translation.setCulture( culture );
            string error;
            if ( FileUtil::exists( translationPath ) && translation.loadFromFile( translationPath, &error ) == false )
            {
                SW_LOG_ERROR( "[ExportPo] %#", error.c_str() );
                bAllWritten = false;
                continue;
            }
            // 번역 메모리가 깨졌으면 그 문화권은 내보내지 않는다(번역 표와 같다) — 빈 메모리로 내보내면 옛 원문 · 제안이 조용히 빠진 PO 가 나간다.
            TextGatherReport  memoryReport;
            TranslationMemory memory;
            if ( LocalizationToolsInternal::loadMemory( LocalizationToolsInternal::makeMemoryPath( absolute, culture ), culture, memory, memoryReport ) == false )
            {
                for ( const TextGatherIssue& issue : memoryReport._listIssue )
                {
                    SW_LOG_ERROR( "[ExportPo] translation memory '%#' cannot be read: %#", issue._location.c_str(), issue._message.c_str() );
                }
                bAllWritten = false;
                continue;
            }
            const PortableObjectFile file = LocalizationToolsInternal::makePortableObject( project, culture, mapSource, translation, memory, result );
            const string             text = file.toText();
            string                   existing;
            const bool               bSame = FileUtil::exists( result._path ) && FileUtil::readTextFile( result._path, existing ) && existing == text;
            if ( bSame == false && ( FileUtil::ensureParentDirectoryExists( result._path ) == false || FileUtil::writeTextFile( result._path, text ) == false ) )
            {
                SW_LOG_ERROR( "[ExportPo] '%#' could not be written", result._path.c_str() );
                bAllWritten = false;
            }
        }
        return bAllWritten;
    }

    bool LocalizationTools::importPo( string_view projectPath, string_view poPath, LocalizationExchangeResult& outResult )
    {
        outResult       = LocalizationExchangeResult{};
        outResult._path = string( poPath );
        LocalizationProject                 project;
        vector<SourceStringTable>           listTable;
        map<string, const SourceTextEntry*> mapSource;
        string                              absolute;
        if ( LocalizationToolsInternal::loadProjectSources( projectPath, project, listTable, mapSource, absolute ) == false )
            return false;

        string             text;
        string             error;
        PortableObjectFile file;
        if ( FileUtil::readTextFile( poPath, text ) == false || file.parse( text, &error ) == false )
        {
            SW_LOG_ERROR( "[ImportPo] '%#' cannot be read: %#", poPath, error.c_str() );
            return false;
        }
        const string culture = CultureTable::normalizeCode( file._language );
        bool         bListed{ false };
        for ( const string& projectCulture : project._listCulture )
        {
            bListed = bListed || projectCulture == culture;
        }
        if ( bListed == false )
        {
            SW_LOG_ERROR( "[ImportPo] '%#' is for culture '%#', which project '%#' does not list", poPath, culture.c_str(), project._name.c_str() );
            return false;
        }
        if ( file._projectName.empty() == false && file._projectName != project._name )
        {
            SW_LOG_ERROR( "[ImportPo] '%#' was exported from project '%#', not '%#'", poPath, file._projectName.c_str(), project._name.c_str() );
            return false;
        }
        outResult._culture = culture;

        const string     translationPath = LocalizationProject::makeTranslationPath( absolute, culture );
        TranslationTable translation;
        translation.setCulture( culture );
        if ( FileUtil::exists( translationPath ) && translation.loadFromFile( translationPath, &error ) == false )
        {
            SW_LOG_ERROR( "[ImportPo] %#", error.c_str() );
            return false;
        }
        TextGatherReport  memoryReport;
        TranslationMemory memory;
        const string      memoryPath = LocalizationToolsInternal::makeMemoryPath( absolute, culture );
        if ( LocalizationToolsInternal::loadMemory( memoryPath, culture, memory, memoryReport ) == false )
        {
            for ( const TextGatherIssue& issue : memoryReport._listIssue )
            {
                SW_LOG_ERROR( "[ImportPo] translation memory '%#' cannot be read: %#", issue._location.c_str(), issue._message.c_str() );
            }
            return false;
        }

        for ( const PortableObjectEntry& poEntry : file._listEntry )
        {
            ++outResult._entryCount;
            const auto sourceIt = mapSource.find( poEntry._context );
            if ( poEntry._context.empty() || sourceIt == mapSource.end() )
            {
                SW_LOG_WARNING( "[ImportPo] key '%#' is not in the string tables - skipped", poEntry._context.c_str() );
                ++outResult._unknownKeyCount;
                continue;
            }
            if ( poEntry._translation.empty() )
                continue; // 번역하지 않은 항목 — 있던 번역은 그대로 둔다
            TranslationEntry& entry  = translation.getOrAddEntry( poEntry._context );
            entry._text              = poEntry._translation;
            entry._translatorComment = poEntry._translatorComment;
            entry._sourceHash        = LocalizationTextUtil::computeSourceHash( poEntry._source ); // 번역가가 본 원문
            entry._bReview           = poEntry._bFuzzy;
            ++outResult._translatedCount;
            outResult._fuzzyCount += poEntry._bFuzzy ? 1u : 0u;
            const bool bStale = poEntry._source != sourceIt->second->_source;
            outResult._staleCount += bStale ? 1u : 0u;
            if ( bStale == false && poEntry._bFuzzy == false )
                (void)memory.addPair( poEntry._source, poEntry._translation );
        }
        if ( translation.saveToFile( translationPath ) == false || memory.saveToFile( memoryPath ) == false )
        {
            SW_LOG_ERROR( "[ImportPo] translation table '%#' could not be written", translationPath.c_str() );
            return false;
        }
        return true;
    }

    bool LocalizationTools::runExportCommand( string_view projectArgument )
    {
        vector<string> listProjectPath;
        if ( projectArgument.empty() || projectArgument == kAllProjects )
            collectProjectPaths( listProjectPath, projectArgument == kAllProjects );
        else
            listProjectPath.push_back( string( projectArgument ) );
        bool bSucceeded = listProjectPath.empty() == false;
        for ( const string& projectPath : listProjectPath )
        {
            vector<LocalizationExchangeResult> listResult;
            bSucceeded = exportProjectPo( projectPath, listResult ) && bSucceeded;
            string summary;
            for ( const LocalizationExchangeResult& result : listResult )
            {
                summary.append( "\n  " ).append( result._culture ).append( " -> " ).append( result._path ).append( ": " ).append( to_string( result._entryCount ) );
                summary.append( " entries, " ).append( to_string( result._translatedCount ) ).append( " translated, " ).append( to_string( result._fuzzyCount ) );
                summary.append( " fuzzy (" ).append( to_string( result._staleCount ) ).append( " stale)" );
            }
            SW_LOG_INFO( "[ExportPo] %#%#", projectPath.c_str(), summary.c_str() );
        }
        return bSucceeded;
    }

    bool LocalizationTools::runImportCommand( string_view poPath, string_view projectArgument )
    {
        string projectPath( projectArgument );
        if ( projectPath.empty() )
        {
            // PO 머리의 프로젝트 이름으로 기본 대상 중에서 고른다.
            string             text;
            PortableObjectFile file;
            if ( FileUtil::readTextFile( poPath, text ) == false || file.parse( text ) == false )
            {
                SW_LOG_ERROR( "[ImportPo] '%#' cannot be read", poPath );
                return false;
            }
            vector<string> listProjectPath;
            collectProjectPaths( listProjectPath, true );
            for ( const string& candidate : listProjectPath )
            {
                LocalizationProject project;
                if ( project.loadFromFile( candidate ) && project._name == file._projectName )
                    projectPath = candidate;
            }
            if ( projectPath.empty() )
            {
                SW_LOG_ERROR( "[ImportPo] no project named '%#' - pass -loc-project=<project file>", file._projectName.c_str() );
                return false;
            }
        }
        LocalizationExchangeResult result;
        if ( importPo( projectPath, poPath, result ) == false )
            return false;
        SW_LOG_INFO( "[ImportPo] %# <- %#: %# entries, %# translations imported, %# fuzzy, %# for an older source, %# unknown keys", result._culture.c_str(), poPath,
                     result._entryCount, result._translatedCount, result._fuzzyCount, result._staleCount, result._unknownKeyCount );
        return true;
    }
} // namespace sw
