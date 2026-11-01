/**
 * @file EditorThemeTokens.h
 * @brief 에디터 테마의 디자인 토큰입니다. 원본은 `Content/editor/theme/<이름>.tokens.json` 이고 이 파일은 그 형식(리플렉션)과 읽기 · 해석을 맡습니다.
 * @details 토큰은 색 · 모서리 · 간격 · 테두리 · 글꼴 크기 · 아이콘 크기 · 밀도이고, 파일에는 키(`Namespace.Key`) → 값 표 둘(`_color` · `_metric`)로 적는다.
 *          ImGui 스타일은 토큰에서만 만든다(`EditorThemeUtil::applyTheme`). 패널 코드는 색이나 간격을 숫자로 적지 않고 키로 가리킨다(`SW_THEMECOLOR` · `SW_THEMEMETRIC`).
 *          언리얼의 에디터 테마 JSON(`EditorThemes` 폴더, `FStyleColors`) · Slate 스타일셋의 이름 키, 유니티 UI Toolkit 의 USS 변수, Godot 의 Theme 리소스(형식 · 이름 키)에 해당한다.
 *
 *          색은 sRGB 16진 글자(`#RRGGBB` · `#RRGGBBAA`)다. ImGui 가 색을 그대로 출력 값으로 쓰므로 선형 변환은 하지 않는다.
 *          주의: 키 모양이 틀렸거나 색 글자가 틀리면 읽기 오류다. 벌끼리 키 묶음이 다르면 그 벌을 쓰지 않는다(한 벌만 고친 오타가 다른 벌에서 빈 색으로 보이지 않게).
 *          주의: `Typography.IconScale` 은 글꼴 아틀라스를 만들 때만 읽는다(아이콘 글꼴을 본문 글꼴에 합칠 때의 크기 비율). 바꾸면 에디터를 다시 띄워야 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /**
     * @brief `<이름>.tokens.json` 한 벌입니다(라이트 · 다크 · 고대비 등). 값은 키(`Namespace.Key`) → 값 표 둘이다.
     * @details 지역화 문구 표(`*.strings.xml` · `SW_LOCKEY`)와 같은 모양이다 — 코드는 키만 가리키고(`SW_THEMECOLOR` · `SW_THEMEMETRIC`), 값은 이 파일에만 있다.
     *          토큰을 더할 때 C++ 형식 · 접근자를 늘리지 않고, 테마를 바꾸는 일은 표를 갈아 끼우는 일이다.
     *          모든 벌은 같은 키 묶음을 가져야 한다(읽을 때 대조). 코드가 쓰는 키가 표에 있는지는 단위 테스트(`EditorThemeKeys`)가 소스를 훑어 본다.
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
        unordered_map<string, string> _color{}; ///< `Base.Text` → sRGB 16진 글자(`#RRGGBB` · `#RRGGBBAA`)
        PROPERTY()
        unordered_map<string, float32> _metric{}; ///< `Spacing.FramePaddingX` → 96 DPI 픽셀 또는 비율(`Typography.IconScale` · `Layout.Density`)
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
        static constexpr const utf8* kFolderName = "theme";        ///< 에디터 팩(`Content/editor`) 아래 폴더
        static constexpr const utf8* kExtension  = ".tokens.json"; ///< 토큰 파일 접미사

        /** @brief 토큰 폴더의 절대 경로입니다. 리소스 루트가 없으면 빈 문자열입니다. */
        static string getFolderPath();
        /** @brief 폴더에 있는 벌의 이름(접미사를 뺀 파일 이름)을 이름순으로 씁니다. */
        static void listThemeNames( vector<string>& outListName );
        /** @brief @p name 벌을 읽고 해석합니다. 실패하면 false 와 이유이고 @p outConfig 는 그대로입니다. */
        [[nodiscard]] static bool loadTheme( string_view name, EditorThemeConfig& outConfig, string& outError );
        /** @brief 리소스 id(`editor/theme/dark.tokens.json`)의 벌을 읽고 해석합니다(리소스 데이터 종류 `editortheme` 의 로더). */
        [[nodiscard]] static bool loadThemeResource( const AssetID& id );
        /** @brief 파일 하나를 읽어 해석합니다(테스트 · 핫 리로드). */
        [[nodiscard]] static bool loadThemeFile( string_view absolutePath, EditorThemeConfig& outConfig, string& outError );
        /** @brief 읽은 토큰을 실행 값으로 해석합니다. 키가 `Namespace.Key` 모양이 아니거나 색 글자가 형식에 맞지 않거나 필수 수치가 양수가 아니면 false 와 그 키입니다. */
        [[nodiscard]] static bool resolve( const EditorThemeDef& def, EditorThemeConfig& outConfig, string& outError );
        /** @brief 두 벌의 키 묶음이 다르면 true 와 한쪽에만 있는 키 하나입니다(모든 벌은 같은 키를 가져야 한다). */
        [[nodiscard]] static bool findKeyMismatch( const EditorThemeConfig& reference, const EditorThemeConfig& other, string& outKey );
    };
} // namespace sw::editor
