/**
 * @file ContentBrowserTypeFilter.h
 * @brief 콘텐츠 브라우저의 종류 필터(All · 종류마다 하나 · Other)와 고른 필터입니다(ImGui 없음).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

namespace sw::editor
{
    /**
     * @class ContentBrowserTypeFilter
     * @brief 종류 필터 목록을 종류 등록에서 만들고, 고른 필터로 항목을 거릅니다.
     * @details 종류는 플러그인 모듈이 올라오고 내려갈 때 바뀐다. 목록을 세대(`EditorAssetTypeRegistry::getGeneration`)가 바뀔 때 다시 만들고,
     *          고른 필터는 순번이 아니라 종류로 기억한다 — 앞에 종류가 끼어도 고른 필터가 다른 종류로 바뀌지 않는다. 고른 종류가 사라지면 All 로 돌아간다.
     */
    class ContentBrowserTypeFilter
    {
    public:
        /** @brief 종류 등록이 바뀌었으면 목록을 다시 만듭니다. 그리기 전에 부릅니다. */
        void sync();

        /** @brief 필터 목록입니다(`sync` 뒤). 맨 앞이 All, 맨 뒤가 Other 입니다. */
        const vector<EditorAssetBrowserFilter>& getFilters() const { return _listFilter; }
        /** @brief 고른 필터의 순번입니다. */
        uint32 getSelectedIndex() const { return _selectedIndex; }
        /** @brief 고른 필터의 라벨입니다. 목록이 아직 없으면 "All" 입니다. */
        const utf8* getSelectedLabel() const;
        /** @brief 순번 @p index 의 필터를 고릅니다. 범위 밖이면 무시합니다. */
        void select( uint32 index );

        /** @brief 파일 @p path 가 고른 필터를 지나면 true 입니다. 폴더는 부르는 쪽이 거르지 않습니다. */
        bool passes( string_view path ) const;

    private:
        vector<EditorAssetBrowserFilter> _listFilter;
        EditorAssetType                  _selectedKind{};          ///< 고른 종류. All · Other 는 빈 값이다
        uint32                           _selectedIndex{ 0 };      ///< `_listFilter` 안의 순번
        uint32                           _generation{ 0 };         ///< 목록을 만든 종류 등록의 세대
        bool                             _bSelectedOther{ false }; ///< Other 를 골랐다
        bool                             _bBuilt{ false };         ///< 목록을 한 번이라도 만들었다
    };
} // namespace sw::editor
