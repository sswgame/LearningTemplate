/**
 * @file ContentBrowserLogic.h
 * @brief 콘텐츠 브라우저 패널(`ContentBrowserPanel`)의 판단입니다 — 경로 줄 조각. ImGui 를 모릅니다(EditorTest 가 시험합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw::editor
{
    /**
     * @struct ContentBrowserCrumb
     * @brief 경로 줄의 조각 하나입니다. 보이는 이름과 누르면 갈 폴더를 함께 듭니다.
     * @details 경로 줄을 글 하나("Favorites / Shaders / bin")로 들고 누를 때 다시 쪼개면, 루트가 아닌 첫 조각("Favorites")에서 절대 경로가
     *          끊겨 다음 조각이 상대 경로가 된다. 조각마다 절대 경로를 처음부터 함께 둔다.
     */
    struct ContentBrowserCrumb
    {
        string _label;        ///< 보이는 이름
        string _absolutePath; ///< 누르면 갈 폴더. 비면 누를 수 없는 묶음 이름입니다("Favorites").
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ContentBrowserLogic
     * @brief 콘텐츠 브라우저가 부르는 순수 함수들입니다(언리얼 Content Browser 의 경로 줄 자리).
     */
    struct ContentBrowserLogic
    {
        /** @brief 즐겨찾기에서 들어간 폴더의 경로 줄입니다: `{ "Favorites", "" }, { @p label, @p absolutePath }`. */
        static void makeFavoriteTrail( string_view label, string_view absolutePath, vector<ContentBrowserCrumb>& outListCrumb );

        /**
         * @brief 루트 아래 폴더의 경로 줄입니다: 루트 조각, 그리고 루트에서 @p folderAbs 까지의 폴더마다 한 조각.
         * @details @p folderAbs 가 루트 아래가 아니면 루트 조각 없이 그 폴더 한 조각입니다.
         */
        static void makeFolderTrail( string_view rootLabel, string_view rootAbs, string_view folderAbs, vector<ContentBrowserCrumb>& outListCrumb );

        /** @brief 지금 경로 줄 끝에 하위 폴더 조각 하나를 더합니다(이름은 폴더 이름). */
        static void appendChildCrumb( string_view childAbs, vector<ContentBrowserCrumb>& inoutListCrumb );
    };
} // namespace sw::editor
