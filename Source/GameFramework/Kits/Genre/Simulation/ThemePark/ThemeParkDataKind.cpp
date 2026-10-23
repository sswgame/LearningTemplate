#include "pch.h"

#include "Engine/Resource/ResourceDataKind.h"

#include "GameFramework/Base/Foundation/Data/GamePackDataUtil.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"

namespace sw
{
    SW_LOG_CALLER( "ThemeParkDataKind" );

    namespace
    {
        /**
         * @brief 공원 배치는 같은 팩의 코스터 레이아웃을 가리킵니다 — 그것을 먼저 읽습니다.
         * @details 코스터 파일 이름을 여기 다시 적지 않는다. 배치 종류의 접미를 떼어 팩 루트를 얻고, 코스터 종류의 등록 줄로 경로를 만든다(기준 하나).
         */
        [[nodiscard]] bool loadParkLayout( const string& resourceID )
        {
            const ResourceDataKind* pLayoutKind = ResourceDataKindRegistry::get().findByLabel( ParkLayout::kDataKind );
            if ( pLayoutKind == nullptr || pLayoutKind->_pSuffix == nullptr )
                return false;
            const string_view suffix{ pLayoutKind->_pSuffix };
            if ( resourceID.size() < suffix.size() )
                return false;
            const string_view packRoot = string_view( resourceID ).substr( 0, resourceID.size() - suffix.size() );
            string            coasterPath;
            string            error;
            if ( GamePackDataUtil::tryMakeKindPath( packRoot, CoasterLayoutCatalog::kDataKind, coasterPath, error ) == false )
            {
                SW_LOG_ERROR( "%#: the coaster layouts have no path - %#", resourceID.c_str(), error.c_str() );
                return false;
            }
            CoasterLayoutCatalog layouts;
            if ( layouts.loadFromResource( coasterPath ) == false )
                return false;
            ParkLayout layout;
            return layout.loadFromResource( resourceID, layouts );
        }
    } // namespace

    /** @brief 코스터 레이아웃(`CoasterLayoutCatalog`)의 종류입니다. */
    SW_RESOURCE_DATA_KIND( Coasters, CoasterLayoutCatalog::kDataKind, "game/", nullptr, "/data/coasters.xml", &ResourceDataKindUtil::loadCatalog<CoasterLayoutCatalog> );

    /** @brief 공원 배치(`ParkLayout`, 놀이기구 목록)의 종류입니다. */
    SW_RESOURCE_DATA_KIND( ParkLayout, ParkLayout::kDataKind, "game/", nullptr, "/data/rides.xml", &loadParkLayout );
} // namespace sw
