#include "pch.h"

#include "Engine/UI/Style/UIThemeController.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Style/UIStyleSet.h"
#include "Engine/UI/Style/UIStyleSheet.h"
#include "Engine/UI/Style/UIStyleSheetCache.h"

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
    {
    }

    void UIThemeController::setCatalog( const UIThemeCatalog& catalog, const string& setting )
    {
        _catalog = catalog;
        _setting = setting;
    }

    hashed_string UIThemeController::findSettingTheme() const
    {
        if ( _setting.empty() || _catalog.findTheme( hashed_string( _setting ) ) == nullptr )
            return hashed_string{};
        return hashed_string( _setting );
    }

    bool UIThemeController::selectTheme( const hashed_string& name )
    {
        if ( _catalog.findTheme( name ) == nullptr )
        {
            SW_LOG_ERROR( "[UI] Unknown UI theme '%#'", name.c_str() );
            return false;
        }
        _themeName = name;
        return true;
    }

    bool UIThemeController::pollSetting( const string& setting, hashed_string& outTheme )
    {
        if ( setting == _setting )
            return false;
        _setting = setting;
        if ( setting.empty() || _catalog._listTheme.empty() )
            return false;
        outTheme = hashed_string( setting );
        return true;
    }

    const UIThemeDesc* UIThemeController::findTheme( const hashed_string& themeName ) const
    {
        const hashed_string& theme = themeName.empty() ? _themeName : themeName;
        return theme.empty() ? nullptr : _catalog.findTheme( theme );
    }

    void UIThemeController::rebuildStyleSet( UIScreen& screen, UIStyleSheetCache& styleSheetCache, const hashed_string& themeName ) const
    {
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
