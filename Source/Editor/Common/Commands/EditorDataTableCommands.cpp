#include "pch.h"

#include "Editor/Common/Commands/EditorDataTableCommands.h"

#include "Core/Container/map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw::editor
{
    namespace
    {
        struct EditorDataTableCommandsInternal
        {
            enum class LocalizationLanguage : uint8
            {
                EnUS = 0,
                KoKR,
                JaJP
            };

            static const utf8* languageFileStem( LocalizationLanguage lang )
            {
                if ( lang == LocalizationLanguage::EnUS )
                    return "en_US";
                if ( lang == LocalizationLanguage::KoKR )
                    return "ko_KR";
                return "ja_JP";
            }

            static void setLocalizationField( LocalizationRecord& record, LocalizationLanguage lang, string_view value )
            {
                if ( lang == LocalizationLanguage::EnUS )
                    record._enUS = string{ value };
                else if ( lang == LocalizationLanguage::KoKR )
                    record._koKR = string{ value };
                else
                    record._jaJP = string{ value };
            }

            static const string& getLocalizationField( const LocalizationRecord& record, LocalizationLanguage lang )
            {
                if ( lang == LocalizationLanguage::EnUS )
                    return record._enUS;
                if ( lang == LocalizationLanguage::KoKR )
                    return record._koKR;
                return record._jaJP;
            }

            /** @brief 언어 파일의 경로입니다. 활성 게임에 `data/localization` 도메인이 없으면 비어 있습니다. */
            static string languageFilePath( LocalizationLanguage lang, const string& localizationFolder )
            {
                return FileUtil::joinPath( localizationFolder, string{ languageFileStem( lang ) } + ".json" );
            }

            /** @brief 있는 언어 파일이 읽히는가 — 없으면 true(새로 만든다), 있는데 JSON 객체로 읽히지 않으면 false 입니다. */
            static bool isLanguageFileReadable( const string& path, JsonDocument& outDoc )
            {
                if ( path.empty() || FileUtil::fileExists( path ) == false )
                    return true;
                return outDoc.loadFile( path ) && outDoc.getRoot().isObject();
            }

            /**
             * @brief 언어 파일 하나를 표에 합칩니다. 파일이 있는데 읽지 못하면 false 입니다(없으면 true — 아직 번역이 없다).
             * @details 활성 게임에 `data/localization` 도메인이 없으면 localizationFolder 가 비고, joinPath 는 빈 경로를 반환한다. 그대로 넘기면
             *          파일 계층이 "File not found: " 로 **이름 없는** 에러를 언어 수만큼 남긴다. 없는 것은 파일이 아니라 폴더다.
             */
            static bool mergeLanguageJson( LocalizationLanguage lang, const string& localizationFolder, map<string, LocalizationRecord>& mapRecord )
            {
                const string path = languageFilePath( lang, localizationFolder );
                JsonDocument doc;
                if ( isLanguageFileReadable( path, doc ) == false )
                {
                    SW_LOG_WARNING( "Localization file '%#' cannot be read - its strings are not shown and saving will not overwrite it", path );
                    return false;
                }
                if ( doc.getRoot().isObject() == false )
                    return true;

                const vector<string> listKey = doc.getRoot().getMemberNames();
                for ( const string& key : listKey )
                {
                    LocalizationRecord& record = mapRecord[key];
                    record._key                = key;
                    setLocalizationField( record, lang, doc.getRoot().get( key ).asString() );
                }
                return true;
            }

            /**
             * @brief 언어 하나를 파일로 씁니다. **읽지 못한 기존 파일은 덮지 않습니다** — false 입니다.
             * @details 예전에는 깨진 파일(끝의 쉼표 · 병합 표식)을 읽을 때 조용히 건너뛰어 그 언어의 칸이 모두 비었고, 저장이 빈 칸으로 그 파일을 다시
             *          써서 **그 언어의 번역이 모두 지워졌다.** 로그는 "모두 저장했다" 였다.
             */
            [[nodiscard]] static bool writeLanguageJson( LocalizationLanguage lang, const string& localizationFolder, const vector<LocalizationRecord>& listRecord )
            {
                const string path = languageFilePath( lang, localizationFolder );
                if ( path.empty() )
                    return false;
                JsonDocument existing;
                if ( isLanguageFileReadable( path, existing ) == false )
                {
                    SW_LOG_ERROR( "Localization file '%#' cannot be read - it is not overwritten (fix the file and save again)", path );
                    return false;
                }

                JsonDocument    doc;
                const JsonValue root = doc.makeObject();

                for ( const LocalizationRecord& record : listRecord )
                {
                    const string& val = getLocalizationField( record, lang );
                    if ( val.empty() == false )
                        root.set( record._key ).setString( val );
                }

                if ( doc.saveFile( path, 4 ) == false )
                {
                    SW_LOG_ERROR( "Localization file '%#' could not be written", path );
                    return false;
                }

                LocalizationManager* pLocalizationManager = editor::getService<LocalizationManager>();
                if ( pLocalizationManager != nullptr && pLocalizationManager->loadLanguageJson( languageFileStem( lang ), doc.dump( 4 ) ) == false )
                    SW_LOG_WARNING( "Saved '%#' but the running game could not reload it", path );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorDataTableCommands" );

    string EditorDataTableCommands::getLocalizationFolderPath()
    {
        return ResourceUtil::getDomainFolderPath(
            GameConfig::getActive()._packRoot, FileUtil::joinPath( path::kDataFolder, path::kLocalizationFolder ) );
    }

    string EditorDataTableCommands::getGameDataFolderPath()
    {
        return ResourceUtil::getDomainFolderPath( GameConfig::getActive()._packRoot, path::kDataFolder );
    }

    bool EditorDataTableCommands::loadLocalization( vector<LocalizationRecord>& outList )
    {
        return loadLocalizationFrom( getLocalizationFolderPath(), outList );
    }

    bool EditorDataTableCommands::saveLocalization( vector<LocalizationRecord>& listRecord )
    {
        return saveLocalizationTo( getLocalizationFolderPath(), listRecord );
    }

    bool EditorDataTableCommands::loadLocalizationFrom( string_view folder, vector<LocalizationRecord>& outList )
    {
        outList.clear();
        const string localizationFolder{ folder };

        map<string, LocalizationRecord> mapRecord;
        bool                            bAllRead = true;
        bAllRead &= EditorDataTableCommandsInternal::mergeLanguageJson( EditorDataTableCommandsInternal::LocalizationLanguage::EnUS, localizationFolder, mapRecord );
        bAllRead &= EditorDataTableCommandsInternal::mergeLanguageJson( EditorDataTableCommandsInternal::LocalizationLanguage::KoKR, localizationFolder, mapRecord );
        bAllRead &= EditorDataTableCommandsInternal::mergeLanguageJson( EditorDataTableCommandsInternal::LocalizationLanguage::JaJP, localizationFolder, mapRecord );

        outList.reserve( mapRecord.size() );
        for ( auto& pair : mapRecord )
            outList.push_back( std::move( pair.second ) );
        return bAllRead;
    }

    bool EditorDataTableCommands::saveLocalizationTo( string_view folder, vector<LocalizationRecord>& listRecord )
    {
        const string localizationFolder{ folder };
        FileUtil::ensureDirectoryExists( localizationFolder );

        bool bAllWritten = true;
        bAllWritten &= EditorDataTableCommandsInternal::writeLanguageJson( EditorDataTableCommandsInternal::LocalizationLanguage::EnUS, localizationFolder, listRecord );
        bAllWritten &= EditorDataTableCommandsInternal::writeLanguageJson( EditorDataTableCommandsInternal::LocalizationLanguage::KoKR, localizationFolder, listRecord );
        bAllWritten &= EditorDataTableCommandsInternal::writeLanguageJson( EditorDataTableCommandsInternal::LocalizationLanguage::JaJP, localizationFolder, listRecord );
        if ( bAllWritten == false )
            return false; // 고친 표시를 지우지 않는다 — 저장되지 않은 것이 남아 있다

        for ( LocalizationRecord& record : listRecord )
            record._bModified = false;

        SW_LOG_INFO( "Successfully saved all localization tables." );
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
