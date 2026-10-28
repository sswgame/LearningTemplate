/**
 * @file UIThemeController.h
 * @brief 테마 목록, 지금 테마, 사용자 설정(gv_uiTheme) 추적을 맡고 화면의 스타일 세트를 테마 시트, 문서 시트 순서로 다시 짓습니다(`UISystem` 의 값 멤버).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Style/UITheme.h"

namespace sw
{
    class UIScreen;
    class UIStyleSheetCache;

    /**
     * @class UIThemeController
     * @brief 테마 고르기 규칙입니다. 목록을 받을 때 설정의 테마(없으면 기본 테마)를 고르고, 설정이 바뀌면 그 이름을 내놓으며, 모르는 이름은 거절합니다.
     * @details 화면을 도는 일(모든 화면에 다시 적용)은 화면 목록을 가진 `UISystem` 이 한다. 이 클래스는 엔진 서비스를 읽지 않으므로
     *          설정 값은 부르는 쪽이 넘긴다(테스트가 엔진 없이 부를 수 있다).
     */
    class SW_API UIThemeController
    {
    public:
        UIThemeController();

        /** @brief 테마 목록을 바꾸고 지금 설정 값 @p setting 을 기억합니다. 테마는 바꾸지 않습니다(부르는 쪽이 `findSettingTheme` 이나 기본 테마로 고릅니다). */
        void                  setCatalog( const UIThemeCatalog& catalog, const string& setting );
        const UIThemeCatalog& getCatalog() const { return _catalog; }
        /** @brief 지금 테마입니다(없으면 빈 이름이며 문서 시트만 씁니다). */
        const hashed_string& getThemeName() const { return _themeName; }
        /** @brief 기억한 설정 값의 테마가 목록에 있으면 그 이름, 아니면 빈 이름입니다. */
        hashed_string findSettingTheme() const;
        /** @brief 테마 @p name 을 지금 테마로 합니다. 목록에 없으면 경고하고 false 이며 지금 테마를 유지합니다. */
        [[nodiscard]] bool selectTheme( const hashed_string& name );
        /**
         * @brief 설정 값 @p setting 이 지난번과 다르면 기억하고, 고를 테마가 있으면 @p outTheme 에 넣어 true 를 반환합니다.
         * @details 빈 값이거나 테마 목록이 비었으면 기억만 하고 false 다. 모르는 이름은 걸러 내지 않는다(`selectTheme` 이 경고한다).
         */
        [[nodiscard]] bool pollSetting( const string& setting, hashed_string& outTheme );
        /** @brief 테마 @p themeName(빈 이름이면 지금 테마)의 서술입니다. 둘 다 비었거나 목록에 없으면 nullptr 입니다. */
        const UIThemeDesc* findTheme( const hashed_string& themeName ) const;
        /** @brief 화면의 스타일 시트를 테마 @p themeName(빈 이름이면 지금 테마) 시트, 화면 시트 순서로 다시 적용하고 트리 전체를 다시 맞추게 합니다. */
        void rebuildStyleSet( UIScreen& screen, UIStyleSheetCache& styleSheetCache, const hashed_string& themeName ) const;

    private:
        UIThemeCatalog _catalog;
        hashed_string  _themeName; ///< 지금 테마입니다(없으면 빈 이름이며 문서 시트만 씁니다).
        string         _setting;   ///< 마지막으로 본 `gv_uiTheme` 값입니다(바뀔 때만 테마를 고릅니다).
    };
} // namespace sw
