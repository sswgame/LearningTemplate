#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorDataTableCommands.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw::editor
{
    namespace
    {
        struct EditorDataTableCommandsInternal
        {
            /** @brief 원문 테이블들을 읽습니다. 못 읽은 테이블이 있으면 false 입니다. */
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

            /** @brief 번역 테이블 하나를 읽습니다. 파일이 없으면 빈 테이블로 true 이고, 있는데 못 읽으면 false 입니다. */
            [[nodiscard]] static bool readTranslation( const string& path, string_view culture, TranslationTable& outTable )
            {
                outTable = TranslationTable{};
                outTable.setCulture( culture );
                if ( FileUtil::exists( path ) == false )
                    return true;
                string error;
                if ( outTable.loadFromFile( path, &error ) )
                    return true;
                SW_LOG_WARNING( "Translation table '%#' cannot be read - its strings are not shown and saving will not overwrite it: %#", path.c_str(), error.c_str() );
                return false;
            }

            /** @brief 지운 키를 모든 표에서 빼고, 고친 줄을 원문 · 번역 표에 적용합니다. 번역이 그대로면 두고(원문만 고쳤으면 그 번역은 낡은 것이 된다), 빈 번역은 지웁니다. */
            static void applySheetEdits( const LocalizationSheet& sheet, vector<SourceStringTable>& inoutListTable, vector<TranslationTable>& inoutListTranslation )
            {
                for ( const string& removedKey : sheet._listRemovedKey )
                {
                    for ( SourceStringTable& table : inoutListTable )
                    {
                        (void)table.removeEntry( removedKey ); // 그 테이블에 없는 키면 false 이고 할 일이 없다
                    }
                    for ( TranslationTable& translation : inoutListTranslation )
                    {
                        (void)translation.removeEntry( removedKey ); // 번역이 없는 키면 false 이고 할 일이 없다
                    }
                }

                for ( const LocalizationRecord& record : sheet._listRecord )
                {
                    if ( record._bModified == false || inoutListTable.empty() )
                        continue;
                    const uint32     tableIndex = record._tableIndex < inoutListTable.size() ? record._tableIndex : 0u;
                    SourceTextEntry& source     = inoutListTable[tableIndex].getOrAddEntry( record._key );
                    source._source              = record._source;
                    source._context             = record._context;
                    source._comment             = record._comment;
                    source._maxLength           = record._maxLength;
                    const uint64 sourceHash     = LocalizationTextUtil::computeSourceHash( record._source );
                    for ( size_t cultureIndex = 0; cultureIndex < inoutListTranslation.size() && cultureIndex < record._listTranslation.size(); ++cultureIndex )
                    {
                        TranslationTable& translation = inoutListTranslation[cultureIndex];
                        const string&     text        = record._listTranslation[cultureIndex];
                        if ( text.empty() )
                        {
                            (void)translation.removeEntry( record._key ); // 번역이 없던 키면 false 이고 할 일이 없다
                            continue;
                        }
                        const TranslationEntry* pExisting = translation.findEntry( record._key );
                        if ( pExisting != nullptr && pExisting->_text == text )
                            continue; // 번역은 그대로 둔다. 원문만 고쳤으면 이 번역은 낡은 것이 된다
                        TranslationEntry& entry = translation.getOrAddEntry( record._key );
                        entry._text             = text;
                        entry._sourceHash       = sourceHash;
                        entry._bReview          = false;
                    }
                }
            }

            /**
             * @brief 원문 표와 번역 표를 파일에 씁니다. 읽지 못해 덮어쓰지 않기로 한 문화권(@p listWritable 이 0)은 건너뛰고, 번역이 하나도 없는 문화권의 빈 파일은 만들지 않습니다.
             * @return 하나라도 쓰지 못하면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool writeSheetTables( const LocalizationSheet& sheet, const vector<SourceStringTable>& listTable, const vector<TranslationTable>& listTranslation,
                                                        const vector<uint8>& listWritable )
            {
                bool bAllWritten{ true };
                for ( size_t tableIndex = 0; tableIndex < listTable.size(); ++tableIndex )
                {
                    if ( listTable[tableIndex].saveToFile( sheet._listTablePath[tableIndex] ) == false )
                    {
                        SW_LOG_ERROR( "String table '%#' could not be written", sheet._listTablePath[tableIndex].c_str() );
                        bAllWritten = false;
                    }
                }
                for ( size_t cultureIndex = 0; cultureIndex < listTranslation.size(); ++cultureIndex )
                {
                    if ( listWritable[cultureIndex] == 0 )
                        continue;
                    const string path = LocalizationProject::makeTranslationPath( sheet._projectPath, sheet._listCulture[cultureIndex] );
                    if ( listTranslation[cultureIndex].getEntries().empty() && FileUtil::exists( path ) == false )
                        continue; // 번역이 하나도 없는 문화권의 빈 파일은 만들지 않는다
                    if ( listTranslation[cultureIndex].saveToFile( path ) == false )
                    {
                        SW_LOG_ERROR( "Translation table '%#' could not be written", path.c_str() );
                        bAllWritten = false;
                    }
                }
                return bAllWritten;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorDataTableCommands" );

    string EditorDataTableCommands::getLocalizationFolderPath()
    {
        return ResourcePaths::findLooseDomainFolder( editor::getSubsystem<const GameConfig>()._packRoot.get(), FileUtil::joinPath( path::kDataFolder, path::kLocalizationFolder ) ).getDiskPath();
    }

    string EditorDataTableCommands::getGameDataFolderPath()
    {
        return ResourcePaths::findLooseDomainFolder( editor::getSubsystem<const GameConfig>()._packRoot.get(), path::kDataFolder ).getDiskPath();
    }

    void EditorDataTableCommands::collectLocalizationProjects( vector<string>& outListProjectPath )
    {
        outListProjectPath.clear();
        // 엔진 문자열 프로젝트 경로의 원본은 데이터에서 읽은 엔진 기본 에셋 테이블이다. 테이블이 없으면 대신 쓸 값이 없으므로 오류로 알리고 엔진 프로젝트를 빼고 모은다.
        const EngineDefaultAssets* pDefaultAssets = editor::findSubsystem<const EngineDefaultAssets>();
        if ( pDefaultAssets == nullptr )
            SW_LOG_ERROR( "Engine default assets are not loaded - the engine localization project is left out" );
        const string enginePath = pDefaultAssets != nullptr ? ResourcePaths::findLooseFile( pDefaultAssets->_localizationProject.get() ).getDiskPath() : string{};
        if ( enginePath.empty() == false && FileUtil::exists( enginePath ) )
            outListProjectPath.push_back( FileUtil::normalizeSeparators( enginePath ) );

        const string   gameFolder = getLocalizationFolderPath();
        vector<string> listFile;
        if ( gameFolder.empty() == false && FileUtil::collectFiles( gameFolder, ".xml", listFile, false ) )
        {
            for ( const string& filePath : listFile )
            {
                if ( StringUtil::endsWith( filePath, LocalizationProject::kExtension, true ) )
                    outListProjectPath.push_back( FileUtil::normalizeSeparators( filePath ) );
            }
        }
    }

    bool EditorDataTableCommands::loadLocalizationProject( string_view projectPathView, LocalizationSheet& outSheet )
    {
        const string projectPath( projectPathView ); // @p outSheet 의 경로를 넘겨받았을 수 있으므로 비우기 전에 복사한다
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
        {
            outSheet._listTablePath.push_back( LocalizationProject::makeSiblingPath( projectPath, tableName ) );
        }

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
        using Internal = EditorDataTableCommandsInternal;
        // 디스크의 지금 내용 위에 고친 줄만 적용한다. 그래야 수집기가 적은 출처, 번역가 메모, 검토 표시가 유지된다.
        vector<SourceStringTable> listTable;
        if ( Internal::readSourceTables( inoutSheet, listTable ) == false )
        {
            SW_LOG_ERROR( "Localization is not saved - a string table could not be read (fix it and save again)" );
            return false;
        }
        bool                     bAllReadable{ true };
        vector<TranslationTable> listTranslation( inoutSheet._listCulture.size() );
        vector<uint8>            listWritable( inoutSheet._listCulture.size(), 1 );
        for ( size_t cultureIndex = 0; cultureIndex < inoutSheet._listCulture.size(); ++cultureIndex )
        {
            const string path = LocalizationProject::makeTranslationPath( inoutSheet._projectPath, inoutSheet._listCulture[cultureIndex] );
            if ( Internal::readTranslation( path, inoutSheet._listCulture[cultureIndex], listTranslation[cultureIndex] ) == false )
            {
                SW_LOG_ERROR( "Translation table '%#' could not be read - it is not overwritten (fix the file and save again)", path.c_str() );
                listWritable[cultureIndex] = 0;
                bAllReadable               = false;
            }
        }

        Internal::applySheetEdits( inoutSheet, listTable, listTranslation );
        const bool bAllWritten = Internal::writeSheetTables( inoutSheet, listTable, listTranslation, listWritable );
        if ( bAllWritten == false || bAllReadable == false )
            return false; // 저장되지 않은 것이 남아 있으므로 고친 표시를 지우지 않는다

        for ( LocalizationRecord& record : inoutSheet._listRecord )
        {
            record._bModified = false;
        }
        inoutSheet._listRemovedKey.clear();

        // 올린 프로젝트가 아니면 NotTracked — 다시 읽을 것이 없다. 저장은 끝났으니 다시 읽기 실패(관리자가 오류를 남겼다)는 실행 중 글이 예전 것이라는 알림이다.
        const bool bReloadFailed = editor::getSubsystem<LocalizationManager>().reloadChangedFile( inoutSheet._projectPath ) == ReloadOutcome::Failed;
        SW_LOG_INFO( "Saved localization project '%#'.", inoutSheet._projectPath.c_str() );
        if ( bReloadFailed )
            SW_LOG_WARNING( "The running game keeps the previous text of '%#' until it reloads", inoutSheet._projectPath.c_str() );
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
