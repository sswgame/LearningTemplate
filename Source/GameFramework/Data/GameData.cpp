#include "pch.h"

#include "GameFramework/Data/GameData.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	SW_LOG_CALLER( "GameData" );

	string_view GameData::getCustomProperty( string_view key, string_view fallback ) const
	{
		const auto it = _mapCustomProperty.find( string( key ) );
		if ( it != _mapCustomProperty.end() )
			return it->second;
		return fallback;
	}

	int32 GameData::getCustomPropertyInt( string_view key, int32 fallback ) const
	{
		const string_view val = getCustomProperty( key );
		if ( val.empty() == true )
			return fallback;
		return static_cast<int32>( std::strtol( string( val ).c_str(), nullptr, 10 ) );
	}

	float32 GameData::getCustomPropertyFloat( string_view key, float32 fallback ) const
	{
		const string_view val = getCustomProperty( key );
		if ( val.empty() == true )
			return fallback;
		return std::strtof( string( val ).c_str(), nullptr );
	}

	bool GameData::getCustomPropertyBool( string_view key, bool bFallback ) const
	{
		const string_view val = getCustomProperty( key );
		if ( val.empty() == true )
			return bFallback;
		if ( val == "true" || val == "True" || val == "1" )
			return true;
		if ( val == "false" || val == "False" || val == "0" )
			return false;
		return bFallback;
	}

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
		root.takeChildText( "defaultSavePath", _defaultSavePath );
		root.takeChildText( "stringsData", _stringsData );
		root.takeChildText( "localizationDirectory", _localizationDirectory );
		root.takeChildText( "defaultLanguage", _defaultLanguage );
		root.takeChildText( "fallbackLanguage", _fallbackLanguage );
		root.takeChildText( "inputMap", _inputMap );

		// 표준 필드 이외의 모든 태그는 _mapCustomProperty 에 자동 보관
		for ( XmlNode child = root.child(); child.isValid() == true; child = child.next() )
		{
			const utf8* pName = child.name();
			if ( pName == nullptr || pName[0] == '\0' )
				continue;

			if ( StringUtil::equals( pName, "startMap" ) ||
				 StringUtil::equals( pName, "titleScene" ) ||
				 StringUtil::equals( pName, "entranceScene" ) ||
				 StringUtil::equals( pName, "defaultSavePath" ) ||
				 StringUtil::equals( pName, "stringsData" ) ||
				 StringUtil::equals( pName, "localizationDirectory" ) ||
				 StringUtil::equals( pName, "defaultLanguage" ) ||
				 StringUtil::equals( pName, "fallbackLanguage" ) ||
				 StringUtil::equals( pName, "inputMap" ) )
			{
				continue;
			}

			if ( StringUtil::equals( pName, "custom" ) )
			{
				for ( XmlNode prop = child.child( "prop" ); prop.isValid() == true; prop = prop.next( "prop" ) )
				{
					const utf8* pKey = prop.attr( "key" );
					const utf8* pVal = prop.text();
					if ( pKey != nullptr && pKey[0] != '\0' && pVal != nullptr )
						_mapCustomProperty[pKey] = pVal;
				}
				continue;
			}

			const utf8* pText = child.text();
			if ( pText != nullptr )
				_mapCustomProperty[pName] = pText;
		}

		SW_LOG_INFO( "Loaded from %# (start=%#)", absPath, _startMap );
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
