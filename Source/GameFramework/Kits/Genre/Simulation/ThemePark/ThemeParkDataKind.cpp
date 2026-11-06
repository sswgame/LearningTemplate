#include "pch.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"
#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 공원 배치는 같은 팩의 코스터 레이아웃을 가리킵니다 — 그것을 먼저 읽습니다.
         * @details 코스터 파일 이름을 여기 다시 적지 않는다. 배치 종류의 접미를 떼어 팩 루트를 얻고, 코스터 종류의 등록 줄로 경로를 만든다(기준 하나).
         */
        [[nodiscard]] bool loadParkLayout( const AssetID& id, ParkLayout& outLayout )
        {
            const string&       assetID     = id.str();
            const AssetTypeDef* pLayoutKind = AssetTypeRegistry::findType( ParkLayout::kDataKind );
            if ( pLayoutKind == nullptr || pLayoutKind->_pathRule._suffix.empty() )
                return false;
            const string_view suffix = pLayoutKind->_pathRule._suffix;
            if ( assetID.size() < suffix.size() )
                return false;
            const string_view packRoot = string_view( assetID ).substr( 0, assetID.size() - suffix.size() );
            string            coasterPath;
            string            error;
            if ( GamePackDataUtil::tryMakeKindPath( packRoot, CoasterLayoutCatalog::kDataKind, coasterPath, error ) == false )
            {
                SW_LOG_ERROR_ID( "ThemeParkDataKind.CoasterLayoutsNoPath", "%#: the coaster layouts have no path - %#", assetID.c_str(), error.c_str() );
                return false;
            }
            CoasterLayoutCatalog layouts;
            if ( layouts.loadFromAsset( AssetID{ coasterPath } ) == false )
                return false;
            return outLayout.loadFromAsset( id, layouts );
        }
    } // namespace

    /** @brief 코스터 레이아웃(`CoasterLayoutCatalog`)의 종류입니다. */
    SW_ASSET_TYPE( Coasters, AssetTypeDef::makeDataFile( CoasterLayoutCatalog::kDataKind, AssetPathRule{ "game/", {}, "/data/coasters.xml" }, AssetTypeLoader::of<CoasterLayoutCatalog>() ) );

    /** @brief 공원 배치(`ParkLayout`, 놀이기구 목록)의 종류입니다. */
    SW_ASSET_TYPE( ParkLayout, AssetTypeDef::makeDataFile( ParkLayout::kDataKind, AssetPathRule{ "game/", {}, "/data/rides.xml" }, AssetTypeLoader::with<ParkLayout, &loadParkLayout>() ) );
} // namespace sw
