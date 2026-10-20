#include "pch.h"

#include "GameFramework/Base/Foundation/Data/GameSettings.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "GameSettings" );

    bool GameSettings::loadFromResource( string_view assetRelativePath )
    {
        // 게임별 기본 경로를 엔진이 알 필요는 없다. 경로가 없으면 로드할 것도 없다.
        if ( assetRelativePath.empty() )
            return false;

        const string path = string( assetRelativePath );
        if ( ResourceIO::hasResource( path ) == false )
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
        root.takeChildText( "audioEvents", _audioEvents );

        bool bUnknownElement = false;
        for ( XMLNode child = root.findChild(); child.isValid() == true; child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::isNullOrEmpty( pName ) )
                continue;

            const bool bReservedSetting = StringUtil::equals( pName, "startMap" ) ||
                                          StringUtil::equals( pName, "titleScene" ) ||
                                          StringUtil::equals( pName, "entranceScene" ) ||
                                          StringUtil::equals( pName, "defaultSavePath" ) ||
                                          StringUtil::equals( pName, "localizationProject" ) ||
                                          StringUtil::equals( pName, "defaultLanguage" ) ||
                                          StringUtil::equals( pName, "fallbackLanguage" ) ||
                                          StringUtil::equals( pName, "inputMap" ) ||
                                          StringUtil::equals( pName, "loadingScreen" ) ||
                                          StringUtil::equals( pName, "audioEvents" );
            if ( bReservedSetting )
                continue;

            // 컴포넌트 기본값 — `ComponentDefaults` 가 같은 파일을 읽는다.
            if ( StringUtil::equals( pName, "Defaults" ) )
                continue;
            // 엔진 칸(창 제목 · 스키마 · UI 규칙) — 엔진의 `PackEngineSettings` 가 같은 파일에서 읽는다. 이 구조체는 읽지 않는다.
            if ( StringUtil::equals( pName, PackEngineSettings::kElementName ) )
                continue;
            SW_LOG_ERROR( "GameSettings %#: unknown element <%#> - game data goes in the game's own catalogs (there is no free-form key-value section)", sourceName, pName );
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
