/**
 * @file EditorService.h
 * @brief 에디터 모듈 안에서 쓰는 서비스 창구와 활성 씬 바로가기입니다 — 에디터 범위(`SubsystemScope`)에서 찾고, 없으면 호스트(엔진) 서비스 표를 봅니다.
 * @details 에디터 범위는 에디터 컨텍스트(`EditorContext`)가 소유하고 활성이 될 때 "지금 범위" 로 건다(언리얼 `UEditorSubsystem` 을 에디터가 드는 자리).
 *          에디터 컨텍스트 없이 도는 테스트는 자기 범위를 만들어 `ScopedEditorScope` 로 잠시 건다. 게임 쪽 짝은 `GameService.h` 다.
 */
#pragma once
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
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

    class EditorSelection;

    namespace internal
    {
        /** @brief App 이 넘긴 호스트 서비스 범위입니다(`bindEditorService`). 없으면 nullptr 입니다. */
        SW_EDITOR_API const ISubsystemLocator* findHostServices();
    } // namespace internal

    /**
     * @brief 타입으로 찾지 못하게 막은 서브시스템이면 true 입니다 — 클래스가 `static constexpr bool kAccessorOnly = true;` 를 적습니다.
     * @details 인스턴스가 여럿이라 "어느 것" 인지가 뜻인 서브시스템(선택 — 메인 씬의 것과 미리보기 창마다의 것)은 타입 조회(`findSubsystem<T>` ·
     *          `EditorContext::getSubsystem<T>`)가 늘 메인 것을 돌려주어, 창이 겨누는데 메인을 고치는 결함이 새 호출부마다 다시 생긴다.
     *          조회를 컴파일에서 막고 뜻이 드러나는 접근자(`getMainSceneSelection` · `getFocusedSelection` · `findSelectionOf`)로만 얻게 한다.
     */
    template <typename T, typename = void>
    struct IsAccessorOnlySubsystem : std::false_type
    {
    };
} // namespace sw::editor

namespace sw::editor
{
    template <typename T>
    struct IsAccessorOnlySubsystem<T, std::void_t<decltype( T::kAccessorOnly )>> : std::bool_constant<T::kAccessorOnly>
    {
    };
    template <typename T>
    inline constexpr bool kIsAccessorOnlySubsystem = IsAccessorOnlySubsystem<std::remove_cv_t<T>>::value;

    /** @brief App 이 넘긴 호스트 서비스 범위를 연결합니다. 범위는 `unbindEditorService` 까지 살아 있어야 합니다(호스트가 소유). */
    void bindEditorService( const ISubsystemLocator& hostServices );
    void unbindEditorService();

    /** @brief 지금 에디터 범위입니다. 에디터 컨텍스트가 활성이 아니고 테스트가 건 범위도 없으면 nullptr 입니다. */
    SW_EDITOR_API SubsystemScope* findEditorScope();
    /** @brief 지금 에디터 범위를 겁니다(nullptr 이면 풉니다). 에디터 컨텍스트가 활성이 될 때(`EditorContext::setActive`) 부른다. */
    SW_EDITOR_API void setEditorScope( SubsystemScope* pScope );

    /**
     * @brief 에디터 서비스를 찾습니다. **없으면 nullptr 이므로** 받는 쪽이 확인해야 합니다.
     * @details 지금 에디터 범위(그 상위 범위까지)에서 먼저 찾고, 없으면 호스트 서비스 범위를 본다. 짝인 `game::findSubsystem<T>()` 와 같은 계약이다 —
     *          한쪽만 실패 자리에 assert 를 두면 **Debug 에서만** 프로세스가 죽는다. `CheckNullableServiceUse` 린트가 두 창구를 모두 본다.
     */
    template <typename T>
    T* findSubsystem()
    {
        static_assert( kIsAccessorOnlySubsystem<T> == false, "this subsystem has several instances - use its named accessor (e.g. getMainSceneSelection / getFocusedSelection)" );
        T* pScoped = locateSubsystem<T>( findEditorScope() );
        if ( pScoped != nullptr )
            return pScoped;
        return locateSubsystem<T>( internal::findHostServices() );
    }

