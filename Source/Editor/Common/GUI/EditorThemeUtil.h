/**
 * @file EditorThemeUtil.h
 * @brief 에디터의 Look & Feel 테마 설정과 실시간 스타일 적용 유틸리티입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/EditorColor.h"
#include "Editor/Common/EditorExports.h"

struct ImDrawList;

namespace sw
{
    struct float2;
} // namespace sw

namespace sw::editor
{
    enum class EditorAssetType : uint8;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorThemeUtil
     * @brief 에디터 전체의 Look & Feel(색상 팔레트, 라운딩, 여백, 경계선)을 일관되게 제어하는 유틸리티
     */
    class SW_EDITOR_API EditorThemeUtil
    {
    public:
        /** @brief 현재 적용된 테마 설정을 반환합니다. */
        static const EditorThemeConfig& getActiveTheme();

        /** @brief 주어진 테마 설정을 ImGui 스타일에 한 번에 적용합니다. */
        static void applyTheme( const EditorThemeConfig& config );

        /**
         * @brief 모니터 DPI 배율을 정하고 지금 테마를 그 배율로 다시 적용합니다(여백 · 둥글기 · 글자).
         * @details 테마는 96 DPI 기준의 절대 크기를 적으므로 배율은 테마를 적용할 때마다 곱합니다. 글자는 `style.FontScaleDpi` 로 키웁니다
         *          (ImGui 1.92 동적 폰트 — 흐려지지 않는다). 모니터를 옮길 때의 글자는 `io.ConfigDpiScaleFonts` 가 따라갑니다.
         */
        static void setDpiScale( float32 dpiScale );
        /** @brief 지금 DPI 배율입니다(96 DPI = 1). */
        static float32 getDpiScale();

        /** @brief 프리셋으로 테마를 바로 적용합니다. */
        static void applyPreset( EditorThemePreset preset );
        /** @brief 설정 파일에 적는 이름("ClassicDark" — 대소문자 무시)으로 프리셋을 찾습니다. 없으면 false 입니다. */
        [[nodiscard]] static bool findPresetByConfigID( string_view configID, EditorThemePreset& outPreset );

        /** @brief EditorConfig(JSON)에 저장된 테마 설정을 읽어 적용합니다. */
        static void loadFromConfig();

        /** @brief 현재 테마 설정을 EditorConfig(JSON)에 저장합니다. */
        static void saveToConfig();

        /** @brief 테마를 실시간으로 조정하는 모달 대화 상자를 그립니다. */
        static void drawThemeSettingsDialog( bool* pOpen );

        /** @brief 액센트 색을 바꾸고 테마를 다시 적용합니다. */
        static void setAccentColor( const Color& accentColor );

        // ----------------------------------------------------------------------
        // 1) 테마 색상 접근자 (Color)
        // ----------------------------------------------------------------------
        static const Color& getAccentColor();
        /** @brief 씬 뷰 호버 강조 색입니다(강조색을 밝게 · 반투명 — 선택 주황보다 약하게, 테마를 따른다). */
        static Color        getViewportHoverColor();
        static const Color& getWindowBgColor();
        static const Color& getPanelBgColor();
        static const Color& getHeaderBgColor();
        static const Color& getFrameBgColor();
        static const Color& getBorderColor();

        static const Color& getSuccessColor();
        static const Color& getWarningColor();
        static const Color& getErrorColor();
        static const Color& getInfoColor();
        static const Color& getTextMutedColor();

        // ----------------------------------------------------------------------
        // 2) UI 텍스트 & 스타일 헬퍼
        // ----------------------------------------------------------------------
        static void textAccent( const utf8* pText );
        static void textSuccess( const utf8* pText );
        static void textWarning( const utf8* pText );
        static void textError( const utf8* pText );
        static void textMuted( const utf8* pText );
        static void textInfo( const utf8* pText );

        /**
         * @brief 다음에 그릴 텍스트에 테마 색을 씌웁니다. `popTextColor()` 와 짝입니다.
         * @details `textSuccess` 류는 서식이 없는 문자열만 받습니다. `"Frame #%u"` 처럼 서식이 필요한 곳은 이 짝으로 감싸고
         *          평소처럼 `ImGui::Text` 를 부릅니다. 색을 고르는 곳은 여전히 테마 하나입니다.
         */
        static void pushTextColor( const Color& color );
        /** @brief pushTextColor() 와 짝을 이룹니다. */
        static void popTextColor();

        static void pushAccentButton( float32 alpha = 1.0f );
        static void popAccentButton();

        static void pushAccentHeader( float32 alpha = 1.0f );
        static void popAccentHeader();

        // ----------------------------------------------------------------------
        // 3) 리소스 & 애셋 아이콘 및 테마 색상 헬퍼
        // ----------------------------------------------------------------------
        /** @brief 폴더용 테마 색을 반환합니다. */
        static Color getFolderColor();

        /** @brief 열림 · 닫힘 상태에 맞는 폴더 아이콘 문자열을 반환합니다(editoricon::kFolderOpen / kFolder). */
        static const utf8* getFolderIcon( bool bOpened = false );

        /** @brief 종류의 에디터 아이콘입니다(애셋 종류 표). `Unknown` 이면 일반 파일 아이콘입니다. */
        static const utf8* getAssetTypeIcon( EditorAssetType kind );
        /** @brief 종류의 색입니다(애셋 종류 표, 액센트 종류는 테마 액센트). `Unknown` 이면 흐린 글자색입니다. */
        static Color getAssetTypeColor( EditorAssetType kind );

        /** @brief 경로(확장자)에 맞는 애셋 아이콘을 반환합니다. */
        static const utf8* getAssetIconForPath( string_view path, bool bIsDirectory = false );

        /** @brief 경로(확장자)에 맞는 테마 색을 반환합니다. */
        static Color getAssetColorForPath( string_view path, bool bIsDirectory = false );

        /**
         * @brief "아이콘 + 공백 두 칸 + 라벨" 문자열을 만듭니다. ImGui 라벨 앞에 아이콘을 붙일 때 씁니다.
         * @details 반환값은 호출한 스레드의 고정 버퍼라 다음 호출 전까지만 유효합니다. ImGui 호출 인자로 바로 넘기십시오 — 한 호출에 두 번 넘기면
         *          두 번째가 첫 번째를 덮습니다(그런 자리는 지역 fixed_string 을 씁니다).
         */
        static const utf8* makeIconLabel( const utf8* pIcon, const utf8* pLabel );

        /**
         * @brief 아이콘 글리프 하나를 사각형 가운데에 그립니다. 글리프 높이는 사각형 짧은 변의 @p sizeRatio 배입니다.
         * @details 콘텐츠 브라우저의 종류 아이콘 썸네일이 쓴다. 글리프 폭과 높이는 그 글꼴 크기로 측정한다(가운데가 글자 기준선이 아니라 글리프 상자다).
         */
        static void drawCenteredGlyph( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos, const utf8* pGlyph, const Color& color, float32 sizeRatio );
    };
} // namespace sw::editor
