/**
 * @file EditorService.h
 * @brief 에디터 모듈 안에서 서비스를 찾는 함수와 활성 씬 바로가기입니다. 에디터 범위(`SubsystemScope`)에서 먼저 찾고, 없으면 호스트(엔진) 서비스 테이블을 봅니다.
 * @details 에디터 범위는 에디터 컨텍스트(`EditorContext`)가 소유하고, 컨텍스트가 활성이 될 때 "지금 범위" 로 등록한다.
 *          언리얼에서 에디터가 `UEditorSubsystem` 을 보관하는 방식에 해당한다. 에디터 컨텍스트 없이 도는 테스트는 자기 범위를 만들어
 *          `ScopedEditorScope` 로 잠시 등록한다. 게임 쪽에서 같은 일을 하는 파일은 `GameService.h` 다.
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
        /** @brief 필수 서비스가 없을 때 오류 로그를 남기고 `SW_ASSERT` 로 멈춥니다(`getSubsystem<T>` 가 부릅니다). @p pLookup 은 어느 조회였는지 알리는 문자열입니다. */
        SW_EDITOR_API void reportMissingRequiredService( const utf8* pLookup );
    } // namespace internal

    /**
     * @brief 타입으로 찾지 못하게 막은 서브시스템이면 true 입니다. 클래스에 `static constexpr bool kAccessorOnly = true;` 를 적어 막습니다.
     * @details 선택처럼 인스턴스가 여럿인 서브시스템이 있다. 메인 씬의 선택과 미리보기 창마다의 선택이 따로 있어 "어느 것" 인지가 중요하다.
     *          타입 조회(`findSubsystem<T>`, `EditorContext::getSubsystem<T>`)는 늘 메인 것을 돌려주므로, 창이 대상인데 메인을 고치는 결함이
     *          새 호출부마다 다시 생긴다. 그래서 조회를 컴파일 단계에서 막고, 의도가 드러나는 접근자(`getMainSceneSelection`, `getFocusedSelection`,
     *          `findSelectionOf`)로만 얻게 한다.
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

    /**
     * @brief 지금 에디터 범위입니다. 에디터 컨텍스트가 활성이 아니고 테스트가 건 범위도 없으면 nullptr 입니다.
     * @details **범위를 등록한 스레드(에디터 UI 스레드)에서만 부릅니다.** 범위는 락 없이 읽고 쓰므로, 워커가 읽는 동안 종료가 범위를 허물면 해제된 범위를 본다.
     *          그래서 `findSubsystem<T>()` · `EditorContext::get()` 도 워커 본문에서 부르지 않는다. 일을 낼 때(호출 스레드) 필요한 서비스를 찾아 값(포인터 · 인자)으로 넘긴다
     *          (`TextureImporter::importTexture` 의 작업 시스템 인자). Debug 에서는 다른 스레드의 호출을 `SW_ASSERT` 가 잡는다.
     */
    SW_EDITOR_API SubsystemScope* findEditorScope();
    /** @brief 지금 에디터 범위를 등록합니다(nullptr 이면 해제합니다). 에디터 컨텍스트가 활성이 될 때(`EditorContext::setActive`) 부릅니다. */
    SW_EDITOR_API void setEditorScope( SubsystemScope* pScope );

    /**
     * @brief 에디터 서비스를 찾습니다. **없으면 nullptr 이므로** 받는 쪽이 확인해야 합니다.
     * @details 지금 에디터 범위와 그 상위 범위에서 먼저 찾고, 없으면 호스트 서비스 범위를 본다. 게임 쪽 `game::findSubsystem<T>()` 와 계약이 같다.
     *          한쪽만 실패 위치에 assert 를 두면 **Debug 에서만** 프로세스가 죽는다. `CheckNullableServiceUse` 린트가 두 함수를 모두 검사한다.
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

    /**
     * @brief 반드시 있어야 하는 에디터 · 호스트 서비스 @p T 입니다(`findSubsystem<T>()` 와 같은 순서). 없으면 부른 쪽의 잘못이라 오류 로그와 `SW_ASSERT` 로 멈춥니다.
     * @details 에디터가 떠 있으면 늘 있는 서비스(워크스페이스, 씬 관리자, 알림 관리자…)는 `findSubsystem` 으로 받아 nullptr 에서 조용히 돌아가지 말고 이것으로 받는다.
     *          테스트나 도구에서 정말 없을 수 있는 것만 `findSubsystem` 으로 받고, 그 자리에 왜 선택인지 한 줄 적는다.
     */
    template <typename T>
    T& getSubsystem()
    {
        T* pFound = findSubsystem<T>();
        if ( pFound == nullptr )
            internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
        return *pFound;
    }

    /**
     * @class ScopedEditorScope
     * @brief 이 객체가 사는 동안 범위 @p scope 를 "지금 에디터 범위" 로 등록합니다. 앞 범위를 상위로 연결하므로 이 범위에 없는 것은 앞 범위에서 찾습니다.
     * @details 에디터 컨텍스트 없이 도는 테스트가 서비스를 잠시 등록할 때 쓴다. 객체가 사라지면 앞 범위를 복원한다. 중첩해도 된다.
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
        SubsystemScope* _pPrevious; ///< 등록하기 전의 지금 범위
    };

    EditorToolDefaults& getEditorToolDefaults();
    void                setEditorToolDefaults( EditorToolDefaults* pData );

    // ------------------------------------------------------------------------------
    // 활성 씬 바로가기
    //
    // 에디터 코드는 거의 항상 "지금 편집 중인 씬" 과 그 GameObjectManager 를 원한다. 그런데
    // 거기까지 가려면 SceneManager 서비스 → getActiveScene() → getObjectManager() 를 거치며
    // 단계마다 nullptr 을 확인해야 한다. 커맨드와 패널마다 그 검사를 다시 쓰면 한 군데서 빠뜨렸을 때
    // 그 경우에만 조용히 크래시가 난다. 그래서 원하는 것을 호출 한 번으로 반환하고, 실패는 nullptr 하나로 합친다.
    // ------------------------------------------------------------------------------
    /** @brief 지금 편집 중인 씬입니다. 씬이 없으면 nullptr 입니다. */
    SW_EDITOR_API Scene* getActiveScene();
    /** @brief 지금 편집 중인 씬의 GameObjectManager 입니다. 씬이 없으면 nullptr 입니다. */
    SW_EDITOR_API GameObjectManager* getActiveObjectManager();
    /**
     * @brief @p pManager 를 가진 씬을 돌려줍니다. 활성 씬이거나 열린 미리보기 씬이며, 어느 쪽도 아니면 nullptr 입니다.
     * @details 프리팹 경로처럼 오브젝트에 딸린 기록은 활성 씬이 아니라 **오브젝트가 있는 씬**의 테이블에 둔다. 활성 씬에서 읽으면 미리보기 씬의 오브젝트를 찾지 못한다.
     */
    SW_EDITOR_API Scene* findSceneOf( const GameObjectManager* pManager );
    /**
     * @brief 지금 편집 중인 씬에서 핸들이 가리키는 오브젝트를 찾습니다. 씬이 없거나 대상이 사라졌으면 nullptr 입니다.
     * @details 에디터가 여러 프레임에 걸쳐 보관하는 참조(선택, 기즈모 대상, 인스펙터 되돌리기)는 모두 핸들이다. 쓸 때마다 이 함수로 대상을 찾는다.
     */
    SW_EDITOR_API GameObject* findGameObject( GameObjectHandle handle );
    /** @brief 지금 편집 중인 씬에서 핸들이 가리키는 컴포넌트를 찾습니다. 씬이 없거나 대상이 사라졌으면 nullptr 입니다. */
    SW_EDITOR_API Component* findComponent( ComponentHandle handle );

    // ------------------------------------------------------------------------------
    // 포커스 창의 범위. 미리보기 씬을 가진 툴 창(`EditorPreviewPanel`)이 포커스를 얻으면 자기 씬과 자기 Undo 스택을 등록한다.
    // 등록된 것이 없으면 활성 씬과 엔진의 `CommandStack` 을 쓴다. 언리얼의 애셋 에디터 툴킷이 자기 트랜잭션 맥락을 갖는 것에 해당한다.
    // ------------------------------------------------------------------------------
    /** @brief 툴이 대상으로 삼는 씬입니다. 포커스된 미리보기 창의 씬이고, 없으면 활성 씬입니다(`SceneManager::getFocusedScene`). */
    SW_EDITOR_API Scene* getFocusedScene();
    /** @brief Ctrl+Z 와 Ctrl+Y 가 쓰는 스택입니다. 포커스된 툴 창이 등록한 스택이고, 없으면 엔진의 전역 `CommandStack`, 그것도 없으면 nullptr 입니다. */
    SW_EDITOR_API CommandStack* getFocusedCommandStack();
    /** @brief 포커스된 툴 창의 Undo 스택을 등록합니다(nullptr 이면 해제합니다). 스택을 가진 창은 닫히기 전에 반드시 해제합니다. */
    SW_EDITOR_API void setFocusedCommandStack( CommandStack* pStack );
    /**
     * @brief 인스펙터와 기즈모가 대상으로 삼는 선택입니다. 가장 최근에 바뀌었거나 포커스를 얻은 미리보기 창의 선택이고, 없으면 메인 선택(에디터 컨텍스트의 `EditorSelection`)입니다.
     * @details 창 포커스가 인스펙터로 옮겨 가도 유지된다. 인스펙터를 누르면 창이 포커스를 잃으므로, 스택처럼 포커스 동안만 등록하면 인스펙터가 쓸 수 없다.
     *          메인 선택이 바뀌거나 창이 닫히면 메인으로 돌아온다(`EditorSelection::notifyChanged`, `unbindPreviewScope`). 둘 다 없으면 nullptr 다.
     */
    SW_EDITOR_API EditorSelection* getFocusedSelection();
    /** @brief 대상 선택을 등록합니다(nullptr 이면 메인 선택). 미리보기 선택만 등록하고, 메인 씬 창(씬 뷰, 계층)이 포커스를 얻으면 nullptr 로 되돌립니다. */
    SW_EDITOR_API void setFocusedSelection( EditorSelection* pSelection );

    // ------------------------------------------------------------------------------
    // 선택을 얻는 경로. `EditorSelection` 은 타입으로 찾지 못한다(`kIsAccessorOnlySubsystem`). 호출부마다 어느 선택인지 이름으로 적는다.
    //   getMainSceneSelection : 메인 씬(활성 씬)의 선택. 메인 월드를 보여 주는 창(씬 뷰, 계층, 맵 검사)과 셀프테스트 탐침이 쓴다.
    //   getFocusedSelection   : 지금 대상인 창의 선택. 인스펙터와 전역 명령(삭제, 복제, 정렬, 붙여넣기)이 쓴다.
    //   findSelectionOf       : 오브젝트가 있는 씬의 선택. 오브젝트를 받아 고치는 명령(지운 것을 선택에서 빼기, 복제본 고르기)이 쓴다.
    // ------------------------------------------------------------------------------
    /** @brief 메인 씬의 선택(에디터 컨텍스트의 `EditorSelection`)입니다. 에디터 범위에 없으면 부른 쪽의 잘못이라 `SW_ASSERT` 로 멈춥니다. */
    SW_EDITOR_API EditorSelection& getMainSceneSelection();
    /**
     * @brief @p manager 의 씬에 묶인 선택입니다. 미리보기 씬이면 그 창의 선택, 활성 씬이면 메인 선택이고, 테스트가 따로 만든 씬처럼 어느 쪽도 아니면 nullptr 입니다.
     * @details 오브젝트를 받아 고치는 명령은 포커스를 묻지 않고 대상이 있는 씬으로 선택을 고른다. 그래서 창이 대상으로 삼는 것과 명령이 받은 대상이 달라도 어긋나지 않는다.
     */
    SW_EDITOR_API EditorSelection* findSelectionOf( const GameObjectManager& manager );
    /** @brief 미리보기 씬에 묶인 선택들입니다(`EditorSelection::bindPreviewScope` 가 넣고 `unbindPreviewScope` 가 뺀다). */
    SW_EDITOR_API const vector<EditorSelection*>& getPreviewSelections();
    /** @brief 미리보기 씬에 묶인 선택을 목록에 넣거나 뺍니다. `EditorSelection` 만 부릅니다. */
    SW_EDITOR_API void registerPreviewSelection( EditorSelection& selection );
    SW_EDITOR_API void unregisterPreviewSelection( EditorSelection& selection );
} // namespace sw::editor
