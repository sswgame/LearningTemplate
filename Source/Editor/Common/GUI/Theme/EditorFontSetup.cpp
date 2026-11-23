#include "pch.h"

#include "Editor/Common/GUI/Theme/EditorFontSetup.h"

#include "Editor/Common/Config/EditorAppearanceConfig.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/Config/EditorToolDefaults.h"

#include <imgui.h>
#include <imgui_freetype.h>

namespace sw::editor
{
    namespace
    {
        struct EditorFontSetupInternal
        {
            /**
             * @brief 글꼴 크기와 아이콘 비율을 설정한 테마 토큰(`Typography.*`)에서 읽습니다. 테마 서브시스템보다 먼저 불리므로 파일을 직접 읽는다.
             * @details 아이콘은 24 격자의 가운데 20 칸에 그려져 있어 em 의 5/6 을 차지합니다. 비율 0.8 이면 아이콘 높이가 본문 크기의 2/3 쯤입니다.
             * @return 토큰을 읽었으면 true. 못 읽으면 이유를 오류로 남기고 false 다(다른 벌이나 ImGui 기본 크기로 대신하지 않는다).
             */
            [[nodiscard]] static bool readTypography( float32& outFontSize, float32& outIconScale )
            {
                const string&                     themeName = getPreferences<EditorAppearanceConfig>()._themeName;
                const expected<EditorThemeConfig> loaded    = EditorThemeTokens::loadTheme( themeName );
                if ( loaded.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "EditorFontSetup.EditorFontSizeNeeds", "Editor font size needs the theme '%#' tokens: %# (%#)", themeName.c_str(), loaded.error().message(), loaded.error().detail() );
                    return false;
                }
                const EditorThemeConfig& theme = *loaded;
                // 테마 서브시스템보다 먼저라 `SW_THEME_METRIC` 이 아니라 읽은 벌에서 찾는다. 두 키는 해석이 양수로 확인했다(`EditorThemeTokens::resolve`).
                outFontSize  = *theme.findMetric( hashed_string( "Typography.FontSize" ) );
                outIconScale = *theme.findMetric( hashed_string( "Typography.IconScale" ) );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool EditorFontSetup::apply( const EditorToolDefaults& toolDefaults, const EditorIconGlyphTable& iconTable )
    {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();

        // FreeType 폰트 로더를 연결한다(앤티에일리어싱 · 힌팅 품질)
        io.Fonts->SetFontLoader( ImGuiFreeType::GetFontLoader() );
        io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;

        const EditorToolDefaults& data = toolDefaults;
        float32                   fontSize{ 0.0f };
        float32                   iconScale{ 0.0f };
        if ( EditorFontSetupInternal::readTypography( fontSize, iconScale ) == false )
            return false;
        // 후보가 데이터다. 하나도 없으면 ImGui 기본 글꼴로 대신하지 않고 멈춘다 — 대신하면 한글 · 아이콘이 빈 상자로 그려져도 원인이 로그 한 줄에 묻힌다.
        const string basePath   = findFontFile( data._listBaseFont );
        const string monoPath   = findFontFile( data._listMonoFont );
        const string koreanPath = findFontFile( data._listKoreanFont );
        const string kanaPath   = findFontFile( data._listJapaneseFont );
        const string iconPath   = findFontFile( vector<string>{ EditorPaths::kIconFontFileName } );
        if ( basePath.empty() )
            SW_LOG_ERROR_ID( "EditorFontSetup.NoBaseFontEditortooldefaults", "No base font from editortooldefaults.json _listBaseFont exists under editor/fonts or the OS font folders" );
        if ( monoPath.empty() )
            SW_LOG_ERROR_ID( "EditorFontSetup.NoMonospaceFontEditortooldefaults", "No monospace font from editortooldefaults.json _listMonoFont exists under editor/fonts or the OS font folders" );
        if ( koreanPath.empty() )
            SW_LOG_ERROR_ID( "EditorFontSetup.NoKoreanFontEditortooldefaults", "No Korean font from editortooldefaults.json _listKoreanFont exists under editor/fonts or the OS font folders" );
        if ( kanaPath.empty() )
            SW_LOG_ERROR_ID( "EditorFontSetup.NoJapaneseFontEditortooldefaults", "No Japanese font from editortooldefaults.json _listJapaneseFont exists under editor/fonts or the OS font folders" );
        if ( iconPath.empty() )
            SW_LOG_ERROR_ID( "EditorFontSetup.EditorIconFontNot", "Editor icon font '%#' not found under editor/fonts", EditorPaths::kIconFontFileName );
        const bool bFontMissing = basePath.empty() || monoPath.empty() || koreanPath.empty() || kanaPath.empty() || iconPath.empty();
        if ( bFontMissing )
            return false;
        ImGui::GetIO().UserData = this; // 이 컨텍스트의 글꼴 구성 — 패널의 고정폭 글꼴 · 테마의 크기 바꾸기가 찾는다
        if ( readEmRatio( basePath, _baseEmRatio ) == false || readEmRatio( monoPath, _monoEmRatio ) == false )
            return false;
        const float32 baseSize = fontSize * _baseEmRatio;
        const float32 monoSize = fontSize * _monoEmRatio;

        ImFont* pBaseFont{ nullptr };
        BLOCK( "Base Font" )
        {
            ImFontConfig baseConfig{};
            baseConfig.OversampleH = 1;
            baseConfig.OversampleV = 1;
            // 명시적 크기가 필요하다. MergeMode 는 명시 크기를 쓴다.
            baseConfig.SizePixels = baseSize;
            pBaseFont             = io.Fonts->AddFontFromFileTTF( basePath.c_str(), baseSize, &baseConfig, io.Fonts->GetGlyphRangesDefault() );
            if ( pBaseFont == nullptr )
            {
                SW_LOG_ERROR_ID( "EditorFontSetup.BaseFontCouldNot", "Base font %# could not be loaded", basePath.c_str() );
                return false;
            }
            SW_LOG_TRACE_ID( "EditorFontSetup.LoadedBaseFont", "Loaded base font: %#", basePath.c_str() );
        }

        BLOCK( "Korean Glyph Merge" )
        {
            ImFontConfig mergeConfig{};
            mergeConfig.MergeMode   = true;
            mergeConfig.OversampleH = 2;
            mergeConfig.OversampleV = 1;
            mergeConfig.PixelSnapH  = true;
            io.Fonts->AddFontFromFileTTF( koreanPath.c_str(), baseSize, &mergeConfig, io.Fonts->GetGlyphRangesKorean() );
            io.Fonts->AddFontFromFileTTF( kanaPath.c_str(), baseSize, &mergeConfig, io.Fonts->GetGlyphRangesJapanese() ); // 한글 글꼴에 없는 글자만 이쪽에서 온다(먼저 병합한 쪽이 이긴다)
            SW_LOG_TRACE_ID( "EditorFontSetup.MergedKoreanGlyphs", "Merged Korean glyphs from: %# (Japanese fallback %#)", koreanPath.c_str(), kanaPath.c_str() );
        }

        auto mergeIcons = [&]( float32 size )
        {
            // ImWchar 가 16 비트일 때만 헤더의 범위 배열을 그대로 넘길 수 있다(IMGUI_USE_WCHAR32 를 켜면 이 단언이 알려 준다).
            static_assert( sizeof( ImWchar ) == sizeof( uint16 ), "editor icon glyph range must match ImWchar" ); // 범위 배열은 표가 들고 아틀라스보다 오래 산다
            const float32 iconFontSize = size * iconScale;
            ImFontConfig  iconConfig{};
            iconConfig.MergeMode        = true;
            iconConfig.PixelSnapH       = true;
            iconConfig.GlyphMinAdvanceX = iconFontSize;
            io.Fonts->AddFontFromFileTTF( iconPath.c_str(), iconFontSize, &iconConfig, reinterpret_cast<const ImWchar*>( iconTable.getGlyphRanges().data() ) );
        };
        mergeIcons( baseSize );

        BLOCK( "Monospace font" )
        {
            // 로그 줄에도 한글 · 아이콘이 있다 — 본문과 같은 병합을 고정폭 글꼴에 한 번 더 한다.
            ImFontConfig monoConfig{};
            monoConfig.SizePixels = monoSize;
            ImFont* pMonoFont     = io.Fonts->AddFontFromFileTTF( monoPath.c_str(), monoSize, &monoConfig, io.Fonts->GetGlyphRangesDefault() );
            if ( pMonoFont == nullptr )
            {
                SW_LOG_ERROR_ID( "EditorFontSetup.MonospaceFontCouldNot", "Monospace font %# could not be loaded", monoPath.c_str() );
                return false;
            }
            ImFontConfig mergeConfig{};
            mergeConfig.MergeMode  = true;
            mergeConfig.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF( koreanPath.c_str(), monoSize, &mergeConfig, io.Fonts->GetGlyphRangesKorean() );
            io.Fonts->AddFontFromFileTTF( kanaPath.c_str(), monoSize, &mergeConfig, io.Fonts->GetGlyphRangesJapanese() );
            mergeIcons( monoSize );
            _monoFontIndex = static_cast<uint32>( io.Fonts->Fonts.index_from_ptr( io.Fonts->Fonts.find( pMonoFont ) ) ); // 병합 글꼴은 이 글꼴에 더해진다 — 번호는 그대로다
        }

        io.FontDefault = pBaseFont;
        return true;
    }

