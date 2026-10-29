/**
 * @file UIThemeController.h
 * @brief 테마 목록, 지금 테마, 사용자 설정(gv_uiTheme) 추적을 맡고 화면의 스타일 세트를 테마 시트, 문서 시트 순서로 다시 짓습니다(엔진 범위 서브시스템 `UIThemes`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/UI/Style/UITheme.h"

namespace sw
{
    class UIScreen;
    class UIStyleSheetCache;

    /** @brief 지금 테마가 바뀌었다는 알림의 칸입니다. 화면을 가진 서비스(화면 스택 · 오프스크린 화면)가 걸고 자기 화면의 스타일 세트를 다시 짓습니다. */
    using UIThemeChangedEvent = MulticastDelegate<void()>;

    /**
     * @class UIThemeController
     * @brief 테마 고르기 규칙입니다(엔진 범위 서브시스템 `UIThemes`, `engine::getSubsystem<UIThemeController>()`). 목록을 받을 때 설정의 테마(없으면 기본 테마)를 고르고,
     *        설정이 바뀌면 따라가며, 모르는 이름은 거절합니다.
     * @details 화면 스택(`UISystem`)과 화면 스택 밖 화면(`UIOffscreenScreens`)이 같은 테마를 보므로 화면 스택의 값 멤버가 아니다(언리얼 `FSlateStyleRegistry`,
     *          Godot `ThemeDB` 처럼 화면과 따로 선다). 테마가 바뀌면 `onThemeChanged` 로 알리고, 화면을 도는 일(모든 화면에 다시 적용)은 화면 목록을 가진 쪽이 한다.
     *          기동이 테마 목록(`engine/ui/uithemes.xml`, 팩 설정 `_uiThemes` 가 덮어씀)을 읽는다 — 못 읽으면 데이터 오류라 기동 실패다.
     */
    class SW_API UIThemeController final : public ISubsystem
    {
    public:
        UIThemeController();
        ~UIThemeController() override;

        // ISubsystem — 기동이 테마 목록을 읽어 설정의 테마를 고른다.
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "UIThemes";
        SubsystemInitResult          initializeSubsystem() override;
        void                         shutdownSubsystem() override;
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        /** @brief 팩 설정의 목록 경로를 읽고, 테마 시트를 스타일 시트 캐시에서 읽는다. 설정의 테마 이름은 사용자 설정이다. */
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override { return EngineSubsystemHost::kClient; }

        /**
         * @brief 테마 목록을 바꿉니다. 사용자 설정 `gv_uiTheme`(accessibility.uiTheme)의 테마가 목록에 있으면 그 테마로, 없으면 목록의 기본 테마로 바꿉니다.
         * @details 그 뒤 설정이 바뀌면 `syncSetting` 이 따라간다(모르는 이름이면 경고하고 지금 테마를 유지한다).
         */
        void                  setCatalog( const UIThemeCatalog& catalog );
        const UIThemeCatalog& getCatalog() const { return _catalog; }
        /** @brief 지금 테마입니다(없으면 빈 이름이며 문서 시트만 씁니다). */
        const hashed_string& getThemeName() const { return _themeName; }
        /**
         * @brief 테마 @p name 을 지금 테마로 하고 `onThemeChanged` 로 알립니다(화면마다 테마 시트, 문서 시트 순서로 다시 적용한다). 목록에 없으면 경고하고 false 이며 지금 테마를 유지합니다.
         * @details 읽지 못한 시트는 오류를 남기고 제외한다(나머지 시트로 그린다).
         */
        [[nodiscard]] bool setTheme( const hashed_string& name );
        /** @brief 사용자 설정 `gv_uiTheme` 이 지난번과 다르면 그 테마로 바꿉니다(빈 값이거나 테마 목록이 없으면 그대로 둡니다). UI 갱신(`UISystem::update`)의 첫머리가 부릅니다. */
        void syncSetting();
        /** @brief 지금 테마가 바뀔 때마다 부르는 칸입니다(`add` 가 돌려준 구독을 든다). */
        UIThemeChangedEvent& onThemeChanged() { return _onThemeChanged; }
        /** @brief 테마 @p themeName(빈 이름이면 지금 테마)의 서술입니다. 둘 다 비었거나 목록에 없으면 nullptr 입니다. */
        const UIThemeDesc* findTheme( const hashed_string& themeName ) const;
        /** @brief 화면의 스타일 시트를 테마 @p themeName(빈 이름이면 지금 테마) 시트, 화면 시트 순서로 다시 적용하고 트리 전체를 다시 맞추게 합니다. */
        void rebuildStyleSet( UIScreen& screen, const hashed_string& themeName = hashed_string{} ) const;

    private:
        /** @brief 기억한 설정 값의 테마가 목록에 있으면 그 이름, 아니면 빈 이름입니다. */
        hashed_string findSettingTheme() const;

    private:
        UIThemeCatalog      _catalog;
        hashed_string       _themeName;      ///< 지금 테마입니다(없으면 빈 이름이며 문서 시트만 씁니다).
        string              _setting;        ///< 마지막으로 본 `gv_uiTheme` 값입니다(바뀔 때만 테마를 고릅니다).
        UIStyleSheetCache*  _pStyleSheets;   ///< 테마 · 문서 시트를 읽는 캐시(기동이 찾는다)
        UIThemeChangedEvent _onThemeChanged; ///< 테마가 바뀐 알림(화면 스택 · 오프스크린 화면)
    };
} // namespace sw
