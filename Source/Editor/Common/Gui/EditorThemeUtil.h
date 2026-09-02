/**
 * @file EditorThemeUtil.h
 * @brief 에디터 Look & Feel 테마 설정 및 실시간 스타일링 유틸리티
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/Widgets/EditorWidgets.h"

namespace sw::editor
{
    /** @brief 에디터 테마 프리셋 */
    enum class EditorThemePreset : uint8
    {
        ModernDark = 0, ///< UE5/Rider 스타일의 모던 딥 다크 (기본)
        DeepCharcoal,   ///< 차콜/무광 블랙 미니멀 테마
        MidnightBlue,   ///< 미드나잇 블루 하이테크 테마
        ClassicDark,    ///< ImGui 기본 클래식 다크 테마
    };

    /** @brief Look & Feel 테마 파라미터 구조체 */
    struct EditorThemeConfig
    {
        EditorThemePreset _preset{ EditorThemePreset::ModernDark };
        Color4            _accentColor{ 0.27f, 0.57f, 1.0f, 1.0f }; ///< 주 액센트 컬러 (Electric Blue)
        Color4            _windowBg{ 0.10f, 0.11f, 0.14f, 1.0f };   ///< 메인 윈도우 배경색
        Color4            _panelBg{ 0.13f, 0.15f, 0.19f, 1.0f };    ///< 자식 패널/캔버스 배경색
        Color4            _headerBg{ 0.18f, 0.21f, 0.28f, 1.0f };   ///< 헤더/타이틀바 배경색
        Color4            _frameBg{ 0.15f, 0.17f, 0.22f, 1.0f };    ///< 입력 필드 프레임 배경색
        Color4            _border{ 0.22f, 0.26f, 0.33f, 1.0f };     ///< 경계선 테두리 색

        Color4 _successColor{ 0.20f, 0.75f, 0.35f, 1.0f }; ///< 성공/정상 상태 (Green)
        Color4 _warningColor{ 0.95f, 0.70f, 0.15f, 1.0f }; ///< 경고 상태 (Amber)
        Color4 _errorColor{ 0.95f, 0.30f, 0.25f, 1.0f };   ///< 에러 상태 (Red)
        Color4 _infoColor{ 0.30f, 0.70f, 0.95f, 1.0f };    ///< 정보 상태 (Cyan)
        Color4 _textMuted{ 0.55f, 0.60f, 0.68f, 1.0f };    ///< 비활성/보조 텍스트

        float32 _windowRounding{ 4.0f };
        float32 _frameRounding{ 3.0f };
        float32 _popupRounding{ 4.0f };
        float32 _tabRounding{ 4.0f };
        float32 _scrollbarRounding{ 6.0f };
        float32 _grabRounding{ 3.0f };
    };

    /**
     * @class EditorThemeUtil
     * @brief 에디터 전체의 Look & Feel(색상 팔레트, 라운딩, 여백, 경계선)을 일관되게 제어하는 유틸리티
     */
    class EditorThemeUtil
    {
    public:
        /** @brief 현재 활성화된 테마 설정 반환 */
        static const EditorThemeConfig& getActiveTheme();

        /** @brief 지정한 테마 설정을 ImGui 스타일에 일괄 적용 */
        static void applyTheme( const EditorThemeConfig& config );

        /** @brief 프리셋 기반으로 테마 즉시 적용 */
        static void applyPreset( EditorThemePreset preset );

        /** @brief EditorConfig(JSON)에서 저장된 테마 설정을 읽어 적용 */
        static void loadFromConfig();

        /** @brief 현재 활성 테마 설정을 EditorConfig(JSON)에 저장 */
        static void saveToConfig();

        /** @brief 테마 설정 실시간 튜닝 모달 다이얼로그 렌더링 */
        static void drawThemeSettingsDialog( bool* pOpen );

        /** @brief 액센트 컬러를 지정하여 테마 재적용 */
        static void setAccentColor( const Color4& accentColor );

        // ----------------------------------------------------------------------
        // 1) 테마 색상 접근자 (Color4)
        // ----------------------------------------------------------------------
        static const Color4& getAccentColor();
        static const Color4& getWindowBgColor();
        static const Color4& getPanelBgColor();
        static const Color4& getHeaderBgColor();
        static const Color4& getFrameBgColor();
        static const Color4& getBorderColor();

        static const Color4& getSuccessColor();
        static const Color4& getWarningColor();
        static const Color4& getErrorColor();
        static const Color4& getInfoColor();
        static const Color4& getTextMutedColor();

        // ----------------------------------------------------------------------
        // 2) UI 텍스트 & 스타일 헬퍼
        // ----------------------------------------------------------------------
        static void textAccent( const utf8* pText );
        static void textSuccess( const utf8* pText );
        static void textWarning( const utf8* pText );
        static void textError( const utf8* pText );
        static void textMuted( const utf8* pText );

        static void pushAccentButton( float32 alpha = 1.0f );
        static void popAccentButton();

        static void pushAccentHeader( float32 alpha = 1.0f );
        static void popAccentHeader();

        // ----------------------------------------------------------------------
        // 3) 리소스 & 애셋 아이콘 및 테마 색상 헬퍼
        // ----------------------------------------------------------------------
        /** @brief 폴더/디렉터리용 테마 색상 반환 */
        static Color4 getFolderColor();

        /** @brief 열림/닫힘 상태에 따른 폴더 아이콘 문자열 반환 (ICON_FA_FOLDER_OPEN / ICON_FA_FOLDER) */
        static const utf8* getFolderIcon( bool bOpened = false );

        /** @brief 확장자 및 파일 경로 기반 Font Awesome 애셋 아이콘 반환 */
        static const utf8* getAssetIconForPath( string_view path, bool bIsDirectory = false );

        /** @brief 확장자 및 파일 경로 기반 테마 색상 반환 */
        static Color4 getAssetColorForPath( string_view path, bool bIsDirectory = false );
    };
} // namespace sw::editor
