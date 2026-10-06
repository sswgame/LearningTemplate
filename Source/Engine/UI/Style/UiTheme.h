/**
 * @file UiTheme.h
 * @brief UI 테마 — 이름 하나에 스타일 시트 묶음입니다(`engine/ui/uithemes.xml`, 게임 프리셋 `_uiThemes` 가 덮어쓴다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @struct UiThemeDesc @brief 테마 하나 — 모든 화면의 문서 시트 앞에 거는 시트들입니다. */
    REFLECT()
    struct SW_API UiThemeDesc
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Name" )
        hashed_string _name{};
        PROPERTY( DisplayName = "Style Sheets", AssetType = "UiStyleSheet" )
        vector<string> _listStyleSheet{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UiThemeCatalog
     * @brief 고를 수 있는 테마들과 기본 테마입니다(언리얼 Slate 스타일 세트 · Godot Theme 의 자리). `UiSystem::setTheme` 이 이름으로 고릅니다.
     */
    REFLECT()
    struct SW_API UiThemeCatalog
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Default Theme" )
        hashed_string _defaultTheme{};
        PROPERTY( DisplayName = "Themes" )
        vector<UiThemeDesc> _listTheme{};

        /** @brief 리소스 경로의 XML 을 읽습니다. 실패하면(파일 없음 · 모르는 키) 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromResource( string_view resourcePath );
        /** @brief 이름의 테마입니다. 없으면 nullptr 입니다. */
        const UiThemeDesc* findTheme( const hashed_string& name ) const;
    };
} // namespace sw
