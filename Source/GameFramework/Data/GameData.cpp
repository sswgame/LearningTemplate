#include "pch.h"

#include "GameFramework/Data/GameData.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	SW_LOG_CALLER( "GameData" );

	namespace
	{
		GameData s_loaded;
	} // namespace

	bool GameData::loadFromResource( string_view assetRelativePath )
	{
		const string path = assetRelativePath.empty()
							  ? string( "game/demo/data/gamedata.xml" )
							  : string( assetRelativePath );

		XmlDocument doc;
		string		absPath;
		if ( doc.loadResource( path, &absPath ) == false )
		{
			s_loaded = *this;
			SW_LOG_WARNING( "Using built-in defaults; failed to read %#", path );
			return false;
		}

		XmlNode root = doc.root( "GameData" );
		if ( root.isValid() == false )
		{
			s_loaded = *this;
			SW_LOG_WARNING( "Missing <GameData> in %# — using defaults.", absPath );
			return false;
		}

		root.takeChildText( "startMap", _startMap );
		root.takeChildText( "titleScene", _titleScene );
		root.takeChildText( "entranceScene", _entranceScene );
		root.takeChildText( "battleMap", _battleMap );
		root.takeChildText( "battleScene", _battleScene );
		root.takeChildText( "defaultSavePath", _defaultSavePath );
		root.takeChildText( "speciesData", _speciesData );
		root.takeChildText( "stringsData", _stringsData );
		root.takeChildText( "localizationDirectory", _localizationDirectory );
		root.takeChildText( "defaultLanguage", _defaultLanguage );
		root.takeChildText( "fallbackLanguage", _fallbackLanguage );
		root.takeChildText( "monstersData", _monstersData );
		root.takeChildText( "starterId", _starterId );
		root.takeChildText( "defaultEncounterId", _defaultEncounterId );
		root.takeChildText( "dungeonBgm", _dungeonBgm );
		root.takeChildText( "bossBgm", _bossBgm );
		root.takeChildText( "bossDefeatSfx", _bossDefeatSfx );
		root.takeChildText( "attackSfx", _attackSfx );
		root.takeChildText( "inputMap", _inputMap );
		root.takeChildText( "renderPipeline", _renderPipeline );
		root.takeChildText( "defaultMaterial", _defaultMaterial );
		root.takeChildText( "glassMaterialInstance", _glassMaterialInstance );

		const utf8* pEncounterRateText = root.childText( "encounterRate" );
		if ( pEncounterRateText != nullptr )
			_encounterRate = static_cast<float32>( StringUtil::atof( pEncounterRateText ) );
		const utf8* pStarterLevelText = root.childText( "starterLevel" );
		if ( pStarterLevelText != nullptr )
			_starterLevel = StringUtil::atoi( pStarterLevelText );
		const utf8* pMaxPartySizeText = root.childText( "maxPartySize" );
		if ( pMaxPartySizeText != nullptr )
			_maxPartySize = StringUtil::atoi( pMaxPartySizeText );

		s_loaded = *this;

		SW_LOG_INFO( "Loaded from %# (start=%# starter=%#)", absPath, _startMap, _starterId );
		return true;
	}

	const GameData& GameData::get()
	{
		return s_loaded;
	}

	string BootstrapConfig::resolve( string_view packRelative ) const
	{
		const string pack = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( _packRoot ) );
		const string rel  = FileUtil::normalizePath( packRelative );
		if ( pack.empty() )
			return rel;
		return FileUtil::joinPath( pack, rel );
	}

	bool BootstrapConfig::load( string_view gamedataFileName )
	{
		const string path = resolve( gamedataFileName );
		Component::setDefaultGamedataPath( path );
		return _data.loadFromResource( path );
	}
} // namespace sw
