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

            static void mergeLanguageJson( LocalizationLanguage lang, const string& localizationFolder, map<string, LocalizationRecord>& mapRecord )
            {
                // 활성 게임에 `data/localization` 도메인이 없으면 localizationFolder 가 비고, joinPath 는 빈 경로를 반환한다. 그대로 넘기면
                // 파일 계층이 "File not found: " 로 **이름 없는** 에러를 언어 수만큼 남긴다. 없는 것은 파일이 아니라 폴더다.
                const string path = FileUtil::joinPath( localizationFolder, string{ languageFileStem( lang ) } + ".json" );
                if ( path.empty() )
                    return;

                JsonDocument doc;
                if ( doc.loadFile( path ) == false || doc.getRoot().isObject() == false )
                    return;

                const vector<string> listKey = doc.getRoot().getMemberNames();
                for ( const string& key : listKey )
                {
                    LocalizationRecord& record = mapRecord[key];
                    record._key                = key;
                    setLocalizationField( record, lang, doc.getRoot().get( key ).asString() );
                }
            }

            static void writeLanguageJson( LocalizationLanguage lang, const string& localizationFolder, const vector<LocalizationRecord>& listRecord )
            {
                JsonDocument    doc;
                const JsonValue root = doc.makeObject();

                for ( const LocalizationRecord& record : listRecord )
                {
                    const string& val = getLocalizationField( record, lang );
                    if ( val.empty() == false )
                        root.set( record._key ).setString( val );
                }

                const string path = FileUtil::joinPath( localizationFolder, string{ languageFileStem( lang ) } + ".json" );
                if ( path.empty() || doc.saveFile( path, 4 ) == false )
                    return;

                LocalizationManager* pLocalizationManager = editor::getService<LocalizationManager>();
                if ( pLocalizationManager != nullptr )
                    pLocalizationManager->loadLanguageJson( languageFileStem( lang ), doc.dump( 4 ) );
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
        outList.clear();
        const string localizationFolder = getLocalizationFolderPath();

        map<string, LocalizationRecord> mapRecord;
        EditorDataTableCommandsInternal::mergeLanguageJson( EditorDataTableCommandsInternal::LocalizationLanguage::EnUS, localizationFolder, mapRecord );
        EditorDataTableCommandsInternal::mergeLanguageJson( EditorDataTableCommandsInternal::LocalizationLanguage::KoKR, localizationFolder, mapRecord );
        EditorDataTableCommandsInternal::mergeLanguageJson( EditorDataTableCommandsInternal::LocalizationLanguage::JaJP, localizationFolder, mapRecord );

        outList.reserve( mapRecord.size() );
        for ( auto& pair : mapRecord )
            outList.push_back( std::move( pair.second ) );
        return true;
    }

    bool EditorDataTableCommands::saveLocalization( vector<LocalizationRecord>& listRecord )
    {
        const string localizationFolder = getLocalizationFolderPath();
        FileUtil::ensureDirectoryExists( localizationFolder );

        EditorDataTableCommandsInternal::writeLanguageJson( EditorDataTableCommandsInternal::LocalizationLanguage::EnUS, localizationFolder, listRecord );
        EditorDataTableCommandsInternal::writeLanguageJson( EditorDataTableCommandsInternal::LocalizationLanguage::KoKR, localizationFolder, listRecord );
        EditorDataTableCommandsInternal::writeLanguageJson( EditorDataTableCommandsInternal::LocalizationLanguage::JaJP, localizationFolder, listRecord );

        for ( LocalizationRecord& record : listRecord )
            record._bModified = false;

        SW_LOG_INFO( "Successfully saved all localization tables." );
        return true;
    }

    bool EditorDataTableCommands::hasModifiedLocalization( const vector<LocalizationRecord>& listRecord )
    {
        for ( const LocalizationRecord& record : listRecord )
        {
            if ( record._bModified )
                return true;
        }
        return false;
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
            entry._fileName           = FileUtil::getFileNamePart( file );
            entry._absolutePath       = FileUtil::normalizeSeparators( file );
            const string& projectRoot = ResourceUtil::getProjectFolderPath();
            FileUtil::makeRelativePath( projectRoot.empty() ? FileUtil::getCurrentPath() : projectRoot, file, entry._relativePath );
            outList.push_back( std::move( entry ) );
        }
        return true;
    }
} // namespace sw::editor
