#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 외형 데이터는 폴더 한 벌로 읽고 서로 대조합니다 — 아이템은 같은 게임의 `data/items.xml` 입니다. 파일마다 폴더 전체를 읽습니다(작다). */
        [[nodiscard]] bool loadAppearanceData( const AssetID& id, AppearanceDatabase& outDatabase )
        {
            const string& assetID   = id.str();
            const string  folder    = FileUtil::getDirectoryPart( assetID );
            const string  itemsPath = FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( folder ) ), "items.xml" );
            ItemCatalog   items;
            if ( items.loadFromAsset( AssetID{ itemsPath } ) == false )
                return false;
            return outDatabase.loadFromFolder( folder, &items );
        }
    } // namespace

    /** @brief 외형 데이터(`data/appearance/` 아래 XML)의 종류입니다. */
    SW_ASSET_TYPE( Appearance, AssetTypeDef::makeDataFile( AppearanceDatabase::kDataKind, AssetPathRule{ {}, "/data/appearance/", ".xml" }, AssetTypeLoader::with<AppearanceDatabase, &loadAppearanceData>() ) );
} // namespace sw
