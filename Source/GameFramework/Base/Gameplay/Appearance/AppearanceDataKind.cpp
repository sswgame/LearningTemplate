#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/ResourceDataKind.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 외형 데이터는 폴더 한 벌로 읽고 서로 대조합니다 — 아이템은 같은 게임의 `data/items.xml` 입니다. 파일마다 폴더 전체를 읽습니다(작다). */
        [[nodiscard]] bool loadAppearanceData( const ResourceID& id )
        {
            const string& resourceID = id.str();
            const string  folder     = FileUtil::getDirectoryPart( resourceID );
            const string  itemsPath  = FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( folder ) ), "items.xml" );
            ItemCatalog   items;
            if ( items.loadFromResource( ResourceID{ itemsPath } ) == false )
                return false;
            AppearanceDatabase database;
            return database.loadFromFolder( folder, &items );
        }
    } // namespace

    /** @brief 외형 데이터(`data/appearance/` 아래 XML)의 종류입니다. */
    SW_RESOURCE_DATA_KIND( Appearance, AppearanceDatabase::kDataKind, "game/", "/data/appearance/", ".xml", &loadAppearanceData );
} // namespace sw
