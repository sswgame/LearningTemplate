#include "pch.h"

#include "Games/Playground/Online/PlaygroundOnlineSettings.h"

#include "Core/Log/Logger.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"

#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {
        [[nodiscard]] bool loadPlaygroundOnlineSettings( const AssetID& id, PlaygroundOnlineSettings& outSettings )
        {
            return ReflectedXMLFile::loadDesc( id, outSettings );
        }
    } // namespace

    bool PlaygroundOnlineSettings::loadForGame( PlaygroundOnlineSettings& outSettings )
    {
        outSettings = PlaygroundOnlineSettings{};
        string path;
        if ( const expected<void> found = GamePackDataUtil::tryMakeKindPath( game::getSubsystem<const GameConfig>()._packRoot.get(), kDataKind, path ); found.has_value() == false )
        {
            SW_LOG_ERROR_ID( "Playground.OnlineNoSettingsPath", "[Playground] the game has no online settings path: %# (%#)", found.error().message(), found.error().detail() );
            return false;
        }
        const bool bRead = loadPlaygroundOnlineSettings( AssetID{ path }, outSettings );
        if ( bRead == false )
            SW_LOG_ERROR_ID( "Playground.OnlineSettingsUnreadable", "[Playground] the online settings '%#' could not be read", path );
        return bRead;
    }

    /** @brief 온라인 흐름 설정의 데이터 파일 종류입니다(스키마 테스트가 이 줄로 파일을 읽어 본다). */
    SW_ASSET_TYPE( PlaygroundOnlineSettings, AssetTypeDef::makeDataFile( PlaygroundOnlineSettings::kDataKind, AssetPathRule{ "game/", {}, "/data/online.xml" }, AssetTypeLoader::with<PlaygroundOnlineSettings, &loadPlaygroundOnlineSettings>( MemoryTag::Game ) ) );
} // namespace sw
