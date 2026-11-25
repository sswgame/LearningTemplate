// ContentBrowserFavoriteTest — 즐겨찾기 폴더 표: 엄격하게 읽히고, 이름과 폴더가 비지 않으며 이름이 겹치지 않는다(nogpu).
#include "pch.h"

#include "Core/Container/unordered_set.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserFavorite.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

using namespace sw;
using namespace sw::editor;

/** @brief [ContentBrowserFavoriteTest] 배포된 표가 읽히고, 이름마다 폴더가 있으며 엔진 팩의 줄도 있다 */
SW_TEST_CASE( ContentBrowserFavoriteTest, ShippedTableIsReadable )
{
    ContentBrowserFavoriteTable table;
    SW_REQUIRE_TRUE( XMLSerializer::loadFromAsset( AssetID{ ContentBrowserFavoriteTable::kAsset }, &table, *ContentBrowserFavoriteTable::StaticType() ) );
    SW_EXPECT_TRUE( table._listFavorite.empty() == false );
    unordered_set<string> setLabel;
    bool                  bEngineListed{ false };
    for ( const ContentBrowserFavorite& favorite : table._listFavorite )
    {
        SW_EXPECT_FALSE( favorite._folder.empty() );
        SW_EXPECT_TRUE( setLabel.insert( favorite._label ).second );
        bEngineListed = bEngineListed || favorite._pack == ContentBrowserFavoritePack::Engine;
    }
    SW_EXPECT_TRUE( bEngineListed );
}
