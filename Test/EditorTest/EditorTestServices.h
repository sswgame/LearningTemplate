/**
 * @file EditorTestServices.h
 * @brief 에디터 테스트가 지역 서비스를 스코프 동안 걸어 두는 도우미입니다.
 * @details `editor::findSubsystem<T>()` 의 모듈 서비스 표는 모듈 export 매크로의 `bindService` 콜백이 채우는데, 테스트는 그것을
 *          부르지 않습니다. 그래서 테스트 안에서는 서비스가 처음부터 없고, 필요한 것만 자기 범위에 걸어 지금 에디터 범위로 겁니다(`ScopedEditorScope`).
 *          여러 테스트 파일이 같은 도우미를 쓰므로 여기 한 곳에 둡니다 — 파일마다 익명 네임스페이스에 두면 유니티 빌드가
 *          파일들을 한 번역 단위로 합칠 때 재정의가 됩니다.
 */
#pragma once
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

namespace sw
{
    class CommandStack;
    class GlobalVariableManager;
    class SceneManager;
} // namespace sw

namespace sw::editor
{
    class EditorWorkspace;

    /** @brief 스코프 동안 워크스페이스를 지역 서비스로 걸어 둡니다 — 에디터 컨텍스트가 없어도 씬 dirty 표시가 여기에 닿습니다. */
    class ScopedWorkspaceService
    {
    public:
        explicit ScopedWorkspaceService( EditorWorkspace& workspace )
            : _scope{}
            , _activeScope{ _scope }
        {
            (void)_scope.bind( &workspace ); // 새 범위라 겹치지 않는다
        }

        ScopedWorkspaceService( const ScopedWorkspaceService& )            = delete;
        ScopedWorkspaceService& operator=( const ScopedWorkspaceService& ) = delete;

    private:
        SubsystemScope    _scope;       ///< 이 서비스만 든 범위
        ScopedEditorScope _activeScope; ///< 사는 동안 지금 에디터 범위로 건다(앞 범위를 상위로 잇는다)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 스코프 동안 커맨드 스택을 지역 서비스로 걸어 둡니다. */
    class ScopedCommandStackService
    {
    public:
        explicit ScopedCommandStackService( CommandStack& stack )
            : _scope{}
            , _activeScope{ _scope }
        {
            (void)_scope.bind( &stack ); // 새 범위라 겹치지 않는다
        }

        ScopedCommandStackService( const ScopedCommandStackService& )            = delete;
        ScopedCommandStackService& operator=( const ScopedCommandStackService& ) = delete;

    private:
        SubsystemScope    _scope;       ///< 이 서비스만 든 범위
        ScopedEditorScope _activeScope; ///< 사는 동안 지금 에디터 범위로 건다(앞 범위를 상위로 잇는다)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 스코프 동안 씬 매니저를 지역 서비스로 걸어 둡니다 — `editor::getActiveScene()` 과 핸들 풀기(`findGameObject`)가 답하게 됩니다. */
    class ScopedSceneManagerService
    {
    public:
        explicit ScopedSceneManagerService( SceneManager& sceneManager )
            : _scope{}
            , _activeScope{ _scope }
        {
            (void)_scope.bind( &sceneManager ); // 새 범위라 겹치지 않는다
        }

        ScopedSceneManagerService( const ScopedSceneManagerService& )            = delete;
        ScopedSceneManagerService& operator=( const ScopedSceneManagerService& ) = delete;

    private:
        SubsystemScope    _scope;       ///< 이 서비스만 든 범위
        ScopedEditorScope _activeScope; ///< 사는 동안 지금 에디터 범위로 건다(앞 범위를 상위로 잇는다)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 스코프 동안 선택을 메인 씬의 선택으로 걸어 둡니다 — `editor::getMainSceneSelection` · `getFocusedSelection` 이 이것을 돌려줍니다. */
    class ScopedMainSceneSelectionService
    {
    public:
        explicit ScopedMainSceneSelectionService( EditorSelection& selection )
            : _scope{}
            , _activeScope{ _scope }
        {
            (void)_scope.bind( &selection ); // 새 범위라 겹치지 않는다
        }

        ScopedMainSceneSelectionService( const ScopedMainSceneSelectionService& )            = delete;
        ScopedMainSceneSelectionService& operator=( const ScopedMainSceneSelectionService& ) = delete;

    private:
        SubsystemScope    _scope;       ///< 이 서비스만 든 범위
        ScopedEditorScope _activeScope; ///< 사는 동안 지금 에디터 범위로 건다(앞 범위를 상위로 잇는다)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 스코프 동안 빈 플레이 세션 상태(`EditorPlaySessionState`, 정지)를 지금 에디터 범위로 걸어 둡니다 — `EditorPlaySession::holdSceneEdits` 같은 상태 변경이 닿습니다. */
    class ScopedPlaySessionService
    {
    public:
        ScopedPlaySessionService()
            : _scope{}
            , _activeScope{ _scope }
        {
            (void)_scope.addSubsystem( make_unique<EditorPlaySessionState>() );
        }

        ScopedPlaySessionService( const ScopedPlaySessionService& )            = delete;
        ScopedPlaySessionService& operator=( const ScopedPlaySessionService& ) = delete;

    private:
        SubsystemScope    _scope;       ///< 상태만 든 범위
        ScopedEditorScope _activeScope; ///< 사는 동안 지금 에디터 범위로 건다(앞 범위를 상위로 잇는다)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 스코프 동안 빈 상태 저장소(`EditorStateStore`)를 지금 에디터 범위로 걸어 둡니다 — 파일 안의 상태(`getEditorState<T>()`)를 쓰는 함수가 이 테스트 몫의 값을 봅니다.
     * @details 테스트가 끝나면 저장소와 값이 함께 사라지므로 테스트끼리 값이 섞이지 않습니다.
     */
    class ScopedEditorStateService
    {
    public:
        ScopedEditorStateService()
            : _scope{}
            , _activeScope{ _scope }
        {
            (void)_scope.addSubsystem( make_unique<EditorStateStore>() );
        }

        ScopedEditorStateService( const ScopedEditorStateService& )            = delete;
        ScopedEditorStateService& operator=( const ScopedEditorStateService& ) = delete;

    private:
        SubsystemScope    _scope;       ///< 저장소만 든 범위
        ScopedEditorScope _activeScope; ///< 사는 동안 지금 에디터 범위로 건다(앞 범위를 상위로 잇는다)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 스코프 동안 전역 변수 매니저를 지역 서비스로 걸어 둡니다 — 프리셋 저장 · 불러오기가 이 매니저를 봅니다. */
    class ScopedGlobalVariableManagerService
    {
    public:
        explicit ScopedGlobalVariableManagerService( GlobalVariableManager& manager )
            : _scope{}
            , _activeScope{ _scope }
        {
            (void)_scope.bind( &manager ); // 새 범위라 겹치지 않는다
        }

        ScopedGlobalVariableManagerService( const ScopedGlobalVariableManagerService& )            = delete;
        ScopedGlobalVariableManagerService& operator=( const ScopedGlobalVariableManagerService& ) = delete;

    private:
        SubsystemScope    _scope;       ///< 이 서비스만 든 범위
        ScopedEditorScope _activeScope; ///< 사는 동안 지금 에디터 범위로 건다(앞 범위를 상위로 잇는다)
    };
} // namespace sw::editor
