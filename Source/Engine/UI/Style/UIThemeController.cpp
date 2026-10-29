#include "pch.h"

#include "Engine/UI/Style/UIThemeController.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/PackEngineSettingsSubsystem.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Style/UIStyleSet.h"
#include "Engine/UI/Style/UIStyleSheet.h"
#include "Engine/UI/Style/UIStyleSheetCache.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    SW_LOG_CALLER( "UIThemeController" );

    namespace
    {
        /** @brief 시트를 캐시에서 읽습니다(읽지 못한 것은 오류를 남기고 제외합니다). */
        void appendStyleSheets( UIStyleSheetCache& styleSheetCache, const vector<string>& listPath, vector<shared_ptr<const UIStyleSheetAsset>>& inoutListSheet )
        {
            for ( const string& path : listPath )
            {
                string                                    error;
                const shared_ptr<const UIStyleSheetAsset> sheet = styleSheetCache.findOrLoad( path, error );
                if ( sheet == nullptr )
                {
                    SW_LOG_ERROR( "[UI] Style sheet is not loaded: %#", error.c_str() );
                    continue;
                }
                inoutListSheet.push_back( sheet );
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    UIThemeController::UIThemeController()
        : _catalog{}
        , _themeName{}
        , _setting{}
        , _pStyleSheets{ nullptr }
        , _onThemeChanged{}
    {
    }

    UIThemeController::~UIThemeController() = default;

    SubsystemInitResult UIThemeController::initializeSubsystem()
    {
        _pStyleSheets = &engine::getSubsystem<UIStyleSheetCache>();
        // 테마 목록 — 팩 설정이 덮어쓰면 그것, 아니면 엔진 기본. 모르는 키는 기동 오류다(배율 규칙과 같다).
        const EngineDefaultAssets& defaultAssets = engine::getSubsystem<EngineDefaultAssets>();
        const PackEngineSettings&  packSettings  = engine::getSubsystem<PackEngineSettings>();
        UIThemeCatalog             themes{};
        if ( themes.loadFromAsset( AssetID{ PackEngineSettings::resolve( packSettings._uiThemes, defaultAssets._uiThemes ) } ) == false )
            return SubsystemInitResult::Failed;
        setCatalog( themes );
        return SubsystemInitResult::Succeeded;
    }

    void UIThemeController::shutdownSubsystem()
    {
        _catalog   = UIThemeCatalog{};
        _themeName = hashed_string{};
        _setting.clear();
        _pStyleSheets = nullptr;
    }

    span<const utf8* const> UIThemeController::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { PackEngineSettingsSubsystem::kSubsystemName, UIStyleSheetCache::kSubsystemName, UserSettingsManager::kSubsystemName };
        return kArrDependency;
    }

    void UIThemeController::setCatalog( const UIThemeCatalog& catalog )
    {
        _catalog = catalog;
        _setting = gv_uiTheme;
        // 사용자 설정(gv_uiTheme)의 테마가 목록에 있으면 그것을, 없으면 목록의 기본 테마를 쓴다. 테마를 두지 않은 게임 목록에서는 설정 값이 조용히 기본 테마가 된다.
        const hashed_string settingTheme = findSettingTheme();
        if ( settingTheme.empty() == false )
        {
            (void)setTheme( settingTheme );
            return;
        }
        const hashed_string& defaultTheme = _catalog._defaultTheme;
        if ( defaultTheme.empty() == false && setTheme( defaultTheme ) == false )
            SW_LOG_ERROR( "[UI] Default UI theme '%#' is not in the theme catalog", defaultTheme.c_str() );
    }

    hashed_string UIThemeController::findSettingTheme() const
    {
        if ( _setting.empty() || _catalog.findTheme( hashed_string( _setting ) ) == nullptr )
            return hashed_string{};
        return hashed_string( _setting );
    }

    bool UIThemeController::setTheme( const hashed_string& name )
    {
        if ( _catalog.findTheme( name ) == nullptr )
        {
            SW_LOG_ERROR( "[UI] Unknown UI theme '%#'", name.c_str() );
            return false;
        }
        _themeName = name;
        _onThemeChanged.broadcast();
        return true;
    }

    void UIThemeController::syncSetting()
    {
        // 실행 중에 바꾼 값: 모르는 이름이면 setTheme 이 경고하고 지금 테마를 유지한다(게임 테마 목록에 그 이름이 없는 경우).
        const string& setting = gv_uiTheme;
        if ( setting == _setting )
            return;
        _setting = setting;
        if ( setting.empty() || _catalog._listTheme.empty() )
            return;
        (void)setTheme( hashed_string( setting ) );
    }

    const UIThemeDesc* UIThemeController::findTheme( const hashed_string& themeName ) const
    {
        const hashed_string& theme = themeName.empty() ? _themeName : themeName;
        return theme.empty() ? nullptr : _catalog.findTheme( theme );
    }

    void UIThemeController::rebuildStyleSet( UIScreen& screen, const hashed_string& themeName ) const
    {
        UIStyleSheetCache&                          styleSheetCache = *_pStyleSheets;
        vector<shared_ptr<const UIStyleSheetAsset>> listSheet;
        const UIThemeDesc*                          pTheme = findTheme( themeName );
        if ( pTheme != nullptr )
            appendStyleSheets( styleSheetCache, pTheme->_listStyleSheet, listSheet );
        appendStyleSheets( styleSheetCache, screen._listStyleSheet, listSheet );
        if ( screen._styleSet == nullptr )
            screen._styleSet = make_unique<UIStyleSet>();
        screen._styleSet->setSheets( std::move( listSheet ) );
        // 트리 전체를 다시 맞춘다. 루트의 kStyle 은 계산이 바뀐 만큼 자손으로 내려가며, 규칙이 바뀌었으므로 자손도 모두 표시한다.
        vector<Widget*> listWidget;
        screen.getTree().collectWidgetsInDocumentOrder( listWidget );
        for ( Widget* pWidget : listWidget )
        {
            pWidget->invalidate( WidgetDirty::kStyle );
        }
    }
} // namespace sw