    EditorFontSetup::EditorFontSetup()
        : _monoFontIndex{ 0 }
        , _baseEmRatio{ 0.0f }
        , _monoEmRatio{ 0.0f }
    {
    }

    EditorFontSetup::~EditorFontSetup()
    {
        if ( ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().UserData == this )
            ImGui::GetIO().UserData = nullptr;
    }

    const EditorFontSetup& EditorFontSetup::getActive()
    {
        const EditorFontSetup* const pActive = static_cast<const EditorFontSetup*>( ImGui::GetIO().UserData );
        SW_ASSERT( pActive != nullptr ); // EditorFontSetup::apply 가 먼저 돈다(에디터 기동이 글꼴 → 테마 순)
        return *pActive;
    }

    float32 EditorFontSetup::toBaseFontSize( float32 emSize )
    {
        const float32 ratio = getActive()._baseEmRatio;
        SW_ASSERT( ratio > 0.0f ); // EditorFontSetup::apply 가 먼저 돈다(에디터 기동이 글꼴 → 테마 순)
        return emSize * ratio;
    }

    void EditorFontSetup::pushMonoFont()
    {
        const EditorFontSetup& fontState = getActive();
        SW_ASSERT( fontState._monoEmRatio > 0.0f ); // EditorFontSetup::apply 가 먼저 돈다
        // 지금 크기(본문 글꼴 단위)를 em 으로 되돌려 고정폭 글꼴 단위로 다시 곱한다 — 두 글꼴의 em 이 같아진다.
        ImGui::PushFont( ImGui::GetIO().Fonts->Fonts[fontState._monoFontIndex], ImGui::GetStyle().FontSizeBase / fontState._baseEmRatio * fontState._monoEmRatio );
    }

    void EditorFontSetup::popMonoFont()
    {
        ImGui::PopFont();
    }
} // namespace sw::editor
