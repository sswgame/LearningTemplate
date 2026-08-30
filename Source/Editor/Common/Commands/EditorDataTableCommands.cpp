#include "pch.h"

#include "Editor/Common/Commands/EditorDataTableCommands.h"

#include "Core/Container/map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Resource/ResourceUtil.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	namespace
	{
		struct EditorDataTableCommandsInternal
		{
			enum class LocLang : uint8
			{
				EnUS = 0,
				KoKR,
				JaJP
			};

			static const utf8* locLangFileStem( LocLang lang )
			{
				if ( lang == LocLang::EnUS )
					return "en_US";
				if ( lang == LocLang::KoKR )
					return "ko_KR";
				return "ja_JP";
			}

			static void setLocField( LocRecord& rec, LocLang lang, string_view value )
			{
				if ( lang == LocLang::EnUS )
					rec._enUS = string{ value };
				else if ( lang == LocLang::KoKR )
					rec._koKR = string{ value };
				else
					rec._jaJP = string{ value };
			}

			static const string& getLocField( const LocRecord& rec, LocLang lang )
			{
				if ( lang == LocLang::EnUS )
					return rec._enUS;
				if ( lang == LocLang::KoKR )
					return rec._koKR;
				return rec._jaJP;
			}

			static void mergeLangJson( LocLang lang, const string& locFolder, map<string, LocRecord>& mapRecord )
			{
				const string path = FileUtil::joinPath( locFolder, string{ locLangFileStem( lang ) } + ".json" );
				JsonDocument doc;
				if ( doc.loadFile( path ) == false || doc.root().isObject() == false )
					return;

				const vector<string> listKey = doc.root().memberNames();
				for ( const string& key : listKey )
				{
					LocRecord& rec = mapRecord[key];
					rec._key	   = key;
					setLocField( rec, lang, doc.root().get( key ).asString() );
				}
			}

			static void writeLangJson( LocLang lang, const string& locFolder, const vector<LocRecord>& listRecord )
			{
				JsonDocument	doc;
				const JsonValue root = doc.makeObject();

				for ( const LocRecord& rec : listRecord )
				{
					const string& val = getLocField( rec, lang );
					if ( val.empty() == false )
						root.set( rec._key ).setString( val );
				}

				const string path = FileUtil::joinPath( locFolder, string{ locLangFileStem( lang ) } + ".json" );
				if ( doc.saveFile( path, 4 ) == false )
					return;

				LocalizationManager* pLocMgr = editor::getService<LocalizationManager>();
				if ( pLocMgr != nullptr )
					pLocMgr->loadLanguageJson( locLangFileStem( lang ), doc.dump( 4 ) );
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	SW_LOG_CALLER( "EditorDataTableCommands" );

	string EditorDataTableCommands::getLocalizationFolderPath()
	{
		return ResourceUtil::joinActivePackPath( FileUtil::joinPath( path::kDataFolder, path::kLocalizationFolder ) );
	}

	string EditorDataTableCommands::getGameDataFolderPath()
	{
		return ResourceUtil::joinActivePackPath( path::kDataFolder );
	}

	bool EditorDataTableCommands::loadLocalization( vector<LocRecord>& outList )
	{
		outList.clear();
		const string locFolder = getLocalizationFolderPath();

		map<string, LocRecord> mapRecord;
		EditorDataTableCommandsInternal::mergeLangJson( EditorDataTableCommandsInternal::LocLang::EnUS, locFolder, mapRecord );
		EditorDataTableCommandsInternal::mergeLangJson( EditorDataTableCommandsInternal::LocLang::KoKR, locFolder, mapRecord );
		EditorDataTableCommandsInternal::mergeLangJson( EditorDataTableCommandsInternal::LocLang::JaJP, locFolder, mapRecord );

		outList.reserve( mapRecord.size() );
		for ( auto& pair : mapRecord )
			outList.push_back( std::move( pair.second ) );
		return true;
	}

	bool EditorDataTableCommands::saveLocalization( vector<LocRecord>& listRecord )
	{
		const string locFolder = getLocalizationFolderPath();
		FileUtil::ensureDirectoryExists( locFolder );

		EditorDataTableCommandsInternal::writeLangJson( EditorDataTableCommandsInternal::LocLang::EnUS, locFolder, listRecord );
		EditorDataTableCommandsInternal::writeLangJson( EditorDataTableCommandsInternal::LocLang::KoKR, locFolder, listRecord );
		EditorDataTableCommandsInternal::writeLangJson( EditorDataTableCommandsInternal::LocLang::JaJP, locFolder, listRecord );

		for ( LocRecord& rec : listRecord )
			rec._bModified = false;

		SW_LOG_INFO( "Successfully saved all localization tables." );
		return true;
	}

	bool EditorDataTableCommands::hasModifiedLocalization( const vector<LocRecord>& listRecord )
	{
		for ( const LocRecord& rec : listRecord )
		{
			if ( rec._bModified )
				return true;
		}
		return false;
	}

	bool EditorDataTableCommands::collectGameDataFiles( vector<GameDataFileEntry>& outList )
	{
		outList.clear();
		const string dataFolder = getGameDataFolderPath();

		vector<string> listFile;
		FileUtil::collectFiles( dataFolder, ".xml", listFile, false, false );

		for ( const string& file : listFile )
		{
			if ( FileUtil::hasExtension( file, ".xml" ) == false )
				continue;

			GameDataFileEntry entry{};
			entry._fileName			  = FileUtil::getFileNamePart( file );
			entry._absolutePath		  = FileUtil::normalizeSeparators( file );
			const string& projectRoot = ResourceUtil::getProjectFolderPath();
			FileUtil::makePathRelative( projectRoot.empty() ? FileUtil::getCurrentPath() : projectRoot, file, entry._relativePath );
			outList.push_back( std::move( entry ) );
		}
		return true;
	}
} // namespace sw::editor
