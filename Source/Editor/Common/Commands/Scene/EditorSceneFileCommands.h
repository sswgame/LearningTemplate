/**
 * @file EditorSceneFileCommands.h
 * @brief 씬 문서의 열기 · 새로 만들기 · 저장 · 종료 흐름입니다. 저장하지 않은 변경 확인을 거칩니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Common/Workspace/EditorSessionPolicy.h"

namespace sw::editor
{
    /**
     * @class EditorSceneFileCommands
     * @brief 메뉴 · 단축키 · 씬 에셋 동작이 함께 쓰는 씬 문서 동작입니다.
     * @details dirty 이면 동작을 워크스페이스의 대기 동작으로 미뤄 두고, 확인 창의 선택(`applyUnsavedSceneChoice`)이 이어서 실행합니다.
     */
    class EditorSceneFileCommands
    {
    public:
        /** @brief 씬을 비동기 로드하고 선택을 지웁니다. */
        [[nodiscard]] static bool loadScene( string_view path );
        /** @brief dirty 이면 확인을 띄우고, 아니면 로드합니다. 플레이 중이면 false 입니다. */
        [[nodiscard]] static bool tryOpenScene( string_view path );
        /** @brief dirty 이면 확인을 띄우고, 아니면 빈 씬으로 바꿉니다. */
        [[nodiscard]] static bool tryCreateNewScene();
        /** @brief 포커스된 dirty 도구 문서를 저장하거나 활성 씬을 저장합니다. */
        static void saveFocusedOrScene();
        /** @brief "저장하지 않은 변경" 확인 창에서 선택한 것을 적용합니다. */
        static void applyUnsavedSceneChoice( EditorUnsavedChoice choice );
        /** @brief dirty 이면 확인을 띄우고, 아니면 종료를 허용합니다. */
        [[nodiscard]] static bool tryBeginQuit();
        /** @brief 창 닫기를 시도합니다. dirty 이면 확인 모달이 뜹니다. */
        static void requestExit();
        /** @brief 활성 씬 세대가 바뀌면 dirty 와 프리팹 맵을 동기화합니다. */
        static void syncAfterSceneGenerationChange();
        /** @brief 활성 씬을 XML 로 저장합니다. path 가 비어 있으면 씬 소스 경로를 씁니다. */
        [[nodiscard]] static bool saveActiveScene( string_view path = {} );
        /** @brief 소스 경로가 있으면 저장하고, 없으면 Save As 대화상자를 엽니다. */
        static void saveActiveSceneOrPrompt();
    };
} // namespace sw::editor
