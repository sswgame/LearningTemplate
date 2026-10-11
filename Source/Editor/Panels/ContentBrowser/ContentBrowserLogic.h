/**
 * @file ContentBrowserLogic.h
 * @brief 콘텐츠 브라우저 패널(`ContentBrowserPanel`)의 판단 로직입니다. 브레드크럼 조각과 폴더 트리 캐시를 담당하며, ImGui 에 의존하지 않으므로 EditorTest 가 테스트합니다.
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
     * @brief 브레드크럼의 조각 하나입니다. 보이는 이름과 누르면 이동할 폴더를 함께 가집니다.
     * @details 브레드크럼을 문자열 하나("Favorites / Shaders / bin")로 보관하고 누를 때 다시 쪼개면, 루트가 아닌 첫 조각("Favorites")에서 절대 경로가
     *          끊겨 다음 조각이 상대 경로가 된다. 그래서 조각마다 절대 경로를 처음부터 함께 둔다.
     */
    struct ContentBrowserCrumb
    {
        string _label;        ///< 보이는 이름
        string _absolutePath; ///< 누르면 이동할 폴더다. 비어 있으면 누를 수 없는 그룹 이름이다("Favorites")
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ContentBrowserLogic
     * @brief 콘텐츠 브라우저가 호출하는 순수 함수 모음입니다. 언리얼 Content Browser 의 브레드크럼에 해당합니다.
     */
    struct ContentBrowserLogic
    {
        /** @brief 즐겨찾기에서 들어간 폴더의 브레드크럼을 만듭니다. `{ "Favorites", "" }, { @p label, @p absolutePath }` 입니다. */
        static void makeFavoriteTrail( string_view label, string_view absolutePath, vector<ContentBrowserCrumb>& outListCrumb );

        /**
         * @brief 루트 아래 폴더의 브레드크럼을 만듭니다. 루트 조각과, 루트에서 @p folderAbs 까지의 폴더마다 한 조각을 둡니다.
         * @details @p folderAbs 가 루트 아래가 아니면 루트 조각 없이 그 폴더 한 조각만 둔다.
         */
        static void makeFolderTrail( string_view rootLabel, string_view rootAbs, string_view folderAbs, vector<ContentBrowserCrumb>& outListCrumb );

        /** @brief 현재 브레드크럼 끝에 하위 폴더 조각 하나를 추가합니다. 이름은 폴더 이름입니다. */
        static void appendChildCrumb( string_view childAbs, vector<ContentBrowserCrumb>& inoutListCrumb );
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 폴더의 직속 하위 폴더 절대 경로를 디스크에서 읽어 채우는 함수 타입입니다(`EditorAssetCommands::collectChildFolders`). */
    using ContentBrowserFolderScanFunc = void ( * )( string_view folderAbs, vector<string>& outListChild );

    /**
     * @class ContentBrowserFolderCache
     * @brief 폴더 트리가 그리는 "폴더에서 정렬된 하위 폴더로" 의 대응을 처음 물을 때 한 번만 디스크에서 읽어 둡니다.
     * @details 트리를 그릴 때마다 보이는 노드마다 하위 폴더를 디스크에서 읽으면 트리가 펼쳐질수록 프레임이 느려진다(Debug 에서 16.8 ms).
     *          디스크가 바뀌면(Refresh 나 파일 감시의 변경 번호) 호출하는 쪽이 `clear` 한다.
     */
    class ContentBrowserFolderCache
    {
    public:
        /** @brief 디스크를 읽는 함수를 받습니다. 테스트는 호출 횟수를 세는 함수를 넘깁니다. */
        explicit ContentBrowserFolderCache( ContentBrowserFolderScanFunc pfnScan );

        /**
         * @brief 폴더의 하위 폴더를 절대 경로로 정렬해 반환합니다. 처음 묻는 폴더만 디스크를 읽습니다. 경로의 대소문자와 구분자가 달라도 같은 폴더로 봅니다.
         * @warning 반환한 참조는 `clear` 전까지 유효합니다. 다른 폴더를 물어 테이블이 커져도 옮겨지지 않습니다. 트리를 그릴 때 재귀 중에 이 참조를 보관하기 때문입니다.
         */
        const vector<string>& getOrScanChildFolders( string_view folderAbs );
        /** @brief 읽어 둔 것을 모두 버립니다. 다음에 물으면 다시 읽습니다. */
        void clear() { _mapChildFolder.clear(); }
        /** @brief 읽어 둔 폴더 수를 반환합니다. */
        uint32 getCachedFolderCount() const { return static_cast<uint32>( _mapChildFolder.size() ); }

    private:
        unordered_map<string, unique_ptr<vector<string>>> _mapChildFolder; ///< 정규화한 폴더 경로에서 하위 폴더로의 대응이다. 값을 힙에 두므로 테이블이 커져도 참조가 유효하다
        ContentBrowserFolderScanFunc                      _pfnScan;
    };
} // namespace sw::editor
