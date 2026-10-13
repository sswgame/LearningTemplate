/**
 * @file EditorThemeConfig.h
 * @brief 에디터 테마 값(프리셋 · 색 · 라운딩)입니다. 적용과 조회는 `EditorThemeUtil`(GUI)이 하고, 값은 에디터 서브시스템 `EditorTheme` 이 듭니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/EditorColor.h"
#include "Editor/Common/EditorSubsystem.h"

namespace sw::editor
{
    /** @brief 에디터 테마 프리셋 */
    enum class EditorThemePreset : uint8
    {
        ModernDark = 0, ///< UE5/Rider 스타일의 모던 딥 다크 (기본)
        DeepCharcoal,   ///< 차콜/무광 블랙 미니멀 테마
        MidnightBlue,   ///< 미드나잇 블루 하이테크 테마
        ClassicDark,    ///< ImGui 기본 클래식 다크 테마
        Count           ///< 프리셋 표(EditorThemeUtil.cpp)와 개수를 맞추는 데 쓰입니다
    };

    /** @brief Look & Feel 테마 파라미터 구조체 */
    struct EditorThemeConfig
    {
        EditorThemePreset _preset{ EditorThemePreset::ModernDark };
        Color             _accentColor{ 0.27f, 0.57f, 1.0f, 1.0f }; ///< 주 액센트 컬러 (Electric Blue)
        Color             _windowBg{ 0.10f, 0.11f, 0.14f, 1.0f };   ///< 메인 윈도우 배경색
        Color             _panelBg{ 0.13f, 0.15f, 0.19f, 1.0f };    ///< 자식 패널/캔버스 배경색
        Color             _headerBg{ 0.18f, 0.21f, 0.28f, 1.0f };   ///< 헤더/타이틀바 배경색
        Color             _frameBg{ 0.15f, 0.17f, 0.22f, 1.0f };    ///< 입력 필드 프레임 배경색
        Color             _border{ 0.22f, 0.26f, 0.33f, 1.0f };     ///< 테두리 색

        Color _successColor{ 0.20f, 0.75f, 0.35f, 1.0f }; ///< 성공/정상 상태 (Green)
        Color _warningColor{ 0.95f, 0.70f, 0.15f, 1.0f }; ///< 경고 상태 (Amber)
        Color _errorColor{ 0.95f, 0.30f, 0.25f, 1.0f };   ///< 에러 상태 (Red)
        Color _infoColor{ 0.30f, 0.70f, 0.95f, 1.0f };    ///< 정보 상태 (Cyan)
        Color _textMuted{ 0.55f, 0.60f, 0.68f, 1.0f };    ///< 비활성/보조 텍스트

        float32 _windowRounding{ 4.0f };
        float32 _frameRounding{ 3.0f };
        float32 _popupRounding{ 4.0f };
        float32 _tabRounding{ 4.0f };
        float32 _scrollbarRounding{ 6.0f };
        float32 _grabRounding{ 3.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorTheme
     * @brief 지금 적용된 테마 값을 드는 에디터 서브시스템입니다. 조회 · 교체는 `EditorThemeUtil` 이 `editor::getService<EditorTheme>()` 로 합니다.
     */
    class EditorTheme final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorTheme";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        EditorThemeConfig&       getConfig() { return _config; }
        const EditorThemeConfig& getConfig() const { return _config; }

    private:
        EditorThemeConfig _config{};
    };
} // namespace sw::editor
