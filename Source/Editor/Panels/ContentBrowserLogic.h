/**
 * @file ContentBrowserLogic.h
 * @brief 콘텐츠 브라우저 패널(`ContentBrowserPanel`)의 판단입니다 — 경로 줄 조각 · 폴더 트리 캐시. ImGui 를 모릅니다(EditorTest 가 시험합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

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

namespace sw::editor
{
    /** @brief 폴더의 직속 하위 폴더 절대 경로를 디스크에서 읽어 채우는 함수입니다(`EditorAssetCommands::collectChildFolders`). */
    using ContentBrowserFolderScanFunc = void ( * )( string_view folderAbs, vector<string>& outListChild );

    /**
     * @class ContentBrowserFolderCache
     * @brief 폴더 트리가 그리는 "폴더 → 정렬된 하위 폴더" 를 처음 물을 때 한 번만 디스크에서 읽어 둡니다.
     * @details 트리를 그릴 때마다 보이는 노드마다 하위 폴더를 디스크에서 읽으면 트리가 펼쳐질수록 프레임이 느려진다(Debug 16.8 ms).
     *          디스크가 바뀌면(Refresh · 파일 감시의 변경 번호) 부르는 쪽이 `clear` 한다.
     */
    class ContentBrowserFolderCache
    {
    public:
        /** @brief 디스크를 읽는 함수를 받습니다(시험은 세는 함수를 넘긴다). */
        explicit ContentBrowserFolderCache( ContentBrowserFolderScanFunc pfnScan );

        /**
         * @brief 폴더의 하위 폴더(절대 경로, 정렬)입니다. 처음 묻는 폴더만 디스크를 읽습니다. 경로의 대소문자 · 구분자는 같은 폴더로 봅니다.
         * @warning 돌려준 참조는 `clear` 전까지 유효합니다 — 다른 폴더를 물어 표가 커져도 옮겨지지 않는다(트리 그리기가 재귀 중에 든다).
         */
        const vector<string>& getChildFolders( string_view folderAbs );
        /** @brief 읽어 둔 것을 모두 버립니다(다음 물음이 다시 읽는다). */
        void clear() { _mapChildFolder.clear(); }
        /** @brief 읽어 둔 폴더 수입니다. */
        uint32 getCachedFolderCount() const { return static_cast<uint32>( _mapChildFolder.size() ); }

    private:
        unordered_map<string, unique_ptr<vector<string>>> _mapChildFolder; ///< 정규화한 폴더 경로 → 하위 폴더(값은 힙에 — 표가 커져도 참조가 산다)
        ContentBrowserFolderScanFunc                      _pfnScan;
    };
} // namespace sw::editor
