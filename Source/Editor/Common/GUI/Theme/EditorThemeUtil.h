/**
 * @file EditorThemeUtil.h
 * @brief 에디터의 Look & Feel 테마 설정과 실시간 스타일 적용을 담당하는 유틸리티입니다.
 */
#pragma once
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"

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
    class EditorAssetType;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorIconLabel
     * @brief "아이콘 + 공백 두 개 + 라벨" 문자열 값입니다(`EditorThemeUtil::makeIconLabel` 이 만듭니다). 자기 버퍼를 들고 `const utf8*` 로 바뀝니다.
     * @details 호출마다 따로 만들어지는 값이라 한 호출에 두 개를 넘겨도 서로 덮어쓰지 않고, 호출 스레드마다 정적 버퍼를 둘 필요가 없다.
     *          변환한 포인터는 이 값이 사라질 때(그 식이 끝날 때)까지만 유효하다 - ImGui 호출 인자로 바로 넘기고 변수에 담지 않는다.
     */
    class SW_EDITOR_API EditorIconLabel
    {
    public:
        EditorIconLabel( const utf8* pIcon, const utf8* pLabel );

        operator const utf8*() const { return _arrText; }

    private:
        utf8 _arrText[constant::kMaxBuffer256];
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorThemeUtil
     * @brief 에디터 전체의 Look & Feel(색상 팔레트, 라운딩, 여백, 경계선)을 일관되게 제어하는 유틸리티입니다.
     */
    class SW_EDITOR_API EditorThemeUtil
    {
    public:
        /** @brief 현재 적용된 테마 설정을 반환합니다. */
        static const EditorThemeConfig& getActiveTheme();

        /** @brief 주어진 테마 설정을 ImGui 스타일에 한 번에 적용합니다. */
        static void applyTheme( const EditorThemeConfig& config );

        /**
         * @brief 모니터 DPI 배율을 정하고 현재 테마를 그 배율로 다시 적용합니다. 여백, 둥글기, 글자가 바뀝니다.
         * @details 테마는 96 DPI 기준의 절대 크기를 기록하므로 배율은 테마를 적용할 때마다 곱한다. 글자는 `style.FontScaleDpi` 로 키운다.
         *          ImGui 1.92 의 동적 폰트이므로 흐려지지 않는다. 모니터를 옮길 때의 글자는 `io.ConfigDpiScaleFonts` 가 따라간다.
         */
        static void setDPIScale( float32 dpiScale );
        /** @brief 현재 DPI 배율을 반환합니다. 96 DPI 가 1 입니다. */
        static float32 getDPIScale();

        /** @brief 토큰 벌(`Content/editor/theme/<이름>.tokens.json`, 대소문자 무시)을 적용합니다. 덮어쓴 강조색 · 둥글기는 버립니다. 없는 이름이면 false 입니다. */
        [[nodiscard]] static bool applyThemeByName( string_view name );
        /** @brief 읽어 둔 토큰 벌의 이름을 씁니다(이름순). */
        static void listThemes( vector<string>& outListName );
        /** @brief 토큰 파일을 모두 다시 읽고 지금 테마를 다시 적용합니다(핫 리로드). 사용자가 덮어쓴 값은 설정에서 다시 얹습니다. */
        static void reloadThemes();

        /** @brief EditorConfig(JSON)에 저장된 테마 설정을 읽어 적용합니다. 그 이름의 토큰 벌이 없으면 오류를 남기고 false 이며, 다른 벌로 대신하지 않습니다. */
        [[nodiscard]] static bool loadFromConfig();

        /** @brief 현재 테마 설정을 EditorConfig(JSON)에 저장합니다. */
        static void saveToConfig();

        /** @brief 테마를 실시간으로 조정하는 모달 대화상자를 그립니다. */
        static void drawThemeSettingsDialog( bool* pOpen );

        /** @brief 액센트 색을 바꾸고 테마를 다시 적용합니다. */
        static void setAccentColor( const Color& accentColor );

        // ----------------------------------------------------------------------
        // 1) 테마 색상 접근자(Color)
        // ----------------------------------------------------------------------
        static const Color& getAccentColor();
        static const Color& getTextColor();
        /** @brief 씬 뷰 호버 강조 색입니다. 강조색을 밝고 반투명하게 만든 색이며 선택 주황보다 약하고 테마를 따릅니다. */
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
        /** @brief 뷰포트 위 오버레이(검색 팝업 · 알림) 바탕입니다. 알파는 쓰는 곳이 정합니다. */
        static const Color& getOverlayBgColor();
        /** @brief 씬 뷰 선택 외곽선입니다. 강조색(파랑 계열)은 격자 · 카메라 프러스텀과 섞여 쓰지 않습니다. */
        static const Color& getSelectionOutlineColor();
        /** @brief 2D 미리보기 · 그래프에서 고른 셀 · 지금 프레임을 짚는 색입니다. */
        static const Color& getHighlightColor();
        /** @brief 꺼진 토글 단추 색입니다(켜진 것은 강조색). */
        static const Color& getToggleInactiveColor();
        static const Color& getAxisXColor();
        static const Color& getAxisYColor();
        static const Color& getAxisZColor();
        /** @brief 씬 뷰 2D 콜라이더 와이어프레임 색입니다. */
        static const Color& getColliderWireColor();
        /** @brief 씬 뷰 카메라 프러스텀 색입니다. */
        static const Color& getCameraWireColor();
        /** @brief 씬 뷰 위에 겹쳐 그리는 시각화 레이블 색입니다. */
        static const Color& getViewportLabelColor();

        // ----------------------------------------------------------------------
        // 2) UI 텍스트와 스타일 헬퍼
        // ----------------------------------------------------------------------
        static void textAccent( const utf8* pText );
        static void textSuccess( const utf8* pText );
        static void textWarning( const utf8* pText );
        static void textError( const utf8* pText );
        static void textMuted( const utf8* pText );
        static void textInfo( const utf8* pText );

        /**
         * @brief 다음에 그릴 텍스트에 테마 색을 적용합니다. `popTextColor()` 와 짝입니다.
         * @details `textSuccess` 류는 서식이 없는 문자열만 받는다. `"Frame #%u"` 처럼 서식이 필요한 곳은 이 짝으로 감싸고
         *          평소처럼 `ImGui::Text` 를 호출한다. 색을 정하는 곳은 여전히 테마 하나다.
         */
        static void pushTextColor( const Color& color );
        /** @brief pushTextColor() 와 짝입니다. */
        static void popTextColor();

        static void pushAccentButton( float32 alpha = 1.0f );
        static void popAccentButton();

        static void pushAccentHeader( float32 alpha = 1.0f );
        static void popAccentHeader();

        // ----------------------------------------------------------------------
        // 3) 리소스와 애셋 아이콘, 테마 색상 헬퍼
        // ----------------------------------------------------------------------
        /** @brief 폴더용 테마 색을 반환합니다. */
        static Color getFolderColor();

        /** @brief 열림 · 닫힘 상태에 맞는 폴더 아이콘 문자열을 반환합니다(editoricon::kFolderOpen / kFolder). */
        static const utf8* getFolderIcon( bool bOpened = false );

        /** @brief 종류의 에디터 아이콘을 반환합니다. 애셋 종류 등록을 따릅니다. 알 수 없음이면 일반 파일 아이콘입니다. */
        static const utf8* getAssetTypeIcon( const EditorAssetType& kind );
        /** @brief 종류의 색을 반환합니다. 애셋 종류 등록을 따르고, 액센트 종류는 테마 액센트를 씁니다. 알 수 없음이면 흐린 글자색입니다. */
        static Color getAssetTypeColor( const EditorAssetType& kind );

        /** @brief 경로(확장자)에 맞는 애셋 아이콘을 반환합니다. */
        static const utf8* getAssetIconForPath( string_view path, bool bIsDirectory = false );

        /** @brief 경로(확장자)에 맞는 테마 색을 반환합니다. */
        static Color getAssetColorForPath( string_view path, bool bIsDirectory = false );

        /**
         * @brief "아이콘 + 공백 두 개 + 라벨" 문자열을 만듭니다. ImGui 라벨 앞에 아이콘을 붙일 때 씁니다.
         * @details 반환값은 자기 버퍼를 든 값이다(`EditorIconLabel`). ImGui 호출 인자로 바로 넘긴다. 한 호출에 두 번 넘겨도 된다.
         */
        static EditorIconLabel makeIconLabel( const utf8* pIcon, const utf8* pLabel );

        /**
         * @brief 아이콘 글리프 하나를 사각형 가운데에 그립니다. 글리프 높이는 사각형 짧은 변의 @p sizeRatio 배입니다.
         * @details 콘텐츠 브라우저의 종류 아이콘 썸네일이 쓴다. 글리프 폭과 높이는 그 글꼴 크기로 측정한다. 가운데는 글자 기준선이 아니라 글리프 상자의 가운데다.
         */
        static void drawCenteredGlyph( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos, const utf8* pGlyph, const Color& color, float32 sizeRatio );
    };
} // namespace sw::editor
