#include "pch.h"

#include "GameFramework/Base/Foundation/Data/GameSettings.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "GameSettings" );

    string_view GameSettings::getCustomProperty( string_view key, string_view fallback ) const
    {
        const auto it = _mapCustomProperty.find( key ); // 투명 비교자(`std::less<>`) — 조회마다 `string` 을 만들지 않는다
        if ( it != _mapCustomProperty.end() )
            return it->second;
        return fallback;
    }

    // 셋 다 `StringUtil` 의 파서를 쓴다. 손수 풀지 말 것 — 주의할 것 둘:
    //   (1) **못 읽은 것과 0 을 구별해야 한다.** `strtol`/`strtof` 는 실패를 0 으로 반환하므로
    //       `maxPartySize=six` 같은 오타가 조용히 0 이 되고 fallback 이 안 쓰인다.
    //   (2) **bool 은 대소문자를 가리지 않는다.** `StringUtil::parseBool` 은 `TRUE` · `yes` · `on` 도 안다.
    // 파서는 `string_view` 로 받으므로 `string` 을 만들지 않는다.
    // 값이 있는데 못 읽으면 알린다(없거나 빈 값은 조용히 fallback) — `KeyValueFile` 의 형제 조회와 같은 규칙이다.
    int32 GameSettings::getCustomPropertyInt( string_view key, int32 fallback ) const
    {
        const string_view text = getCustomProperty( key );
        int32             value{ 0 };
        if ( text.empty() )
            return fallback;
        if ( StringUtil::parseInt( text, value ) )
            return value;
        SW_LOG_WARNING( "GameSettings '%#' has an unreadable integer '%#' - using %#", key, text, fallback );
        return fallback;
    }

    float32 GameSettings::getCustomPropertyFloat( string_view key, float32 fallback ) const
    {
        const string_view text = getCustomProperty( key );
        float32           value{ 0.0f };
        if ( text.empty() )
            return fallback;
        if ( StringUtil::parseFloat( text, value ) )
            return value;
        SW_LOG_WARNING( "GameSettings '%#' has an unreadable number '%#' - using %#", key, text, fallback );
        return fallback;
    }

    bool GameSettings::getCustomPropertyBool( string_view key, bool bFallback ) const
    {
        const string_view text = getCustomProperty( key );
        bool              value{ bFallback };
        if ( text.empty() )
            return bFallback;
        if ( StringUtil::tryParseBool( text, value ) )
            return value;
        SW_LOG_WARNING( "GameSettings '%#' has an unreadable boolean '%#' - using %#", key, text, bFallback ? "true" : "false" );
        return bFallback;
    }

    bool GameSettings::loadFromResource( string_view assetRelativePath )
    {
        // 게임별 기본 경로를 엔진이 알 필요는 없다. 경로가 없으면 로드할 것도 없다.
        if ( assetRelativePath.empty() )
            return false;

        const string path = string( assetRelativePath );
        if ( ResourceUtil::hasResource( path ) == false )
            return false;

        // **먼저 비운다.**
        // 주의: 안 비우면 팩을 바꿔 다시 읽을 때 앞 팩의 커스텀 프로퍼티가 그대로 남아,
        // 새 팩에 없는 키를 물으면 **없어진 팩의 값**이 나온다.
        *this = GameSettings{};
        return GameDataXML::loadFile( *this, &GameSettings::loadRoot, path, "GameSettings" );
    }

    bool GameSettings::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = GameSettings{};
        return GameDataXML::loadText( *this, &GameSettings::loadRoot, xmlText, sourceName, "GameSettings" );
    }

    bool GameSettings::loadRoot( const XMLNode& root, string_view sourceName )
    {
        root.takeChildText( "startMap", _startMap );
        root.takeChildText( "titleScene", _titleScene );
        root.takeChildText( "entranceScene", _entranceScene );
        root.takeChildText( "defaultSavePath", _defaultSavePath );
        root.takeChildText( "localizationProject", _localizationProject );
        root.takeChildText( "defaultLanguage", _defaultLanguage );
        root.takeChildText( "fallbackLanguage", _fallbackLanguage );
        root.takeChildText( "inputMap", _inputMap );
        root.takeChildText( "loadingScreen", _loadingScreen );

        bool bUnknownElement = false;
        for ( XMLNode child = root.findChild(); child.isValid() == true; child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::isNullOrEmpty( pName ) )
                continue;

            if ( StringUtil::equals( pName, "startMap" ) ||
                 StringUtil::equals( pName, "titleScene" ) ||
                 StringUtil::equals( pName, "entranceScene" ) ||
                 StringUtil::equals( pName, "defaultSavePath" ) ||
                 StringUtil::equals( pName, "localizationProject" ) ||
                 StringUtil::equals( pName, "defaultLanguage" ) ||
                 StringUtil::equals( pName, "fallbackLanguage" ) ||
                 StringUtil::equals( pName, "inputMap" ) ||
                 StringUtil::equals( pName, "loadingScreen" ) )
                continue;

            if ( StringUtil::equals( pName, "custom" ) )
            {
                for ( XMLNode prop = child.findChild( "prop" ); prop.isValid() == true; prop = prop.findNextSibling( "prop" ) )
                {
                    const utf8* pKey = prop.findAttribute( "key" );
                    const utf8* pVal = prop.getText();
                    if ( StringUtil::isNullOrEmpty( pKey ) == false && pVal != nullptr )
                        _mapCustomProperty[pKey] = pVal;
                }
                continue;
            }

            // 컴포넌트 기본값 — `ComponentDefaults` 가 같은 파일을 읽는다.
            if ( StringUtil::equals( pName, "Defaults" ) )
                continue;
            SW_LOG_ERROR( "GameSettings %#: unknown element <%#> - custom values go under <custom><prop key=\"...\">", sourceName, pName );
            bUnknownElement = true;
        }
        if ( bUnknownElement )
            return false;

        SW_LOG_INFO( "Loaded from %# (start=%#)", sourceName, _startMap );
        return true;
    }

    string BootstrapConfig::resolve( string_view packRelative ) const
    {
        const string pack = FileUtil::trimTrailingSlashes( _packRoot );
        if ( pack.empty() )
            return FileUtil::normalizePath( packRelative );

        return FileUtil::joinPath( pack, FileUtil::normalizePath( packRelative ) );
    }

    bool BootstrapConfig::load( string_view gameSettingsFileName )
    {
        const string path = resolve( gameSettingsFileName );
        Component::setDefaultGameSettingsPath( path );
        return _data.loadFromResource( path );
    }
} // namespace sw
