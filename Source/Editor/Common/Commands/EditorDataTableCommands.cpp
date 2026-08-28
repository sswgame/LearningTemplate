#include "pch.h"

#include "Editor/Common/Commands/EditorDataTableCommands.h"

#include "Core/Container/map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Utility/Json/JsonDocument.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	SW_LOG_CALLER( "EditorDataTableCommands" );

	namespace
	{
		enum class LocLang : uint8
		{
			EnUS = 0,
			KoKR,
			JaJP
		};

		const utf8* locLangFileStem( LocLang lang )
		{
			if ( lang == LocLang::EnUS )
				return "en_US";
			if ( lang == LocLang::KoKR )
				return "ko_KR";
			return "ja_JP";
		}

		void setLocField( LocRecord& rec, LocLang lang, string_view value )
		{
			if ( lang == LocLang::EnUS )
				rec._enUS = string{ value };
			else if ( lang == LocLang::KoKR )
				rec._koKR = string{ value };
			else
				rec._jaJP = string{ value };
		}

		const string& getLocField( const LocRecord& rec, LocLang lang )
		{
			if ( lang == LocLang::EnUS )
				return rec._enUS;
			if ( lang == LocLang::KoKR )
				return rec._koKR;
			return rec._jaJP;
		}

		void mergeLangJson( LocLang lang, const string& locFolder, map<string, LocRecord>& mapRecords )
		{
			const string path = FileUtil::joinPath( locFolder, string{ locLangFileStem( lang ) } + ".json" );
			string		 text;
			if ( FileUtil::readTextFile( path, text ) == false )
				return;

			JsonDocument doc;
			if ( doc.parse( text ) == false || doc.root().isObject() == false )
				return;

			const vector<string> listKeys = doc.root().memberNames();
			for ( const string& key : listKeys )
			{
				LocRecord& rec = mapRecords[key];
				rec._key	   = key;
				setLocField( rec, lang, doc.root().get( key ).asString() );
			}
		}

		void writeLangJson( LocLang lang, const string& locFolder, const vector<LocRecord>& listRecord )
		{
			JsonDocument doc;
			doc.root().setObject();

			for ( const LocRecord& rec : listRecord )
			{
				const string& val = getLocField( rec, lang );
				if ( val.empty() == false )
					doc.root().set( rec._key ).setString( val );
			}

			const string path	= FileUtil::joinPath( locFolder, string{ locLangFileStem( lang ) } + ".json" );
			const string dumped = doc.dump( 4 );
			FileUtil::writeTextFile( path, dumped );

			LocalizationManager* pLocMgr = editor::getService<LocalizationManager>();
			if ( pLocMgr != nullptr )
				pLocMgr->loadLanguageJson( locLangFileStem( lang ), dumped );
		}
	} // namespace

	string EditorDataTableCommands::getLocalizationFolderPath()
	{
		return FileUtil::joinPath( FileUtil::getCurrentPath(), "Resource/game/demo/data/localization" );
	}

	string EditorDataTableCommands::getGameDataFolderPath()
	{
		return FileUtil::joinPath( FileUtil::getCurrentPath(), "Resource/game/demo/data" );
	}

	bool EditorDataTableCommands::loadLocalization( vector<LocRecord>& outList )
	{
		outList.clear();
		const string locFolder = getLocalizationFolderPath();

		map<string, LocRecord> mapRecords;
		mergeLangJson( LocLang::EnUS, locFolder, mapRecords );
		mergeLangJson( LocLang::KoKR, locFolder, mapRecords );
		mergeLangJson( LocLang::JaJP, locFolder, mapRecords );

		outList.reserve( mapRecords.size() );
		for ( auto& pair : mapRecords )
			outList.push_back( std::move( pair.second ) );
		return true;
	}

	bool EditorDataTableCommands::saveLocalization( vector<LocRecord>& listRecord )
	{
		const string locFolder = getLocalizationFolderPath();
		FileUtil::ensureDirectoryExists( locFolder );

		writeLangJson( LocLang::EnUS, locFolder, listRecord );
		writeLangJson( LocLang::KoKR, locFolder, listRecord );
		writeLangJson( LocLang::JaJP, locFolder, listRecord );

		for ( LocRecord& rec : listRecord )
			rec._bModified = false;

		SW_LOG_INFO( "Successfully saved all localization tables." );
		return true;
	}

	bool EditorDataTableCommands::collectGameDataFiles( vector<GameDataFileEntry>& outList )
	{
		outList.clear();
		const string dataFolder = getGameDataFolderPath();

		vector<string> listFiles;
		FileUtil::collectFiles( dataFolder, ".xml", listFiles, false, false );

		for ( const string& file : listFiles )
		{
			if ( FileUtil::hasExtension( file, ".xml" ) == false )
				continue;

			GameDataFileEntry entry{};
			entry._fileName		= FileUtil::getFileNamePart( file );
			entry._absolutePath = FileUtil::normalizeSeparators( file );
			FileUtil::makePathRelative( FileUtil::getCurrentPath(), file, entry._relativePath );
			outList.push_back( std::move( entry ) );
		}
		return true;
	}

	bool EditorDataTableCommands::loadGameDataFile( string_view absolutePath, string& outText )
	{
		return FileUtil::readTextFile( absolutePath, outText );
	}

	bool EditorDataTableCommands::saveGameDataFile( string_view absolutePath, string_view text )
	{
		if ( FileUtil::writeTextFile( absolutePath, text ) == false )
			return false;
		SW_LOG_INFO( "Saved game data table %#", FileUtil::getFileNamePart( string{ absolutePath } ).c_str() );
		return true;
	}
} // namespace sw::editor
