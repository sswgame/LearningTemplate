/**
 * @file ContentBrowserCollectionStore.h
 * @brief 콘텐츠 브라우저의 저장 검색과 에셋 컬렉션입니다. 줄 단위 텍스트로 읽고 쓰며 ImGui 에 의존하지 않습니다(단위 테스트가 본다).
 * @details 언리얼 콘텐츠 브라우저의 Collections(로컬 · 공유) · 유니티 Project 창의 Saved Searches 와 같은 자리다.
 *          로컬 것은 에디터 상태 폴더의 `ContentBrowserCollectionStore.txt`, 공유 것은 프로젝트의 `Config/Editor/ContentCollections.txt`(커밋한다)에 둔다.
 *          줄 형식: `search<TAB>이름<TAB>종류 필터 라벨<TAB>검색 글` · `collection<TAB>이름<TAB>에셋 id`(에셋마다 한 줄). 빈 줄 · 모르는 줄은 건너뛴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 저장 검색 하나 — 검색 글과 종류 필터를 이름으로 다시 겁니다. */
    struct ContentBrowserSavedSearch
    {
        string _name;
        string _typeFilterLabel; ///< 종류 필터 라벨(`EditorAssetBrowserFilter::_pLabel`). 비면 All
        string _searchText;
        bool   _bShared{ false }; ///< 프로젝트와 함께 커밋하는 공유 목록이다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /** @brief 에셋 컬렉션 하나 — 폴더와 상관없이 모은 에셋 id 목록입니다(파일을 옮기지 않는다). */
    struct ContentBrowserCollection
    {
        string         _name;
        vector<string> _listAssetID;
        bool           _bShared{ false }; ///< NOLINT(sw-bool-flag): 위와 같다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ContentBrowserCollectionStore
     * @brief 저장 검색 · 컬렉션 목록과 두 파일(로컬 · 공유)의 읽기 · 쓰기입니다. 이름은 범위(로컬 · 공유) 안에서 겹치지 않습니다.
     */
    class ContentBrowserCollectionStore
    {
    public:
        ContentBrowserCollectionStore();

        const vector<ContentBrowserSavedSearch>& getSavedSearches() const { return _listSavedSearch; }
        const vector<ContentBrowserCollection>&  getCollections() const { return _listCollection; }

        /** @brief 저장 검색을 더하거나(같은 범위 · 같은 이름이면 바꾼다) 고칩니다. 이름이 비면 false 입니다. */
        bool saveSearch( const ContentBrowserSavedSearch& search );
        /** @brief 저장 검색을 지웁니다. 있었으면 true 입니다. */
        bool removeSearch( string_view name, bool bShared );
        /** @brief 빈 컬렉션을 만듭니다. 이름이 비었거나 같은 범위에 이미 있으면 false 입니다. */
        bool createCollection( string_view name, bool bShared );
        /** @brief 컬렉션을 지웁니다(에셋 파일은 그대로). 있었으면 true 입니다. */
        bool removeCollection( string_view name, bool bShared );
        /** @brief 컬렉션에 에셋 id 를 더합니다. 이미 있으면 false 입니다. */
        bool addToCollection( string_view name, bool bShared, string_view assetID );
        /** @brief 컬렉션에서 에셋 id 를 뺍니다. 있었으면 true 입니다. */
        bool removeFromCollection( string_view name, bool bShared, string_view assetID );
        /** @brief 이름 · 범위의 컬렉션입니다. 없으면 nullptr 입니다. */
        const ContentBrowserCollection* findCollection( string_view name, bool bShared ) const;

        /** @brief 한 범위(@p bShared)의 목록을 줄 텍스트로 만듭니다. */
        string toText( bool bShared ) const;
        /** @brief 줄 텍스트를 읽어 그 범위(@p bShared)의 목록을 바꿉니다. 다른 범위의 목록은 그대로 둡니다. */
        void fromText( string_view text, bool bShared );

        /** @brief 두 파일을 읽습니다. 없는 파일은 빈 목록이다(처음 실행). */
        void loadFiles( string_view localFilePath, string_view sharedFilePath );
        /** @brief 한 범위의 파일을 씁니다. 실패하면 경고하고 false 입니다. */
        [[nodiscard]] bool saveFile( string_view filePath, bool bShared ) const;
        /** @brief 로컬 파일 경로입니다(에디터 상태 폴더의 `kLocalFileName`). */
        static string getLocalFilePath();
        /** @brief 공유 파일 경로입니다(프로젝트 루트 아래 `kSharedFileName`). */
        static string getSharedFilePath();

        static constexpr const utf8* kLocalFileName  = "ContentBrowserCollectionStore.txt";
        static constexpr const utf8* kSharedFileName = "Config/Editor/ContentCollections.txt";

    private:
        vector<ContentBrowserSavedSearch> _listSavedSearch;
        vector<ContentBrowserCollection>  _listCollection;
    };
} // namespace sw::editor
