#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/ResourceDataKind.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"

namespace sw
{
    namespace
    {
        /** @brief 공원 배치는 같은 폴더의 코스터 레이아웃(`coasters.xml`)을 가리킵니다 — 그것을 먼저 읽습니다. */
        [[nodiscard]] bool loadParkLayout( const string& resourceID )
        {
            const string         folder = FileUtil::getDirectoryPart( resourceID );
            CoasterLayoutCatalog layouts;
            if ( layouts.loadFromResource( FileUtil::joinPath( folder, "coasters.xml" ) ) == false )
                return false;
            ParkLayout layout;
            return layout.loadFromResource( resourceID, layouts );
        }
    } // namespace

    /** @brief 코스터 레이아웃(`CoasterLayoutCatalog`)의 종류입니다. */
    SW_RESOURCE_DATA_KIND( Coasters, "coasters", "game/", nullptr, "/data/coasters.xml", &ResourceDataKindUtil::loadCatalog<CoasterLayoutCatalog> );

    /** @brief 공원 배치(`ParkLayout`, 놀이기구 목록)의 종류입니다. */
    SW_RESOURCE_DATA_KIND( ParkLayout, "parklayout", "game/", nullptr, "/data/rides.xml", &loadParkLayout );
} // namespace sw
