/**
 * @file EditorThemeConfig.h
 * @brief 에디터 테마의 실행 값입니다. 토큰 파일(`EditorThemeTokens`)을 해석한 값이고, 적용과 조회는 `EditorThemeUtil`(GUI)이 하며, 값은 에디터 서브시스템 `EditorTheme` 이 듭니다.
 * @details 색 · 크기의 기본값을 여기 두지 않는다. 원본은 토큰 파일 하나다(두 곳에 두면 한쪽만 고쳐진다). 테마를 읽기 전의 값은 의미가 없다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Config/EditorThemeTokens.h"
#include "Editor/Common/EditorColor.h"
#include "Editor/Common/EditorSubsystem.h"

namespace sw::editor
{
    /** @brief 해석한 테마 값입니다. 색 이름은 `EditorThemeColorDef` 의 키와 같습니다(접미사 Color 는 옛 접근자 이름을 따른 것). */
    struct EditorThemeConfig
    {
        string _name{};        ///< 토큰 벌 이름(`dark`)
        string _displayName{}; ///< 대화상자에 보이는 이름

        Color _text{};
        Color _textMuted{};
        Color _accentColor{};
        Color _windowBg{};
        Color _panelBg{};
        Color _headerBg{};
        Color _frameBg{};
        Color _border{};
        Color _successColor{};
        Color _warningColor{};
        Color _errorColor{};
        Color _infoColor{};
        Color _scrollbarBg{};
        Color _dockingEmptyBg{};
        Color _tableRowBgAlt{};
        Color _overlayBg{};
        Color _selectionOutline{};
        Color _highlight{};
        Color _toggleInactive{};
        Color _axisX{};
        Color _axisY{};
        Color _axisZ{};
        Color _colliderWire{};
        Color _cameraWire{};
        Color _viewportLabel{};

        EditorThemeRadiusDef     _radius{};
        EditorThemeSpacingDef    _spacing{};
        EditorThemeStrokeDef     _stroke{};
        EditorThemeTypographyDef _typography{};
        float32                  _density{ 1.0f };

        uint8 _bAccentOverridden{ SW_FALSE }; ///< 사용자가 강조색을 바꿨다(설정에 저장한다)
        uint8 _bRadiusOverridden{ SW_FALSE }; ///< 사용자가 둥글기를 바꿨다(설정에 저장한다)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorTheme
     * @brief 지금 적용된 테마 값을 가진 에디터 서브시스템입니다. 조회 · 교체는 `EditorThemeUtil` 이 `editor::findSubsystem<EditorTheme>()` 로 합니다.
     */
    class EditorTheme final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorTheme";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        EditorThemeConfig&       getConfig() { return _config; }
        const EditorThemeConfig& getConfig() const { return _config; }
        /** @brief 폴더에서 읽어 둔 토큰 벌입니다(`EditorThemeUtil` 이 처음 쓸 때 채운다). */
        vector<EditorThemeConfig>& getAvailable() { return _listAvailable; }

        /** @brief DPI 배율입니다(`EditorThemeUtil::setDpiScale`). 테마는 96 DPI 기준 크기로 기록하고, 적용할 때 이 값을 곱한다. 1 이 기본입니다. */
        float32 getDpiScale() const { return _dpiScale; }
        void    setDpiScale( float32 dpiScale ) { _dpiScale = dpiScale; }

        /** @brief 토큰 파일이 바뀌었다고 알립니다(핫 리로드 — 에셋 테이블의 처리기가 부른다). 다음 UI 프레임이 다시 읽는다. */
        void markTokensChanged() { _bTokensChanged = SW_TRUE; }
        /**
         * @brief 핫 리로드가 바뀐 토큰 파일을 알릴 때 부릅니다(에셋 종류 테이블의 처리기). 활성 에디터의 테마에 표시를 남기고, 다음 UI 프레임이 모두 다시 읽고 적용합니다.
         * @return 늘 true 입니다(처리했으니 엔진 캐시 리로드는 하지 않는다).
         */
        [[nodiscard]] static bool importChangedTokenFile( string_view relativePath );
        /** @brief 바뀐 토큰이 있었으면 true 를 돌려주고 표시를 지웁니다. */
        bool consumeTokensChanged()
        {
            const bool bChanged = _bTokensChanged == SW_TRUE;
            _bTokensChanged     = SW_FALSE;
            return bChanged;
        }

    private:
        EditorThemeConfig         _config{};
        vector<EditorThemeConfig> _listAvailable{};
        float32                   _dpiScale{ 1.0f };
        uint8                     _bTokensChanged{ SW_FALSE };
    };
} // namespace sw::editor
