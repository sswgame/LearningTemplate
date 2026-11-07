/**
 * @file EditorSearchSources.h
 * @brief Command Palette 와 Quick Open 이 함께 쓰는 검색 제공자입니다 — 명령 · 패널 · 씬 오브젝트 · 에셋을 한 항목 형식으로 모으고, 같은 규칙으로 거르고 실행합니다.
 * @details 두 팝업은 진입점(Ctrl+Shift+P · Ctrl+P)과 제공자 순서만 다르다(유니티 Search 의 제공자 · VS Code 의 Quick Open 과 같은 자리). 예전에는 팝업마다
 *          목록을 따로 만들어 팔레트는 에셋을, Quick Open 은 명령 · 패널을 찾지 못했고 거르는 규칙도 달랐다(이어진 일치 우선 · 포함만).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

namespace sw::editor
{
    struct EditorAssetIndexEntry;

    /** @brief 항목을 낸 제공자입니다. 실행 방법이 이것으로 갈린다. */
    enum class EditorSearchSource : uint8
    {
        Command,     ///< 명령 레지스트리(메뉴 · 단축키와 같은 정의)
        Panel,       ///< 등록된 에디터 패널 열기
        SceneObject, ///< 활성 씬의 게임 오브젝트 고르기
        Asset,       ///< 리소스 트리의 에셋 열기(색인은 워커가 만든다)
    };

    /** @brief 검색 항목 하나입니다. */
    struct EditorSearchItem
    {
        EditorSearchSource _source{ EditorSearchSource::Command };
        EditorAssetType    _kind{};   ///< 에셋 항목의 종류(배지 색). 나머지는 빈 값
        string             _category; ///< 배지 글 — 명령 분류 · "Panel" · "GameObject" · 에셋 종류 이름
        string             _title;
        string             _detail;
        string             _key;           ///< 실행 열쇠 — 명령 id · 패널 id · 에셋 리소스 id
        uint64             _objectID{ 0 }; ///< 씬 오브젝트 id
    };

    /**
     * @struct EditorSearchSources
     * @brief 제공자마다 항목을 모으는 함수와 공통 거르기 · 실행입니다.
     */
    struct EditorSearchSources
    {
        /** @brief 팔레트에 보이는 명령을 @p inoutListItem 끝에 붙입니다. */
        static void appendCommands( vector<EditorSearchItem>& inoutListItem );
        /** @brief 등록된 패널 열기를 붙입니다. */
        static void appendPanels( vector<EditorSearchItem>& inoutListItem );
        /** @brief 활성 씬의 게임 오브젝트를 붙입니다. 씬이 없으면 아무것도 붙이지 않는다. */
        static void appendSceneObjects( vector<EditorSearchItem>& inoutListItem );
        /** @brief 워커가 만든 에셋 색인을 붙입니다. */
        static void appendAssets( const vector<EditorAssetIndexEntry>& listEntry, vector<EditorSearchItem>& inoutListItem );
        /**
         * @brief @p pattern 으로 거릅니다 — 제목 · 분류 · 설명에 이어진 그대로 든 것을 먼저, 흩어진 글자 순서로 맞는 것을 뒤에 둔다(각 묶음 안은 @p listSource 순서 · 들어온 순서). 제공자 목록을 여럿 받아 팝업마다 제공자 순서를 정한다.
         * @details 흩어진 일치만으로 줄을 세우면 Enter 가 뜻밖의 항목을 고른다("Open the GM" 이 "Open the paired animation editor (... alignment ...)" 에 먼저 걸렸다).
         *          검색어가 비면 모두다.
         */
        static void filter( std::initializer_list<const vector<EditorSearchItem>*> listSource, string_view pattern, vector<const EditorSearchItem*>& outListFiltered );
        /** @brief 항목을 실행합니다(명령 실행 · 패널 열기 · 오브젝트 고르기 · 에셋 열기). */
        static void execute( const EditorSearchItem& item );
        /** @brief 대소문자 없이 @p pattern 의 글자가 @p text 에 순서대로 나오면 true 입니다(흩어진 일치). */
        static bool matchesScattered( string_view text, string_view pattern );
        /** @brief 대소문자 없이 @p pattern 이 @p text 에 이어진 그대로 들어 있으면 true 입니다. */
        static bool containsIgnoreCase( string_view text, string_view pattern );
    };
} // namespace sw::editor
