/**
 * @file ContentBrowserReferenceSearch.h
 * @brief 콘텐츠 브라우저의 참조 찾기입니다. Find References 와 Show Dependencies 의 결과를 가집니다. 색인은 에디터 전역(`AssetReferenceTracker`)의 것을 읽습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Asset/Reference/EditorReferenceIndex.h"

namespace sw::editor
{
    /**
     * @class ContentBrowserReferenceSearch
     * @brief 마지막 질의의 결과입니다. 언리얼 Content Browser 의 Reference Viewer 를 테이블 하나로 줄인 것입니다.
     * @details 역색인을 최신으로 두는 일은 `AssetHotReload` 가 가진 `AssetReferenceTracker` 가 한다 — 핫 리로드의 연쇄도 같은 색인을 읽는다. 질의와 결과는 UI 스레드에서만 다룬다.
     */
    class ContentBrowserReferenceSearch
    {
    public:
        ContentBrowserReferenceSearch();

        /**
         * @brief 에셋의 참조를 찾아 결과를 바꿉니다.
         * @param bDependencies false 면 그 에셋을 사용하는 쪽(Find References)을, true 면 그 에셋이 사용하는 쪽(Show Dependencies)을 찾습니다.
         */
        void query( const EditorReferenceIndex& index, string_view absolutePath, bool bDependencies );

        /** @brief 마지막 질의의 대상 리소스 id 를 반환합니다. */
        const string& getQueryID() const { return _queryID; }
        /** @brief 마지막 질의가 Show Dependencies 였으면 true 입니다. */
        bool isDependencyQuery() const { return _bDependencyQuery == SW_TRUE; }
        /** @brief 마지막 질의의 결과 행을 반환합니다. */
        const vector<EditorAssetReference>& getResultList() const { return _listResult; }

    private:
        string                       _queryID; ///< 마지막 질의의 대상 리소스 id
        vector<EditorAssetReference> _listResult;
        uint8                        _bDependencyQuery : 1; ///< 마지막 질의가 Show Dependencies 였다
    };
} // namespace sw::editor
