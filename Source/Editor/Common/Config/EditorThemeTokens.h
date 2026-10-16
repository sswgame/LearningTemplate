/**
 * @file EditorThemeTokens.h
 * @brief 에디터 테마의 디자인 토큰입니다. 정본은 `Resource/editor/theme/<이름>.tokens.json` 이고 이 파일은 그 형식(리플렉션)과 읽기 · 해석을 맡습니다.
 * @details 토큰은 색 · 모서리 · 간격 · 테두리 · 글꼴 크기 · 아이콘 크기 · 밀도입니다. ImGui 스타일은 토큰에서만 만든다(`EditorThemeUtil::applyTheme`).
 *          패널 코드가 색이나 간격을 숫자로 적지 않고 `EditorThemeUtil` 접근자를 쓰는 것은 린트(`CheckEditorStyleLiterals`)가 새 줄에서 지킨다.
 *          언리얼의 에디터 테마 JSON(`EditorThemes` 폴더의 JSON, `FStyleColors`) · Slate 스타일셋, 유니티 UI Toolkit 의 USS 변수, Godot 의 Theme 리소스와 같은 자리다.
 *          그들과 달리 키를 리플렉션 멤버 이름으로 두어 읽기 코드가 따로 없고, Figma 변수와는 `py -3 -m Scripts editor-theme` 로 오간다(Figma 는 선택 입력).
 *
 *          색은 sRGB 16진 글자(`#RRGGBB` · `#RRGGBBAA`)다. ImGui 가 색을 그대로 출력 값으로 쓰므로 선형 변환은 하지 않는다.
 *          주의: 모르는 키 · 빠진 색은 읽기 오류다. 오타가 기본값으로 조용히 바뀌지 않게 하기 위해서다. 수 토큰은 빠지면 0 이 되므로 파일에 모두 적는다.
 *          주의: `_typography._iconScale` 은 글꼴 아틀라스를 만들 때만 읽는다(아이콘 글꼴을 본문 글꼴에 합칠 때의 크기 비율). 바꾸면 에디터를 다시 띄워야 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /** @brief 색 토큰입니다. 값은 sRGB 16진 글자입니다. */
    REFLECT()
    struct EditorThemeColorDef
    {
        REFLECT_BODY();
        PROPERTY()
        string _text{}; ///< 본문 글자
        PROPERTY()
        string _textMuted{}; ///< 흐린 글자(비활성 · 보조)
        PROPERTY()
        string _accent{}; ///< 강조(선택 · 버튼 호버 · 체크)
        PROPERTY()
        string _windowBg{}; ///< 창 바탕
        PROPERTY()
        string _panelBg{}; ///< 자식 창 · 캔버스 바탕
        PROPERTY()
        string _headerBg{}; ///< 헤더 · 활성 타이틀
        PROPERTY()
        string _frameBg{}; ///< 입력 칸 바탕
        PROPERTY()
        string _border{}; ///< 테두리
        PROPERTY()
        string _success{};
        PROPERTY()
        string _warning{};
        PROPERTY()
        string _error{};
        PROPERTY()
        string _info{};
        PROPERTY()
        string _scrollbarBg{};
        PROPERTY()
        string _dockingEmptyBg{}; ///< 빈 도킹 영역
        PROPERTY()
        string _tableRowBgAlt{}; ///< 표의 번갈아 칠하는 줄
        PROPERTY()
        string _overlayBg{}; ///< 뷰포트 위 오버레이(검색 팝업 · 알림) 바탕
        PROPERTY()
        string _selectionOutline{}; ///< 씬 뷰 선택 외곽선
        PROPERTY()
        string _highlight{}; ///< 2D 미리보기 · 그래프에서 고른 칸 · 지금 프레임
        PROPERTY()
        string _toggleInactive{}; ///< 꺼진 토글 단추
        PROPERTY()
        string _axisX{};
        PROPERTY()
        string _axisY{};
        PROPERTY()
        string _axisZ{};
        PROPERTY()
        string _colliderWire{}; ///< 씬 뷰 2D 콜라이더 와이어프레임
        PROPERTY()
        string _cameraWire{}; ///< 씬 뷰 카메라 프러스텀
        PROPERTY()
        string _viewportLabel{}; ///< 씬 뷰 위에 겹쳐 그리는 이름표(시각화 레이블)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 모서리 둥글기 토큰(96 DPI 픽셀)입니다. */
    REFLECT()
    struct EditorThemeRadiusDef
    {
        REFLECT_BODY();
        PROPERTY( Min = 0.0 )
        float32 _window{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _frame{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _popup{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _tab{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _scrollbar{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _grab{ 0.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 간격 토큰(96 DPI 픽셀)입니다. 여백과 간격에는 `_density` 를 곱합니다. */
    REFLECT()
    struct EditorThemeSpacingDef
    {
        REFLECT_BODY();
        PROPERTY( Min = 0.0 )
        float32 _windowPaddingX{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _windowPaddingY{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _framePaddingX{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _framePaddingY{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _itemSpacingX{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _itemSpacingY{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _itemInnerSpacingX{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _itemInnerSpacingY{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _indent{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _scrollbarSize{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _grabMinSize{ 0.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 테두리 두께 토큰(96 DPI 픽셀)입니다. */
    REFLECT()
    struct EditorThemeStrokeDef
    {
        REFLECT_BODY();
        PROPERTY( Min = 0.0 )
        float32 _window{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _child{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _popup{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _frame{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _tab{ 0.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 글꼴 토큰입니다. */
    REFLECT()
    struct EditorThemeTypographyDef
    {
        REFLECT_BODY();
        PROPERTY( Min = 6.0 )
        float32 _fontSize{ 0.0f }; ///< 본문 글꼴 크기(96 DPI 픽셀). ImGui 1.92 동적 글꼴이라 테마를 바꾸면 바로 바뀐다
        PROPERTY( Min = 0.1 )
        float32 _iconScale{ 0.0f }; ///< 아이콘 글꼴 크기 ÷ 본문 글꼴 크기. 아틀라스를 만들 때만 읽는다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief `<이름>.tokens.json` 한 벌입니다(라이트 · 다크 · 고대비 등).
     */
    REFLECT()
    struct EditorThemeDef
    {
        REFLECT_BODY();
        PROPERTY()
        string _name{}; ///< 파일 이름과 같다(`dark`). 설정(`EditorConfig::_themeName`)이 이 이름을 저장한다
        PROPERTY()
        string _displayName{}; ///< 테마 대화상자에 보이는 이름
        PROPERTY()
        EditorThemeColorDef _color{};
        PROPERTY()
        EditorThemeRadiusDef _radius{};
        PROPERTY()
        EditorThemeSpacingDef _spacing{};
        PROPERTY()
        EditorThemeStrokeDef _stroke{};
        PROPERTY()
        EditorThemeTypographyDef _typography{};
        PROPERTY( Min = 0.5, Max = 2.0 )
        float32 _density{ 0.0f }; ///< 여백 · 간격 배율(촘촘 0.8 · 보통 1 · 넉넉 1.2)
    };
} // namespace sw::editor

namespace sw::editor
{
    struct EditorThemeConfig;

    /**
     * @struct EditorThemeTokens
     * @brief 토큰 파일을 찾고, 읽고, 실행 값(`EditorThemeConfig`)으로 해석합니다.
     */
    struct SW_EDITOR_API EditorThemeTokens
    {
        static constexpr const utf8* kFolderName  = "theme";        ///< 에디터 팩(`Resource/editor`) 아래 폴더
        static constexpr const utf8* kExtension   = ".tokens.json"; ///< 토큰 파일 접미사
        static constexpr const utf8* kDefaultName = "dark";         ///< 설정이 없거나 그 이름의 파일이 없을 때 쓰는 벌

        /** @brief 토큰 폴더의 절대 경로입니다. 리소스 루트가 없으면 빈 문자열입니다. */
        static string getFolderPath();
        /** @brief 폴더에 있는 벌의 이름(접미사를 뺀 파일 이름)을 이름순으로 씁니다. */
        static void listThemeNames( vector<string>& outListName );
        /** @brief @p name 벌을 읽고 해석합니다. 실패하면 false 와 이유이고 @p outConfig 는 그대로입니다. */
        [[nodiscard]] static bool loadTheme( string_view name, EditorThemeConfig& outConfig, string& outError );
        /** @brief 리소스 id(`editor/theme/dark.tokens.json`)의 벌을 읽고 해석합니다(리소스 데이터 종류 `editortheme` 의 로더). */
        [[nodiscard]] static bool loadThemeResource( const string& resourceID );
        /** @brief 파일 하나를 읽어 해석합니다(테스트 · 핫 리로드). */
        [[nodiscard]] static bool loadThemeFile( string_view absolutePath, EditorThemeConfig& outConfig, string& outError );
        /** @brief 읽은 토큰을 실행 값으로 해석합니다. 색 글자가 형식에 맞지 않거나 비었으면 false 와 그 키입니다. */
        [[nodiscard]] static bool resolve( const EditorThemeDef& def, EditorThemeConfig& outConfig, string& outError );
    };
} // namespace sw::editor
