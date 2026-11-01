/**
 * @file EditorComponentIconTable.h
 * @brief 컴포넌트 아이콘의 데이터 표(`editor/data/componenticons.xml`)입니다. 리플렉션 Category → 아이콘 이름 · 색 이름, 기본 · 오브젝트 아이콘, 중립 색입니다.
 * @details 컴포넌트 선언의 메타(`Icon=` · `IconColor=`)가 먼저이고, 메타가 없는 타입(키트 · 게임)이 Category 로 여기서 맞는다. 아이콘 이름은 아이콘 표(`EditorIconGlyphTable`),
 *          색 이름은 테마 색 키 `Component.<이름>`(`*.tokens.json`)이다. 둘 다 없으면 Missing 아이콘 · 마젠타와 오류다(`EditorComponentIcon`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /**
     * @brief 리플렉션 Category 하나의 아이콘입니다.
     * @code
     *     <EditorComponentCategoryIcon _category="Physics 2D" _icon="Physics" _color="Physics" />
     * @endcode
     */
    REFLECT()
    struct EditorComponentCategoryIcon
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Reflection category (REFLECT( Category = ... ))" )
        string _category{};
        PROPERTY( Required, Tooltip = "Icon name in the editor icon table" )
        string _icon{};
        PROPERTY( Required, Tooltip = "Color name - the theme color key is Component.<name>" )
        string _color{};
    };

    /** @brief 컴포넌트 아이콘 표 한 벌입니다. */
    REFLECT()
    struct EditorComponentIconTable
    {
        REFLECT_BODY();
        /** @brief 색 이름 @p colorName 의 테마 색 키(`Component.<이름>`)입니다. */
        static string makeColorKey( string_view colorName ) { return string( "Component." ) + string( colorName ); }

        PROPERTY( Required, Tooltip = "Icon of a component no meta or category matches" )
        string _defaultIcon{};
        PROPERTY( Required, Tooltip = "Icon of an object row with no billboard component" )
        string _objectIcon{};
        PROPERTY( Required, Tooltip = "Color name of a component that names none" )
        string _neutralColor{};
        PROPERTY( Required, Tooltip = "Icons of reflection categories" )
        vector<EditorComponentCategoryIcon> _listCategory;

        /** @brief 리소스 경로의 XML 을 엄격하게 읽고 검사합니다(Category 가 겹치지 않는다). 실패하면 오류를 남기고 false 이며 표는 비어 있습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief Category 의 줄입니다. 없으면 nullptr 입니다. */
        const EditorComponentCategoryIcon* findCategory( string_view category ) const;
    };
} // namespace sw::editor
