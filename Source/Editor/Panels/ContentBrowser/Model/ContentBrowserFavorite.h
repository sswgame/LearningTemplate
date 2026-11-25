/**
 * @file ContentBrowserFavorite.h
 * @brief 콘텐츠 브라우저의 즐겨찾기 폴더 표(`editor/panels/contentbrowser.favorites.xml`)입니다. 보이는 이름은 영어 원문이고 지금 언어로 풀어 보입니다(패널 이름 표와 같은 길).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /** @brief 즐겨찾기 폴더가 있는 팩입니다. */
    ENUM()
    enum class ContentBrowserFavoritePack : uint8
    {
        Game = 0, ///< 돌리는 게임의 팩
        Engine,   ///< 엔진 팩
    };

    /** @brief 즐겨찾기 폴더 하나입니다. */
    REFLECT()
    struct ContentBrowserFavorite
    {
        REFLECT_BODY();

        PROPERTY( Required, Meta = "Localizable", Tooltip = "Name in the list (English source - the text is its localization key)" )
        string _label{};
        PROPERTY( Required, Meta = "NotLocalizable", Tooltip = "Folder inside the pack" )
        string _folder{};
        PROPERTY( Tooltip = "Pack the folder is in" )
        ContentBrowserFavoritePack _pack{ ContentBrowserFavoritePack::Game };

        /** @brief 폴더의 디스크 자리입니다. 그 팩에 폴더가 없으면 빈 경로다. */
        AbsolutePath findFolder() const;
        /** @brief 표를 보이는 순서로 돌려줍니다. `_label` 은 지금 언어로 푼 글이고 언어가 바뀌면 다시 푼다. 표를 읽지 못하면 오류를 남기고 빈 목록이다. */
        static const vector<ContentBrowserFavorite>& getFavorites();
    };

    /** @brief 즐겨찾기 폴더 표 한 벌입니다. */
    REFLECT()
    struct ContentBrowserFavoriteTable
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Favorite folders in list order" )
        vector<ContentBrowserFavorite> _listFavorite{};
    };
} // namespace sw::editor
