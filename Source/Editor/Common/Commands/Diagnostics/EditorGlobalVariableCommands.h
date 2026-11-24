/**
 * @file EditorGlobalVariableCommands.h
 * @brief 전역 변수 프리셋 파일 IO 커맨드
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

namespace sw::editor
{
    /**
     * @class EditorGlobalVariableCommands
     * @brief Global Variables 패널의 프리셋 저장/로드/목록을 ImGui 없이 수행합니다.
     */
    class EditorGlobalVariableCommands
    {
    public:
        /** @brief 현재 전역 변수 값을 이름 @p presetName 의 프리셋 XML 글로 만듭니다(저장과 자동 저장 사본이 같은 글이다). */
        static string makePresetText( string_view presetName );
        /** @brief 프리셋 XML 글 @p presetText 를 전역 변수에 적용합니다. 읽지 못하면 false 이고 값은 그대로입니다. */
        [[nodiscard]] static bool applyPresetText( string_view presetText );
        /** @brief 현재 전역 변수 값을 프리셋 XML로 저장합니다. */
        [[nodiscard]] static bool savePreset( const AbsolutePath& filePath, const string& presetName );
        /** @brief 프리셋 XML을 읽어 전역 변수에 적용합니다. */
        [[nodiscard]] static bool loadPreset( const AbsolutePath& filePath );
        /** @brief 프리셋 파일이 값을 적은 전역 변수(등록된 것, 테스트용 제외) 이름입니다. 읽지 못하면 비어 있습니다. */
        static vector<string> collectPresetVariables( const AbsolutePath& filePath );
        /** @brief 이름 @p listName 의 지금 값을 프리셋과 같은 글로 담습니다(되돌리기 단계). 없는 이름은 뺍니다. */
        static string captureValues( const vector<string>& listName );
        /** @brief `captureValues` 의 글을 적용합니다 — 글에 있는 변수만 바꾼다(그 사이 콘솔 · 다른 창이 고친 변수는 그대로다). */
        static void restoreValues( string_view text );
        /** @brief 프리셋 폴더 절대 경로를 반환합니다. */
        static AbsolutePath getPresetFolderPath();
        /** @brief 컴포넌트 프리셋 폴더 절대 경로를 반환합니다. */
        static AbsolutePath getComponentPresetFolderPath();
        /** @brief 에디터 세션 프리셋 파일 절대 경로를 반환합니다. */
        static AbsolutePath getSessionPresetPath();
        /** @brief 에디터 세션 프리셋 글입니다 — `saveSessionPreset` 가 쓸 글입니다. */
        static string makeSessionPresetText();
        /** @brief 현재 값을 에디터 세션 프리셋으로 저장합니다. */
        [[nodiscard]] static bool saveSessionPreset();
    };
} // namespace sw::editor
