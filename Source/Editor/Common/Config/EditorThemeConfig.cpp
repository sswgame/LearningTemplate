#include "pch.h"

#include "Editor/Common/Config/EditorThemeConfig.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorAppearanceConfig.h"
#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
} // namespace sw::editor

namespace sw::editor
{
    bool EditorTheme::importChangedTokenFile( const AssetPaths& /*assetPaths*/, [[maybe_unused]] string_view relativePath ) // 배포본은 로그가 없다
    {
        editor::getSubsystem<EditorTheme>().markTokensChanged();
        SW_LOG_INFO_ID( "EditorTheme.ThemeTokensChanged", "Theme tokens changed: %#", string{ relativePath }.c_str() );
        return true;
    }
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct EditorThemeConfigInternal
        {
            /** @brief 사용자가 덮어쓰는 토큰 키입니다(글 상수 — 정적 초기화에는 이름 풀이 없다). 나머지 둥글기는 슬라이더 셋에서 정한다. */
            static constexpr const utf8* kAccentKey       = "Base.Accent";
            static constexpr const utf8* kRadiusWindowKey = "Radius.Window";
            static constexpr const utf8* kRadiusFrameKey  = "Radius.Frame";
            static constexpr const utf8* kRadiusTabKey    = "Radius.Tab";
            /** @brief 둥글기를 한 슬라이더로 묶을 때 스크롤바 둥글기는 입력 필드 둥글기의 이 배수입니다. */
            static constexpr float32 kScrollbarRadiusPerFrameRadius = 2.0f;

            static void setWindowRadius( float32 radius, EditorThemeConfig& inoutTheme )
            {
                inoutTheme._mapMetric[hashed_string( kRadiusWindowKey )] = radius;
                inoutTheme._mapMetric[hashed_string( "Radius.Popup" )]   = radius;
            }

            static void setFrameRadius( float32 radius, EditorThemeConfig& inoutTheme )
            {
                inoutTheme._mapMetric[hashed_string( kRadiusFrameKey )]    = radius;
                inoutTheme._mapMetric[hashed_string( "Radius.Scrollbar" )] = radius * kScrollbarRadiusPerFrameRadius;
                inoutTheme._mapMetric[hashed_string( "Radius.Grab" )]      = radius;
            }

            /** @brief 덮어쓴 둥글기의 저장 값입니다. 덮어쓰면 슬라이더 키는 늘 표에 있다(`overrideRadius`). */
            static float32 readSavedRadius( const EditorThemeConfig& theme, const hashed_string& key )
            {
                const float32* pValue = theme._bRadiusOverridden == SW_TRUE ? theme.findMetric( key ) : nullptr;
                return pValue != nullptr ? *pValue : EditorAppearanceConfig::kNotOverridden;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    const EditorThemeConfig* EditorThemeConfig::findByName( const vector<EditorThemeConfig>& listTheme, string_view name )
    {
        for ( const EditorThemeConfig& theme : listTheme )
        {
            if ( StringUtil::equals( theme._name, name, true ) )
                return &theme;
        }
        return nullptr;
    }

    bool EditorThemeConfig::buildFromSaved( const vector<EditorThemeConfig>& listTheme, const EditorAppearanceConfig& saved, EditorThemeConfig& outTheme )
    {
        const EditorThemeConfig* pTheme = findByName( listTheme, saved._themeName );
        if ( pTheme == nullptr )
            return false;
        outTheme = *pTheme;

        if ( saved._themeAccentR >= 0.0f && saved._themeAccentG >= 0.0f && saved._themeAccentB >= 0.0f )
            overrideAccent( Color{ saved._themeAccentR, saved._themeAccentG, saved._themeAccentB, 1.0f }, outTheme );

        if ( saved._themeWindowRounding >= 0.0f )
        {
            EditorThemeConfigInternal::setWindowRadius( saved._themeWindowRounding, outTheme );
            outTheme._bRadiusOverridden = SW_TRUE;
        }
        if ( saved._themeFrameRounding >= 0.0f )
        {
            EditorThemeConfigInternal::setFrameRadius( saved._themeFrameRounding, outTheme );
            outTheme._bRadiusOverridden = SW_TRUE;
        }
        if ( saved._themeTabRounding >= 0.0f )
        {
            outTheme._mapMetric[hashed_string( EditorThemeConfigInternal::kRadiusTabKey )] = saved._themeTabRounding;
            outTheme._bRadiusOverridden                                                    = SW_TRUE;
        }
        return true;
    }

    void EditorThemeConfig::overrideAccent( const Color& accent, EditorThemeConfig& inoutTheme )
    {
        inoutTheme._mapColor[hashed_string( EditorThemeConfigInternal::kAccentKey )] = accent;
        inoutTheme._bAccentOverridden                                                = SW_TRUE;
    }

    void EditorThemeConfig::overrideRadius( float32 window, float32 frame, float32 tab, EditorThemeConfig& inoutTheme )
    {
        EditorThemeConfigInternal::setWindowRadius( MathUtil::max( window, 0.0f ), inoutTheme );
        EditorThemeConfigInternal::setFrameRadius( MathUtil::max( frame, 0.0f ), inoutTheme );
        inoutTheme._mapMetric[hashed_string( EditorThemeConfigInternal::kRadiusTabKey )] = MathUtil::max( tab, 0.0f );
        inoutTheme._bRadiusOverridden                                                    = SW_TRUE;
    }

    void EditorThemeConfig::writeSaved( const EditorThemeConfig& theme, EditorAppearanceConfig& outSaved )
    {
        outSaved._themeName = theme._name;

        // 덮어쓰면 강조색 키는 늘 표에 있다(`overrideAccent`).
        const Color* pAccent   = theme._bAccentOverridden == SW_TRUE ? theme.findColor( hashed_string( EditorThemeConfigInternal::kAccentKey ) ) : nullptr;
        outSaved._themeAccentR = pAccent != nullptr ? pAccent->_r : EditorAppearanceConfig::kNotOverridden;
        outSaved._themeAccentG = pAccent != nullptr ? pAccent->_g : EditorAppearanceConfig::kNotOverridden;
        outSaved._themeAccentB = pAccent != nullptr ? pAccent->_b : EditorAppearanceConfig::kNotOverridden;

        outSaved._themeWindowRounding = EditorThemeConfigInternal::readSavedRadius( theme, hashed_string( EditorThemeConfigInternal::kRadiusWindowKey ) );
        outSaved._themeFrameRounding  = EditorThemeConfigInternal::readSavedRadius( theme, hashed_string( EditorThemeConfigInternal::kRadiusFrameKey ) );
        outSaved._themeTabRounding    = EditorThemeConfigInternal::readSavedRadius( theme, hashed_string( EditorThemeConfigInternal::kRadiusTabKey ) );
    }
} // namespace sw::editor
