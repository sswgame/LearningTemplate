#include "pch.h"

#include "Editor/Common/Commands/EditorDataTableCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw::editor
{
    namespace
    {
        struct EditorDataTableCommandsInternal
        {
            static constexpr const utf8* kEngineProject = "engine/localization/engine.locproject.json";

            /** @brief 원문 표들을 읽습니다. 못 읽은 표가 있으면 false 입니다. */
            [[nodiscard]] static bool readSourceTables( const LocalizationSheet& sheet, vector<SourceStringTable>& outListTable )
            {
                outListTable.clear();
                bool bAllRead{ true };
                for ( const string& tablePath : sheet._listTablePath )
                {
                    SourceStringTable& table = outListTable.emplace_back();
                    string             error;
                    if ( table.loadFromFile( tablePath, &error ) == false )
                    {
                        SW_LOG_WARNING( "String table '%#' cannot be read - it is not shown and saving will not overwrite it: %#", tablePath.c_str(), error.c_str() );
                        bAllRead = false;
                    }
                }
                return bAllRead;
            }

            /** @brief 번역 표 하나를 읽습니다 — 파일이 없으면 빈 표로 true, 있는데 못 읽으면 false 입니다. */
            [[nodiscard]] static bool readTranslation( const string& path, string_view culture, TranslationTable& outTable )
            {
                outTable = TranslationTable{};
                outTable.setCulture( culture );
                if ( FileUtil::fileExists( path ) == false )
                    return true;
                string error;
                if ( outTable.loadFromFile( path, &error ) )
                    return true;
                SW_LOG_WARNING( "Translation table '%#' cannot be read - its strings are not shown and saving will not overwrite it: %#", path.c_str(), error.c_str() );
                return false;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorDataTableCommands" );

    string EditorDataTableCommands::getLocalizationFolderPath()
    {
        return ResourceUtil::getDomainFolderPath( GameConfig::getActive()._packRoot, FileUtil::joinPath( path::kDataFolder, path::kLocalizationFolder ) );
    }

    string EditorDataTableCommands::getGameDataFolderPath()
    {
        return ResourceUtil::getDomainFolderPath( GameConfig::getActive()._packRoot, path::kDataFolder );
    }

    void EditorDataTableCommands::collectLocalizationProjects( vector<string>& outListProjectPath )
    {
        outListProjectPath.clear();
        const string enginePath = ResourceUtil::getResourcePath( EditorDataTableCommandsInternal::kEngineProject );
        if ( enginePath.empty() == false && FileUtil::fileExists( enginePath ) )
            outListProjectPath.push_back( FileUtil::normalizeSeparators( enginePath ) );

        const string   gameFolder = getLocalizationFolderPath();
        vector<string> listFile;
        if ( gameFolder.empty() == false && FileUtil::collectFiles( gameFolder, ".json", listFile, false ) )
        {
            for ( const string& filePath : listFile )
            {
                if ( StringUtil::endsWith( filePath, LocalizationProject::kFileSuffix, true ) )
                    outListProjectPath.push_back( FileUtil::normalizeSeparators( filePath ) );
            }
        }
    }

    bool EditorDataTableCommands::loadLocalizationProject( string_view projectPathView, LocalizationSheet& outSheet )
    {
        const string projectPath( projectPathView ); // @p outSheet 의 경로를 넘겨받았을 수 있다 — 비우기 전에 복사한다
        outSheet = LocalizationSheet{};
        LocalizationProject project;
        string              error;
        if ( project.loadFromFile( projectPath, &error ) == false )
        {
            SW_LOG_WARNING( "Localization project cannot be read: %#", error.c_str() );
            return false;
        }
        outSheet._projectPath   = string( projectPath );
        outSheet._sourceCulture = project._sourceCulture;
        outSheet._listCulture   = project._listCulture;
        for ( const string& tableName : project._listStringTable )
            outSheet._listTablePath.push_back( LocalizationProject::makeSiblingPath( projectPath, tableName ) );

        vector<SourceStringTable> listTable;
        bool                      bAllRead = EditorDataTableCommandsInternal::readSourceTables( outSheet, listTable );
        vector<TranslationTable>  listTranslation( outSheet._listCulture.size() );
        for ( size_t cultureIndex = 0; cultureIndex < outSheet._listCulture.size(); ++cultureIndex )
        {
            const string path = LocalizationProject::makeTranslationPath( projectPath, outSheet._listCulture[cultureIndex] );
            bAllRead          = EditorDataTableCommandsInternal::readTranslation( path, outSheet._listCulture[cultureIndex], listTranslation[cultureIndex] ) && bAllRead;
        }

        for ( size_t tableIndex = 0; tableIndex < listTable.size(); ++tableIndex )
        {
            for ( const auto& [key, entry] : listTable[tableIndex].getEntries() )
            {
                LocalizationRecord& record = outSheet._listRecord.emplace_back();
                record._key                = key;
                record._source             = entry._source;
                record._context            = entry._context;
                record._comment            = entry._comment;
                record._maxLength          = entry._maxLength;
                record._tableIndex         = static_cast<uint32>( tableIndex );
                for ( const TranslationTable& translation : listTranslation )
                {
                    const TranslationEntry* pTranslation = translation.findEntry( key );
                    record._listTranslation.push_back( pTranslation != nullptr ? pTranslation->_text : string{} );
                    record._listState.push_back( translation.computeState( key, &entry ) );
                }
            }
        }
        return bAllRead;
    }

    bool EditorDataTableCommands::saveLocalizationProject( LocalizationSheet& inoutSheet )
    {
        // 디스크의 지금 내용 위에 고친 줄만 얹는다 — 수집기가 적은 자리 · 번역가 메모 · 검토 표시를 지킨다.
        vector<SourceStringTable> listTable;
        if ( EditorDataTableCommandsInternal::readSourceTables( inoutSheet, listTable ) == false )
        {
            SW_LOG_ERROR( "Localization is not saved - a string table could not be read (fix it and save again)" );
            return false;
        }
        bool                     bAllWritten{ true };
        vector<TranslationTable> listTranslation( inoutSheet._listCulture.size() );
        vector<uint8>            listWritable( inoutSheet._listCulture.size(), 1 );
        for ( size_t cultureIndex = 0; cultureIndex < inoutSheet._listCulture.size(); ++cultureIndex )
        {
            const string path = LocalizationProject::makeTranslationPath( inoutSheet._projectPath, inoutSheet._listCulture[cultureIndex] );
            if ( EditorDataTableCommandsInternal::readTranslation( path, inoutSheet._listCulture[cultureIndex], listTranslation[cultureIndex] ) == false )
            {
                SW_LOG_ERROR( "Translation table '%#' could not be read - it is not overwritten (fix the file and save again)", path.c_str() );
                listWritable[cultureIndex] = 0;
                bAllWritten                = false;
            }
        }

        for ( const string& removedKey : inoutSheet._listRemovedKey )
        {
            for ( SourceStringTable& table : listTable )
                (void)table.removeEntry( removedKey );
            for ( TranslationTable& translation : listTranslation )
                (void)translation.removeEntry( removedKey );
        }

        for ( const LocalizationRecord& record : inoutSheet._listRecord )
        {
            if ( record._bModified == false || listTable.empty() )
                continue;
            const uint32     tableIndex = record._tableIndex < listTable.size() ? record._tableIndex : 0u;
            SourceTextEntry& source     = listTable[tableIndex].getOrAddEntry( record._key );
            source._source              = record._source;
            source._context             = record._context;
            source._comment             = record._comment;
            source._maxLength           = record._maxLength;
            const uint64 sourceHash     = LocalizationTextUtil::computeSourceHash( record._source );
            for ( size_t cultureIndex = 0; cultureIndex < listTranslation.size() && cultureIndex < record._listTranslation.size(); ++cultureIndex )
            {
                TranslationTable& translation = listTranslation[cultureIndex];
                const string&     text        = record._listTranslation[cultureIndex];
                if ( text.empty() )
                {
                    (void)translation.removeEntry( record._key );
                    continue;
                }
                const TranslationEntry* pExisting = translation.findEntry( record._key );
                if ( pExisting != nullptr && pExisting->_text == text )
                    continue; // 번역은 그대로 — 원문만 고쳤으면 이 번역은 낡은 것이 된다
                TranslationEntry& entry = translation.getOrAddEntry( record._key );
                entry._text             = text;
                entry._sourceHash       = sourceHash;
                entry._bReview          = false;
            }
        }

        for ( size_t tableIndex = 0; tableIndex < listTable.size(); ++tableIndex )
        {
            if ( listTable[tableIndex].saveToFile( inoutSheet._listTablePath[tableIndex] ) == false )
            {
                SW_LOG_ERROR( "String table '%#' could not be written", inoutSheet._listTablePath[tableIndex].c_str() );
                bAllWritten = false;
            }
        }
        for ( size_t cultureIndex = 0; cultureIndex < listTranslation.size(); ++cultureIndex )
        {
            if ( listWritable[cultureIndex] == 0 )
                continue;
            const string path = LocalizationProject::makeTranslationPath( inoutSheet._projectPath, inoutSheet._listCulture[cultureIndex] );
            if ( listTranslation[cultureIndex].getEntries().empty() && FileUtil::fileExists( path ) == false )
                continue; // 번역이 하나도 없는 문화권의 빈 파일은 만들지 않는다
            if ( listTranslation[cultureIndex].saveToFile( path ) == false )
            {
                SW_LOG_ERROR( "Translation table '%#' could not be written", path.c_str() );
                bAllWritten = false;
            }
        }
        if ( bAllWritten == false )
            return false; // 고친 표시를 지우지 않는다 — 저장되지 않은 것이 남아 있다

        for ( LocalizationRecord& record : inoutSheet._listRecord )
            record._bModified = false;
        inoutSheet._listRemovedKey.clear();

        LocalizationManager* pLocalizationManager = editor::getService<LocalizationManager>();
        if ( pLocalizationManager != nullptr )
            (void)pLocalizationManager->reloadChangedFile( inoutSheet._projectPath );
        SW_LOG_INFO( "Saved localization project '%#'.", inoutSheet._projectPath.c_str() );
        return true;
    }

    bool EditorDataTableCommands::collectGameDataFiles( vector<GameDataFileEntry>& outList )
    {
        outList.clear();
        const string dataFolder = getGameDataFolderPath();

        vector<string> listFile;
        FileUtil::collectFiles( dataFolder, ".xml", listFile, false );

        for ( const string& file : listFile )
        {
            if ( FileUtil::hasExtension( file, ".xml" ) == false )
                continue;

            GameDataFileEntry entry{};
            entry._fileName     = FileUtil::getFileNamePart( file );
            entry._absolutePath = FileUtil::normalizeSeparators( file );
            outList.push_back( std::move( entry ) );
        }
        return true;
    }
} // namespace sw::editor
