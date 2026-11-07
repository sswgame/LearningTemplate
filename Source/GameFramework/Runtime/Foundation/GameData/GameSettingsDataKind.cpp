#include "pch.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Input/Map/InputMapBundle.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

namespace sw
{
    namespace
    {
        [[nodiscard]] bool loadGameSettings( const AssetID& id, GameSettings& outSettings )
        {
            // 입력 맵 묶음도 이 파일의 내용이다 — 카탈로그 편집기의 검사가 겹침을 함께 잡는다.
            return outSettings.loadFromAsset( id ) && InputMapBundle::checkSettings( id );
        }
    } // namespace

    /** @brief `GameSettings` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_ASSET_TYPE( GameSettings, AssetTypeDef::makeDataFile( "gamesettings", AssetPathRule{ "game/", {}, "/data/gamesettings.xml" }, AssetTypeLoader::with<GameSettings, &loadGameSettings>() ) );
} // namespace sw
