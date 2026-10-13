#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    class EditorDockLayout;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorContext
     * @brief 에디터의 중앙 컨텍스트입니다. 에디터 셸(ImGuiEditor)이 직접 만들고 파괴합니다.
     * @details 에디터 범위(`SubsystemScope`)를 들고 하위 객체(선택 · 패널 · 뷰 렌더 타깃 · 플레이 세션 · 테마 …)를 만들어 그 범위에 넣는 얇은 껍데기다.
     *          상태는 하위 객체가 들고, 컨텍스트에는 범위와 생명 주기 구동만 있다(언리얼 `UEditorEngine` 이 `UEditorSubsystem` 컬렉션을 드는 자리).
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
         * @brief 활성 에디터 컨텍스트입니다. 셸이 `initialize` 에서 활성으로 하고 `shutdown` 에서 푼다. 없으면 nullptr 입니다.
         * @details 따로 든 정적 포인터가 아니라 지금 에디터 범위(`findEditorScope`)의 주인이다 — 컨텍스트가 자기 범위에 자신을 건다.
         *          하위 객체는 `getSubsystem<T>()`, 에디터 · 호스트 서비스는 `EditorService.h` 의 `getService<T>()` 로 찾는다.
         */
        static EditorContext* get();
        /** @brief 활성 에디터 컨텍스트를 설정합니다. 그 컨텍스트의 범위가 지금 에디터 범위가 된다(`editor::getService<T>()` 가 거기서 찾는다). */
        static void setActive( EditorContext* pContext );

        /**
         * @brief 에디터 범위의 하위 객체 @p T 입니다(`EditorSelection` · `EditorWorkspace` · `EditorPanelManager` · `IEditorPanelHost` …). `initialize` 뒤에는 늘 있다.
         * @details 언리얼 `GEditor->GetEditorSubsystem<T>()` 자리다. 범위가 타입으로 찾는다(하위 객체마다 포인터 · 접근자를 두지 않는다).
         *          `initialize` 전이나 `shutdown` 뒤에 부르면 부른 쪽의 잘못이라 `SW_ASSERT` 로 멈춘다 — 없을 수 있는 자리는 `findSubsystem` 을 쓴다.
         *          조회는 하위 객체 수만큼의 선형 검색이다 — 프레임마다 많이 부르는 자리는 한 번 얻은 참조를 든다.
         */
        template <class T>
        T& getSubsystem() const
        {
            return _scope.get<T>();
        }
        /** @brief 에디터 범위의 하위 객체 @p T 입니다. 없으면 nullptr 입니다. */
        template <class T>
        T* findSubsystem() const
        {
            return _scope.find<T>();
        }
        /** @brief 셸이 도킹 레이아웃을 알립니다(범위에 빌려 건다 — `findSubsystem<EditorDockLayout>()` 로 찾는다). 종료할 때는 nullptr 을 넘깁니다. */
        void setDockLayout( EditorDockLayout* pDockLayout );

    private:
        /** @brief 에디터 범위 — 아래 하위 객체(`EditorSubsystem`)를 소유하고 타입으로 찾게 합니다. 선언된 의존 순서로 초기화하고 역순으로 종료 · 소멸합니다. `getSubsystem<T>()` 가 여기서 찾는다. */
        SubsystemScope _scope;
    };
} // namespace sw::editor
