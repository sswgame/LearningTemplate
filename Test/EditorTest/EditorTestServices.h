/**
 * @file EditorTestServices.h
 * @brief 에디터 테스트가 지역 서비스를 스코프 동안 걸어 두는 도우미입니다.
 * @details `editor::getService<T>()` 의 모듈 서비스 표는 모듈 export 매크로의 `bindService` 콜백이 채우는데, 테스트는 그것을
 *          부르지 않습니다. 그래서 테스트 안에서는 서비스가 처음부터 없고, 필요한 것만 `bindLocalService` 로 겁니다.
 *          여러 테스트 파일이 같은 도우미를 쓰므로 여기 한 곳에 둡니다 — 파일마다 익명 네임스페이스에 두면 유니티 빌드가
 *          파일들을 한 번역 단위로 합칠 때 재정의가 됩니다.
 */
#pragma once
#include "Editor/Common/Workspace/EditorService.h"

namespace sw
{
    class CommandStack;
    class SceneManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 스코프 동안 커맨드 스택을 지역 서비스로 걸어 둡니다. */
    class ScopedCommandStackService
    {
    public:
        explicit ScopedCommandStackService( CommandStack& stack ) { bindLocalService<CommandStack>( &stack ); }
        ~ScopedCommandStackService() { unbindLocalService<CommandStack>(); }

        ScopedCommandStackService( const ScopedCommandStackService& )            = delete;
        ScopedCommandStackService& operator=( const ScopedCommandStackService& ) = delete;
    };

    /** @brief 스코프 동안 씬 매니저를 지역 서비스로 걸어 둡니다 — `editor::getActiveScene()` 과 핸들 풀기(`findGameObject`)가 답하게 됩니다. */
    class ScopedSceneManagerService
    {
    public:
        explicit ScopedSceneManagerService( SceneManager& sceneManager ) { bindLocalService<SceneManager>( &sceneManager ); }
        ~ScopedSceneManagerService() { unbindLocalService<SceneManager>(); }

        ScopedSceneManagerService( const ScopedSceneManagerService& )            = delete;
        ScopedSceneManagerService& operator=( const ScopedSceneManagerService& ) = delete;
    };
} // namespace sw::editor
