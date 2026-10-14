/**
 * @file EditorService.h
 * @brief 에디터 모듈 안에서 쓰는 서비스 창구와 활성 씬 바로가기입니다 — 에디터 범위(`SubsystemScope`)에서 찾고, 없으면 호스트(엔진) 서비스 표를 봅니다.
 * @details 에디터 범위는 에디터 컨텍스트(`EditorContext`)가 소유하고 활성이 될 때 "지금 범위" 로 건다(언리얼 `UEditorSubsystem` 을 에디터가 드는 자리).
 *          에디터 컨텍스트 없이 도는 테스트는 자기 범위를 만들어 `ScopedEditorScope` 로 잠시 건다. 게임 쪽 짝은 `GameService.h` 다.
 */
#pragma once
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    class CommandStack;
    class Component;
    class GameObject;
    class GameObjectManager;
    class Scene;
} // namespace sw

namespace sw::editor
{
    struct EditorToolDefaults;

    namespace internal
    {
        /** @brief App 이 넘긴 호스트 서비스 범위입니다(`bindEditorService`). 없으면 nullptr 입니다. */
        SW_EDITOR_API const ISubsystemLocator* findHostServices();
    } // namespace internal

    /** @brief App 이 넘긴 호스트 서비스 범위를 연결합니다. 범위는 `unbindEditorService` 까지 살아 있어야 합니다(호스트가 소유). */
    void bindEditorService( const ISubsystemLocator& hostServices );
    void unbindEditorService();

    /** @brief 지금 에디터 범위입니다. 에디터 컨텍스트가 활성이 아니고 테스트가 건 범위도 없으면 nullptr 입니다. */
    SW_EDITOR_API SubsystemScope* findEditorScope();
    /** @brief 지금 에디터 범위를 겁니다(nullptr 이면 풉니다). 에디터 컨텍스트가 활성이 될 때(`EditorContext::setActive`) 부른다. */
    SW_EDITOR_API void setEditorScope( SubsystemScope* pScope );

    /**
     * @brief 에디터 서비스를 찾습니다. **없으면 nullptr 이므로** 받는 쪽이 확인해야 합니다.
     * @details 지금 에디터 범위(그 상위 범위까지)에서 먼저 찾고, 없으면 호스트 서비스 표를 본다. 짝인 `game::getService<T>()` 와 같은 계약이다 —
     *          한쪽만 실패 자리에 assert 를 두면 **Debug 에서만** 프로세스가 죽는다. `CheckNullableServiceUse` 린트가 두 창구를 모두 본다.
     */
    template <typename T>
    T* getService()
    {
        T* pScoped = locateSubsystem<T>( findEditorScope() );
        if ( pScoped != nullptr )
            return pScoped;
        return locateSubsystem<T>( internal::findHostServices() );
    }

    /**
     * @class ScopedEditorScope
     * @brief 범위 @p scope 를 이 객체가 사는 동안 "지금 에디터 범위" 로 겁니다. 앞 범위를 상위로 이어, 그 범위에 없는 것은 앞 범위에서 찾는다.
     * @details 에디터 컨텍스트 없이 도는 테스트가 서비스를 잠시 걸 때 쓴다. 끝나면 앞 범위를 되돌린다(중첩해도 된다).
     */
    class ScopedEditorScope
    {
    public:
        explicit ScopedEditorScope( SubsystemScope& scope )
            : _pPrevious{ findEditorScope() }
        {
            scope.setParentScope( _pPrevious );
            setEditorScope( &scope );
        }
        ~ScopedEditorScope() { setEditorScope( _pPrevious ); }

        ScopedEditorScope( const ScopedEditorScope& )            = delete;
        ScopedEditorScope& operator=( const ScopedEditorScope& ) = delete;

    private:
        SubsystemScope* _pPrevious; ///< 걸기 전의 지금 범위
    };

    EditorToolDefaults& getEditorToolDefaults();
    void                setEditorToolDefaults( EditorToolDefaults* pData );

    // ------------------------------------------------------------------------------
    // 활성 씬 바로가기
    //
    // 에디터 코드는 거의 항상 "지금 편집 중인 씬" 과 그 GameObjectManager 를 원한다. 그런데
    // 거기까지 가려면 SceneManager 서비스 → getActiveScene() → getObjectManager() 를 거치며
    // 단계마다 nullptr 을 확인해야 한다. 커맨드 · 패널마다 그 검사를 다시 쓰면 한 군데서 빠뜨릴 때
    // 그때만 조용히 죽는다. 원하는 것을 호출 한 번으로 반환하고, 실패는 nullptr 하나로 합친다.
    // ------------------------------------------------------------------------------
    /** @brief 지금 편집 중인 씬입니다. 씬이 없으면 nullptr 입니다. */
    SW_EDITOR_API Scene* getActiveScene();
    /** @brief 지금 편집 중인 씬의 GameObjectManager 입니다. 씬이 없으면 nullptr 입니다. */
    SW_EDITOR_API GameObjectManager* getActiveObjectManager();
    /**
     * @brief 지금 편집 중인 씬에서 핸들이 가리키는 오브젝트를 찾습니다. 씬이 없거나 대상이 사라졌으면 nullptr 입니다.
     * @details 에디터가 프레임을 넘겨 드는 참조(선택 · 기즈모 대상 · 인스펙터 되돌리기)는 모두 핸들이고, 쓸 때마다 이것으로
     *          풉니다.
     */
    SW_EDITOR_API GameObject* findGameObject( GameObjectHandle handle );
    /** @brief 지금 편집 중인 씬에서 핸들이 가리키는 컴포넌트를 찾습니다. 씬이 없거나 대상이 사라졌으면 nullptr 입니다. */
    SW_EDITOR_API Component* findComponent( ComponentHandle handle );

    // ------------------------------------------------------------------------------
    // 포커스 창의 범위 — 미리보기 씬을 가진 툴 창(`EditorPreviewPanel`)이 포커스를 얻으면 자기 씬 · 자기 Undo 스택을 겁니다.
    // 걸린 것이 없으면 활성 씬 · 엔진의 `CommandStack` 입니다(언리얼 애셋 에디터 툴킷이 자기 트랜잭션 맥락을 갖는 자리).
    // ------------------------------------------------------------------------------
    /** @brief 툴이 겨누는 씬입니다(`SceneManager::getFocusedScene` — 포커스된 미리보기 창의 씬, 없으면 활성 씬). */
    SW_EDITOR_API Scene* getFocusedScene();
    /** @brief Ctrl+Z · Ctrl+Y 가 되돌릴 스택입니다. 포커스된 툴 창이 건 스택, 없으면 엔진의 전역 `CommandStack` 입니다. 없으면 nullptr 입니다. */
    SW_EDITOR_API CommandStack* getFocusedCommandStack();
    /** @brief 포커스된 툴 창의 Undo 스택을 겁니다(nullptr 이면 풉니다). 스택을 가진 창이 내려가기 전에 반드시 풉니다. */
    SW_EDITOR_API void setFocusedCommandStack( CommandStack* pStack );
} // namespace sw::editor
