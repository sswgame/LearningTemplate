#include "pch.h"

#include "GameFramework/Data/GameData.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	SW_LOG_CALLER( "GameData" );

	bool GameData::loadFromResource( string_view assetRelativePath )
	{
		// 게임별 기본 경로를 엔진이 알 필요는 없다. 경로가 없으면 로드할 것도 없다.
		if ( assetRelativePath.empty() )
			return false;

		const string path = string( assetRelativePath );

		XmlDocument doc;
		string		absPath;
		if ( doc.loadResource( path, &absPath ) == false )
		{
			SW_LOG_WARNING( "Using built-in defaults; failed to read %#", path );
			return false;
		}

		XmlNode root = doc.root( "GameData" );
		if ( root.isValid() == false )
		{
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

		_encounterRate = root.childFloat( "encounterRate", _encounterRate );
		_starterLevel  = root.childInt( "starterLevel", _starterLevel );
		_maxPartySize  = root.childInt( "maxPartySize", _maxPartySize );

		SW_LOG_INFO( "Loaded from %# (start=%# starter=%#)", absPath, _startMap, _starterId );
		return true;
	}

	string BootstrapConfig::resolve( string_view packRelative ) const
	{
		const string_view pack = FileUtil::trimTrailingSlashes( _packRoot );
		if ( pack.empty() )
			return FileUtil::normalizePath( packRelative );

		return FileUtil::joinPath( pack, FileUtil::normalizePath( packRelative ) );
	}

	bool BootstrapConfig::load( string_view gamedataFileName )
	{
		const string path = resolve( gamedataFileName );
		Component::setDefaultGamedataPath( path );
		return _data.loadFromResource( path );
	}
} // namespace sw
