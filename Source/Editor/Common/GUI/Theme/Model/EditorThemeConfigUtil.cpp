#include "pch.h"

#include "Editor/Common/GUI/Theme/Model/EditorThemeConfigUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Config/EditorConfig.h"

namespace sw::editor
{
    namespace
    {
        struct EditorThemeConfigUtilInternal
        {
            /** @brief 사용자가 덮어쓰는 토큰 키입니다. 나머지 둥글기는 슬라이더 셋에서 정한다. */
            static inline const hashed_string kAccentKey{ "Base.Accent" };
            static inline const hashed_string kRadiusWindowKey{ "Radius.Window" };
            static inline const hashed_string kRadiusFrameKey{ "Radius.Frame" };
            static inline const hashed_string kRadiusTabKey{ "Radius.Tab" };
            /** @brief 둥글기를 한 슬라이더로 묶을 때 스크롤바 둥글기는 입력 필드 둥글기의 이 배수입니다. */
            static constexpr float32 kScrollbarRadiusPerFrameRadius = 2.0f;

            static void setWindowRadius( float32 radius, EditorThemeConfig& inoutTheme )
            {
                inoutTheme._mapMetric[kRadiusWindowKey]                = radius;
                inoutTheme._mapMetric[hashed_string( "Radius.Popup" )] = radius;
            }

            static void setFrameRadius( float32 radius, EditorThemeConfig& inoutTheme )
            {
                inoutTheme._mapMetric[kRadiusFrameKey]                     = radius;
                inoutTheme._mapMetric[hashed_string( "Radius.Scrollbar" )] = radius * kScrollbarRadiusPerFrameRadius;
                inoutTheme._mapMetric[hashed_string( "Radius.Grab" )]      = radius;
            }

            /** @brief 덮어쓴 둥글기의 저장 값입니다. 덮어쓰면 슬라이더 키는 늘 표에 있다(`overrideRadius`). */
            static float32 readSavedRadius( const EditorThemeConfig& theme, const hashed_string& key )
            {
                const float32* pValue = theme._bRadiusOverridden == SW_TRUE ? theme.findMetric( key ) : nullptr;
                return pValue != nullptr ? *pValue : EditorConfig::kNotOverridden;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    const EditorThemeConfig* EditorThemeConfigUtil::findByName( const vector<EditorThemeConfig>& listTheme, string_view name )
    {
        for ( const EditorThemeConfig& theme : listTheme )
        {
            if ( StringUtil::equals( theme._name, name, true ) )
                return &theme;
        }
        return nullptr;
    }

    bool EditorThemeConfigUtil::buildFromSaved( const vector<EditorThemeConfig>& listTheme, const EditorConfig& saved, EditorThemeConfig& outTheme )
    {
        const EditorThemeConfig* pTheme = findByName( listTheme, saved._themeName );
        if ( pTheme == nullptr )
            return false;
        outTheme = *pTheme;

        if ( saved._themeAccentR >= 0.0f && saved._themeAccentG >= 0.0f && saved._themeAccentB >= 0.0f )
            overrideAccent( Color{ saved._themeAccentR, saved._themeAccentG, saved._themeAccentB, 1.0f }, outTheme );

        if ( saved._themeWindowRounding >= 0.0f )
        {
            EditorThemeConfigUtilInternal::setWindowRadius( saved._themeWindowRounding, outTheme );
            outTheme._bRadiusOverridden = SW_TRUE;
        }
        if ( saved._themeFrameRounding >= 0.0f )
        {
            EditorThemeConfigUtilInternal::setFrameRadius( saved._themeFrameRounding, outTheme );
            outTheme._bRadiusOverridden = SW_TRUE;
        }
        if ( saved._themeTabRounding >= 0.0f )
        {
            outTheme._mapMetric[EditorThemeConfigUtilInternal::kRadiusTabKey] = saved._themeTabRounding;
            outTheme._bRadiusOverridden                                       = SW_TRUE;
        }
        return true;
    }

    void EditorThemeConfigUtil::overrideAccent( const Color& accent, EditorThemeConfig& inoutTheme )
    {
        inoutTheme._mapColor[EditorThemeConfigUtilInternal::kAccentKey] = accent;
        inoutTheme._bAccentOverridden                                   = SW_TRUE;
    }

    void EditorThemeConfigUtil::overrideRadius( float32 window, float32 frame, float32 tab, EditorThemeConfig& inoutTheme )
    {
        EditorThemeConfigUtilInternal::setWindowRadius( MathUtil::max( window, 0.0f ), inoutTheme );
        EditorThemeConfigUtilInternal::setFrameRadius( MathUtil::max( frame, 0.0f ), inoutTheme );
        inoutTheme._mapMetric[EditorThemeConfigUtilInternal::kRadiusTabKey] = MathUtil::max( tab, 0.0f );
        inoutTheme._bRadiusOverridden                                       = SW_TRUE;
    }

    void EditorThemeConfigUtil::writeSaved( const EditorThemeConfig& theme, EditorConfig& outSaved )
    {
        outSaved._themeName = theme._name;

        // 덮어쓰면 강조색 키는 늘 표에 있다(`overrideAccent`).
        const Color* pAccent   = theme._bAccentOverridden == SW_TRUE ? theme.findColor( EditorThemeConfigUtilInternal::kAccentKey ) : nullptr;
        outSaved._themeAccentR = pAccent != nullptr ? pAccent->_r : EditorConfig::kNotOverridden;
        outSaved._themeAccentG = pAccent != nullptr ? pAccent->_g : EditorConfig::kNotOverridden;
        outSaved._themeAccentB = pAccent != nullptr ? pAccent->_b : EditorConfig::kNotOverridden;

        outSaved._themeWindowRounding = EditorThemeConfigUtilInternal::readSavedRadius( theme, EditorThemeConfigUtilInternal::kRadiusWindowKey );
        outSaved._themeFrameRounding  = EditorThemeConfigUtilInternal::readSavedRadius( theme, EditorThemeConfigUtilInternal::kRadiusFrameKey );
        outSaved._themeTabRounding    = EditorThemeConfigUtilInternal::readSavedRadius( theme, EditorThemeConfigUtilInternal::kRadiusTabKey );
    }
} // namespace sw::editor
