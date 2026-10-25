#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    class EditorDockLayout;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorContext
     * @brief 에디터의 중앙 컨텍스트입니다. 에디터 셸(ImGuiEditor)이 직접 만들고 파괴합니다.
     * @details 에디터 범위(`SubsystemScope`)를 가지고, 하위 객체(선택, 패널, 뷰 렌더 타깃, 플레이 세션, 테마 등)를 만들어 그 범위에 넣는 얇은 껍데기다.
     *          상태는 하위 객체가 가지고 있고, 컨텍스트에는 범위와 생명 주기 처리만 있다. 언리얼의 `UEditorEngine` 이 `UEditorSubsystem` 컬렉션을
     *          보관하는 것에 해당한다.
     */
    class SW_EDITOR_API EditorContext
    {
    public:
        EditorContext();
        ~EditorContext();

        /** @brief 에디터 서브시스템들을 만들고 초기화합니다. */
        void initialize();
        /** @brief 에디터 서브시스템들을 정리하고 해제합니다. */
        void shutdown();
        /**
         * @brief 패널, 팝업, 인스펙터 매니저를 등록 목록과 맞춥니다. 에디터 프레임 앞에서 호출합니다.
         * @details 확장 모듈은 에디터가 뜬 뒤에도 로드되고 언로드된다(핫 리로드). 등록 세대가 같으면 비교 한 번으로 끝난다.
         */
        void syncExtensionRegistrations();

        /**
         * @brief 활성 에디터 컨텍스트입니다. 셸이 `initialize` 에서 활성으로 만들고 `shutdown` 에서 해제합니다. 없으면 nullptr 입니다.
         * @details 따로 보관하는 정적 포인터가 아니라 지금 에디터 범위(`findEditorScope`)의 주인이다. 컨텍스트가 자기 범위에 자신을 등록한다.
         *          하위 객체는 `getSubsystem<T>()` 로, 에디터와 호스트 서비스는 `EditorService.h` 의 `findSubsystem<T>()` 로 찾는다.
         *          **그리기 · 패널 · 메뉴에서는 `editor::getSubsystem<EditorContext>()` 를 쓴다.** 그곳에서 컨텍스트가 없는 것은 버그라 멈추는 게 맞고,
         *          `get()` 뒤의 null 확인은 조용히 건너뛰어 결함을 가린다. `get()` 은 명령 · 테스트 · 종료 · 도구처럼 컨텍스트가 없을 수 있는 곳 전용이다.
         */
        static EditorContext* get();
        /** @brief 활성 에디터 컨텍스트를 설정합니다. 그 컨텍스트의 범위가 지금 에디터 범위가 되고, `editor::findSubsystem<T>()` 가 거기서 찾습니다. */
        static void setActive( EditorContext* pContext );

        /**
         * @brief 에디터 범위의 하위 객체 @p T 입니다(`EditorSelection`, `EditorWorkspace`, `EditorPanelManager`, `IEditorPanelHost` 등). `initialize` 뒤에는 늘 있습니다.
         * @details 언리얼의 `GEditor->GetEditorSubsystem<T>()` 에 해당한다. 하위 객체마다 포인터나 접근자를 두지 않고 범위가 타입으로 찾는다.
         *          `initialize` 전이나 `shutdown` 뒤에 부르면 부른 쪽의 잘못이므로 `SW_ASSERT` 로 멈춘다. 없을 수 있는 곳에서는 `findSubsystem` 을 쓴다.
         *          조회는 하위 객체 수만큼의 선형 검색이므로, 프레임마다 자주 부르는 곳은 한 번 얻은 참조를 보관한다.
         */
        template <class T>
        T& getSubsystem() const
        {
            static_assert( kIsAccessorOnlySubsystem<T> == false, "this subsystem has several instances - use its named accessor (e.g. getMainSceneSelection / getFocusedSelection)" );
            return _scope.get<T>();
        }
        /** @brief 에디터 범위의 하위 객체 @p T 입니다. 없으면 nullptr 입니다. */
        template <class T>
        T* findSubsystem() const
        {
            static_assert( kIsAccessorOnlySubsystem<T> == false, "this subsystem has several instances - use its named accessor (e.g. getMainSceneSelection / getFocusedSelection)" );
            return _scope.find<T>();
        }
        /** @brief 셸이 도킹 레이아웃을 알립니다. 범위에 소유권 없이 등록하므로 `findSubsystem<EditorDockLayout>()` 로 찾습니다. 종료할 때는 nullptr 을 넘깁니다. */
        void setDockLayout( EditorDockLayout* pDockLayout );

    private:
        /** @brief 에디터 범위입니다. 하위 객체(`EditorSubsystem`)를 소유하고 타입으로 찾게 하며, 선언된 의존 순서로 초기화하고 역순으로 종료하고 소멸합니다. `getSubsystem<T>()` 가 여기서 찾습니다. */
        SubsystemScope _scope;
    };
} // namespace sw::editor
