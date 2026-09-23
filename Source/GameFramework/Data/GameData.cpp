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

    // 셋 다 `StringUtil` 의 파서를 쓴다. 예전에는 여기서 손수 풀었는데 그 사본들이 공통으로
    // 두 가지를 잃고 있었다.
    //   (1) **못 읽은 것과 0 을 구별 못 했다.** `strtol`/`strtof` 는 실패를 0 으로 반환하므로
    //       `maxPartySize=six` 같은 오타가 조용히 0 이 됐다. fallback 이 있는데도 안 쓰였다.
    //   (2) **대소문자를 반만 봤다.** bool 은 `true`/`True`/`1` 만 알아서 `TRUE` · `yes` · `on`
    //       은 모두 fallback 으로 떨어졌다. `StringUtil::parseBool` 은 처음부터 그것들을 안다.
    // 덤으로 손수 푸는 쪽은 매번 `string` 을 하나씩 만들었다(파서는 `string_view` 로 받는다).
    int32 GameData::getCustomPropertyInt( string_view key, int32 fallback ) const
    {
        int32 value{ 0 };
        return StringUtil::parseInt( getCustomProperty( key ), value ) ? value : fallback;
    }

    float32 GameData::getCustomPropertyFloat( string_view key, float32 fallback ) const
    {
        float32 value{ 0.0f };
        return StringUtil::parseFloat( getCustomProperty( key ), value ) ? value : fallback;
    }

    bool GameData::getCustomPropertyBool( string_view key, bool bFallback ) const
    {
        return StringUtil::parseBool( getCustomProperty( key ), bFallback );
    }

    bool GameData::loadFromResource( string_view assetRelativePath )
    {
        // 게임별 기본 경로를 엔진이 알 필요는 없다. 경로가 없으면 로드할 것도 없다.
        if ( assetRelativePath.empty() )
            return false;

        const string path = string( assetRelativePath );
        if ( ResourceUtil::hasResource( path ) == false )
            return false;

        // **먼저 비운다.** 형제인 `SpeciesCatalog::loadFromResource` 는 처음부터 그렇게 한다.
        // 여기만 안 비워서, 팩을 바꿔 다시 읽으면 앞 팩의 커스텀 프로퍼티가 그대로 남았다.
        // 새 팩에 없는 키를 물으면 **없어진 팩의 값**이 나온다.
        *this = GameData{};

        XmlDocument doc;
        string      absPath;
        if ( doc.loadResource( path, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to parse gamedata from %# — using built-in defaults.", path );
            return false;
        }

        XmlNode root = doc.getRoot( "GameData" );
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
        for ( XmlNode child = root.findChild(); child.isValid() == true; child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::isNullOrEmpty( pName ) )
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
                continue;

            if ( StringUtil::equals( pName, "custom" ) )
            {
                for ( XmlNode prop = child.findChild( "prop" ); prop.isValid() == true; prop = prop.findNextSibling( "prop" ) )
                {
                    const utf8* pKey = prop.findAttribute( "key" );
                    const utf8* pVal = prop.getText();
                    if ( StringUtil::isNullOrEmpty( pKey ) == false && pVal != nullptr )
                        _mapCustomProperty[pKey] = pVal;
                }
                continue;
            }

            const utf8* pText = child.getText();
            if ( pText != nullptr )
                _mapCustomProperty[pName] = pText;
        }

        SW_LOG_INFO( "Loaded from %# (start=%#)", absPath, _startMap );
        return true;
    }

    string BootstrapConfig::resolve( string_view packRelative ) const
    {
        const string pack = FileUtil::trimTrailingSlashes( _packRoot );
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
