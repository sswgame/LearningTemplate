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
        /** @brief 현재 전역 변수 값을 프리셋 XML로 저장합니다. */
        [[nodiscard]] static bool savePreset( const string& filePath, const string& presetName );
        /** @brief 프리셋 XML을 읽어 전역 변수에 적용합니다. */
        [[nodiscard]] static bool loadPreset( const string& filePath );
        /** @brief 프리셋 파일이 값을 적은 전역 변수(등록된 것, 테스트용 제외) 이름입니다. 읽지 못하면 비어 있습니다. */
        static vector<string> collectPresetVariables( const string& filePath );
        /** @brief 이름 @p listName 의 지금 값을 프리셋과 같은 글로 담습니다(되돌리기 단계). 없는 이름은 뺍니다. */
        static string captureValues( const vector<string>& listName );
        /** @brief `captureValues` 의 글을 적용합니다 — 글에 있는 변수만 바꾼다(그 사이 콘솔 · 다른 창이 고친 변수는 그대로다). */
        static void restoreValues( string_view text );
        /** @brief 프리셋 폴더 절대 경로를 반환합니다. */
        static string getPresetFolderPath();
        /** @brief 컴포넌트 프리셋 폴더 절대 경로를 반환합니다. */
        static string getComponentPresetFolderPath();
        /** @brief 에디터 세션 프리셋 파일 절대 경로를 반환합니다. */
        static string getSessionPresetPath();
        /** @brief 현재 값을 에디터 세션 프리셋으로 저장합니다. */
        [[nodiscard]] static bool saveSessionPreset();
    };
} // namespace sw::editor