    /** @brief 반드시 있어야 하는 에디터 · 호스트 서비스 @p T 입니다(`findSubsystem<T>()` 와 같은 순서). 없으면 부른 쪽의 잘못이라 `SW_ASSERT` 로 멈춥니다. */
    template <typename T>
    T& getSubsystem()
    {
        T* pFound = findSubsystem<T>();
        SW_ASSERT( pFound != nullptr );
        return *pFound;
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
    /**
     * @brief 인스펙터 · 기즈모가 겨누는 선택입니다 — 가장 최근에 바뀌거나 포커스를 얻은 미리보기 창의 선택, 없으면 메인 선택(에디터 컨텍스트의 `EditorSelection`).
     * @details 창 포커스가 인스펙터로 옮겨 가도 유지된다(인스펙터를 누르면 창 포커스를 잃으므로 스택처럼 포커스 동안만 걸면 인스펙터가 쓸 수 없다).
     *          메인 선택이 바뀌거나 창이 내려가면 메인으로 돌아온다(`EditorSelection::notifyChanged` · `unbindPreviewScope`). 둘 다 없으면 nullptr 입니다.
     */
    SW_EDITOR_API EditorSelection* getFocusedSelection();
    /** @brief 겨누는 선택을 겁니다(nullptr 이면 메인 선택). 미리보기 선택만 겁니다. 메인 씬 창(씬 뷰 · 계층)이 포커스를 얻으면 nullptr 로 돌린다. */
    SW_EDITOR_API void setFocusedSelection( EditorSelection* pSelection );

    // ------------------------------------------------------------------------------
    // 선택을 얻는 길 — `EditorSelection` 은 타입으로 찾지 못한다(`kIsAccessorOnlySubsystem`). 호출부마다 어느 선택인지 이름으로 적는다.
    //   getMainSceneSelection : 메인 씬(활성 씬)의 선택. 메인 월드를 보여 주는 창(씬 뷰 · 계층 · 맵 검사)과 셀프테스트 탐침
    //   getFocusedSelection   : 지금 겨누는 창의 선택. 인스펙터 · 전역 명령(삭제 · 복제 · 정렬 · 붙여넣기)
    //   findSelectionOf       : 오브젝트가 사는 씬의 선택. 오브젝트를 받아 고치는 명령(지운 것을 선택에서 빼기 · 복제본 고르기)
    // ------------------------------------------------------------------------------
    /** @brief 메인 씬의 선택(에디터 컨텍스트의 `EditorSelection`)입니다. 에디터 범위에 없으면 부른 쪽의 잘못이라 `SW_ASSERT` 로 멈춥니다. */
    SW_EDITOR_API EditorSelection& getMainSceneSelection();
    /**
     * @brief @p manager 의 씬에 묶인 선택입니다 — 미리보기 씬이면 그 창의 선택, 활성 씬이면 메인 선택입니다. 어느 쪽도 아니면(테스트가 만든 따로 선 씬) nullptr 입니다.
     * @details 오브젝트를 받아 고치는 명령이 "포커스" 를 묻지 않고 대상이 사는 씬으로 선택을 고른다 — 창이 겨누는 것과 명령이 받은 대상이 달라도 어긋나지 않는다.
     */
    SW_EDITOR_API EditorSelection* findSelectionOf( const GameObjectManager& manager );
    /** @brief 미리보기 씬에 묶인 선택들입니다(`EditorSelection::bindPreviewScope` 가 넣고 `unbindPreviewScope` 가 뺀다). */
    SW_EDITOR_API const vector<EditorSelection*>& getPreviewSelections();
    /** @brief 미리보기 씬에 묶인 선택을 목록에 넣습니다 · 뺍니다. `EditorSelection` 만 부릅니다. */
    SW_EDITOR_API void registerPreviewSelection( EditorSelection& selection );
    SW_EDITOR_API void unregisterPreviewSelection( EditorSelection& selection );
} // namespace sw::editor
