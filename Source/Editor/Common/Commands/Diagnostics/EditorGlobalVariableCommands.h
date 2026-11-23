/**
 * @file EditorGlobalVariableCommands.h
 * @brief 전역 변수 프리셋 파일 IO 커맨드
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

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
        [[nodiscard]] static bool savePreset( const string& filePath, const string& presetName );
        /** @brief 프리셋 XML을 읽어 전역 변수에 적용합니다. */
        [[nodiscard]] static bool loadPreset( const string& filePath );
        /** @brief 프리셋 폴더 절대 경로를 반환합니다. */
        static string getPresetFolderPath();
        /** @brief 컴포넌트 프리셋 폴더 절대 경로를 반환합니다. */
        static string getComponentPresetFolderPath();
        /** @brief 에디터 세션 프리셋 파일 절대 경로를 반환합니다. */
        static string getSessionPresetPath();
        /** @brief 에디터 세션 프리셋 글입니다 — `saveSessionPreset` 가 쓸 글입니다. */
        static string makeSessionPresetText();
        /** @brief 현재 값을 에디터 세션 프리셋으로 저장합니다. */
        [[nodiscard]] static bool saveSessionPreset();
    };
} // namespace sw::editor
