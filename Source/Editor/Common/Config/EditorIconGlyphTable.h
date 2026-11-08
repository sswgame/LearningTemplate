/**
 * @file EditorIconGlyphTable.h
 * @brief 에디터 아이콘 글꼴의 이름 → 코드포인트 표(`editor/fonts/sweditoricons.glyphs.xml`)입니다. 글꼴을 바꾸면 이 표를 바꾼다(코드는 이름으로만 찾는다).
 * @details 표는 데이터다 — 고도도 에디터 아이콘을 이름으로 찾고(`get_editor_theme_icon`) 그림은 파일에 둔다. 코드는 이름만 안다(`SW_EDITOR_ICON`) — 글꼴을 바꾸면 표만 바꾼다.
 *          테마 색(`SW_THEME_COLOR`)과 같은 모양이다: 표에 없는 이름은 Missing 아이콘을 그리고 이름마다 한 번 오류를 남기며, 단위 테스트(`EditorIconNames`)가 소스의
 *          이름 리터럴이 모두 표에 있는지 본다. 등록 줄 · 상수 표는 글리프가 아니라 이름(`SW_EDITOR_ICON_NAME`)을 들고 그릴 때 찾는다 — 정적 초기화는 표를 읽기 전이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Config/EditorIconName.h"
#include "Editor/Common/EditorExports.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    struct EditorComponentIconTable;

    /**
     * @brief 아이콘 하나입니다.
     * @code
     *     <EditorIconGlyph _name="Save" _codepoint="U+E003" />
     * @endcode
     */
    REFLECT()
    struct EditorIconGlyph
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Icon name code looks up (SW_EDITOR_ICON)" )
        string _name{};
        PROPERTY( Required, Tooltip = "Codepoint in the icon font, U+XXXX within the basic multilingual plane" )
        string _codepoint{};
    };

    /** @brief 아이콘 글꼴 한 벌의 표입니다. 이름 · 코드포인트는 겹치지 않고 `Missing` 이 있어야 합니다. */
    REFLECT()
    struct SW_EDITOR_API EditorIconGlyphTable
    {
        REFLECT_BODY();
        /** @brief 표에 없는 이름을 찾을 때 대신 그리는 아이콘입니다(테마 색의 마젠타와 같은 자리 — 오류는 따로 남긴다). */
        static constexpr const utf8* kMissingIconName = "Missing";

        PROPERTY( Required, Tooltip = "Icons of the font" )
        vector<EditorIconGlyph> _listIcon;

        /** @brief 리소스 경로의 XML 을 엄격하게 읽고 검사합니다. 실패하면 오류를 남기고 false 이며 표는 비어 있습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief XML 글을 엄격하게 읽고 검사합니다(`loadFromAsset` 의 본문 — 테스트 · 도구). @p sourceName 은 오류 글에 쓴다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 이름의 글리프(UTF-8)입니다. 없으면 nullptr 입니다. */
        const utf8* findGlyph( const hashed_string& name ) const;
        /** @brief 글꼴에서 읽을 코드포인트 구간입니다. ImGui 글리프 범위 형식(짝마다 처음 · 끝, 0 으로 끝남)입니다. */
        const vector<uint16>& getGlyphRanges() const { return _listGlyphRange; }

        /** @brief `U+XXXX` 를 코드포인트로 읽습니다. 형식이 틀리거나 기본 다국어 평면 밖이면 false 입니다. */
        [[nodiscard]] static bool parseCodepoint( string_view text, uint16& outCodepoint );

        // 아이콘 이름 → 글리프입니다. 표는 셸(`ImGuiEditor`)이 들고 글꼴을 세울 때 읽어 여기 게시한다(`EditorUIContextState::publish` 와 같은 모양).
        /** @brief 셸이 표 둘을 게시합니다(읽은 뒤). 셸을 내릴 때 nullptr 둘로 거둔다. 표는 게시가 거둘 때까지 살아 있어야 한다. */
        static void publish( const EditorIconGlyphTable* pGlyphTable, const EditorComponentIconTable* pComponentTable );
        /** @brief 게시된 컴포넌트 아이콘 표입니다. 게시 전에 부르면 멈춘다(그리기는 셸이 선 뒤다). */
        static const EditorComponentIconTable& getComponentIconTable();
        /** @brief 이름의 글리프(UTF-8)입니다. 표에 없으면 Missing 아이콘이고 오류를 남깁니다(이름마다 한 번). 늘 nullptr 이 아닙니다. */
        static const utf8* findPublishedGlyph( const hashed_string& name );
        /** @brief 데이터 · 등록 줄이 든 이름으로 찾습니다(`findGlyph` 와 같은 규칙). @p pName 이 nullptr 이거나 비면 nullptr(아이콘 없음)입니다. */
        static const utf8* findGlyphByName( const utf8* pName );
        /** @brief 이름 형으로 찾습니다(`SW_EDITOR_ICON_NAME`). 비었으면 nullptr 입니다. */
        static const utf8* findGlyphByName( EditorIconName name ) { return findGlyphByName( name._pName ); }

    private:
        vector<hashed_string> _listNameKey;    ///< `_listIcon` 과 같은 순서의 이름 키(읽을 때 만든다)
        vector<string>        _listGlyphUTF8;  ///< `_listIcon` 과 같은 순서의 글리프(읽을 때 만든다)
        vector<uint16>        _listGlyphRange; ///< 읽을 때 만든다
    };
} // namespace sw::editor

/** @brief 아이콘 이름 @p NameLiteral(문자열 리터럴)의 글리프입니다. 호출 자리마다 이름 키를 한 번만 intern 한다. */
#define SW_EDITOR_ICON( NameLiteral ) \
    ::sw::editor::EditorIconGlyphTable::findPublishedGlyph( []() -> const ::sw::hashed_string& {                                                                         \
        static const ::sw::hashed_string s_key{ NameLiteral };                                                                                             \
        return s_key; }() )
