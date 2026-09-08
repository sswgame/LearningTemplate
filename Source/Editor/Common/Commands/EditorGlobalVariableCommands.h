/**
 * @file EditorGlobalVariableCommands.h
 * @brief 전역 변수 프리셋 파일 IO 커맨드
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    struct GlobalVariableInfo;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorGlobalVariableCommands
     * @brief Global Variables 패널의 프리셋 저장/로드/목록을 ImGui 없이 수행합니다.
     */
    class EditorGlobalVariableCommands
    {
    public:
        /**
         * @brief 전역 변수 타입을 표시용 문자열로 바꿉니다 (Enum 은 열거형 이름).
         * @details 패널과 프리셋 저장이 같은 이름을 써야 해서 여기 한 곳에만 둔다 —
         *          예전엔 두 벌이라 GlobalVariableType 이 늘면 한쪽을 빠뜨리게 돼 있었다.
         */
        static string getTypeString( const GlobalVariableInfo& info );

        /** @brief 현재 전역 변수 값을 프리셋 XML로 저장합니다. */
        static bool savePreset( const string& filePath, const string& presetName );
        /** @brief 프리셋 XML을 읽어 전역 변수에 적용합니다. */
        static bool loadPreset( const string& filePath );
        /** @brief .gvpreset.xml 파일 경로 목록을 채웁니다. */
        static bool collectPresetFiles( vector<string>& outList );
        /** @brief 프리셋 폴더 절대 경로를 반환합니다. */
        static string getPresetFolderPath();
        /** @brief 컴포넌트 프리셋(.preset.xml) 폴더를 스캔합니다. */
        static bool collectComponentPresetFiles( vector<string>& outList );
        /** @brief 컴포넌트 프리셋 폴더 절대 경로를 반환합니다. */
        static string getComponentPresetFolderPath();
        /** @brief 에디터 세션 프리셋 파일 절대 경로를 반환합니다. */
        static string getSessionPresetPath();
        /** @brief 현재 값을 에디터 세션 프리셋으로 저장합니다. */
        static bool saveSessionPreset();
    };
} // namespace sw::editor
