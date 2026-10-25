#include "pch.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/CommandStack.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <atomic>

SW_LOG_CALLER( "EditorService" );
namespace sw::editor
{
    namespace
    {
        /** @brief 지금 에디터 범위와 그 범위를 등록한 스레드입니다. 둘은 `setEditorScope` 가 함께 바꾸는 한 칸이다. */
        struct EditorScopeSlot
        {
            SubsystemScope* _pScope{ nullptr }; ///< 지금 에디터 범위(에디터 컨텍스트 · 테스트가 건다)
            /// 지금 범위를 등록한 스레드(에디터 UI 가 도는 스레드)입니다. 범위가 없으면 기본값입니다. 읽는 쪽이 다른 스레드일 수 있어 atomic 입니다.
            std::atomic<std::thread::id> _ownerThreadID{};
        };

        const ISubsystemLocator* s_pHostServices{ nullptr }; ///< App 이 넘긴 호스트 서비스 범위
        EditorScopeSlot          s_editorScope{};
        EditorToolDefaults*      s_pEditorToolDefaults{ nullptr };
        CommandStack*            s_pFocusedCommandStack{ nullptr }; ///< 포커스된 툴 창의 Undo 스택(`setFocusedCommandStack`)
        EditorSelection*         s_pFocusedSelection{ nullptr };    ///< 겨누는 미리보기 선택(`setFocusedSelection`). nullptr 이면 메인 선택
        vector<EditorSelection*> s_listPreviewSelection;            ///< 미리보기 씬에 묶인 선택(`registerPreviewSelection`, 빌림)

        /**
         * @brief 에디터 상태(범위 · 포커스 · 미리보기 선택)를 만지는 스레드가 범위를 등록한 스레드인지 모든 구성에서 확인합니다.
         * @details 이 상태는 락 없이 읽고 쓴다 — 워커가 읽는 동안 종료가 범위를 허물면 해제된 메모리를 본다. 워커 본문은 에디터 상태를 직접 찾지 않고,
         *          제출 시점(범위를 등록한 스레드)에 서비스를 찾아 값으로 받는다. 어기면 여기서 멈춘다. 범위가 없으면 어느 스레드든 통과한다.
         *          Release 에서도 확인하는 이유: 비용이 원자 읽기 한 번과 스레드 번호 비교뿐이고, 어긴 호출은 Debug 가 돌지 않은 경로에서만 드러나
         *          종료 경합(해제된 범위 읽기)으로 나타나기 때문이다.
         */
        void assertEditorThread()
        {
            const std::thread::id scopeThreadID = s_editorScope._ownerThreadID.load( std::memory_order_relaxed );
            if ( scopeThreadID == std::thread::id{} || scopeThreadID == std::this_thread::get_id() )
                return;
            SW_LOG_ERROR( "Editor state is read off the editor thread - look the service up on the editor thread and pass it by value (see findEditorScope)" );
#if defined( SW_DEBUG )
            if ( sw::internal::tryCaptureAssert() )
                return; // 단언 가로채기를 건 테스트(`ScopedAssertCapture`)만 멈추지 않고 개수를 센다 — 가로채기는 Debug 에만 있다
#endif
            Logger::flushGlobalForCrash(); // 비동기 싱크가 보관하고 있는 위 한 줄을 멈추기 전에 내보낸다
            std::abort();
        }

        /** @brief 메인 씬의 선택입니다 — 지금 에디터 범위(에디터 컨텍스트 또는 테스트가 건 범위)에 건 `EditorSelection`. 없으면 nullptr 입니다. */
        EditorSelection* findMainSceneSelectionInScope()
        {
            // 타입 조회(`findSubsystem<EditorSelection>`)는 막혀 있다 — 범위를 직접 본다. 이 파일만 메인 선택이 어디 있는지 안다.
            return locateSubsystem<EditorSelection>( s_editorScope._pScope );
        }
    } // namespace

    void bindEditorService( const ISubsystemLocator& hostServices )
    {
        s_pHostServices = &hostServices;
    }

    void unbindEditorService()
    {
        s_pHostServices = nullptr;
    }

    namespace internal
    {
        void reportMissingRequiredService( const utf8* pLookup )
        {
            SW_LOG_ERROR( "Required editor service is missing: %#", pLookup );
#if defined( SW_DEBUG )
            if ( sw::internal::tryCaptureAssert() )
                return; // 단언 가로채기를 건 테스트(`ScopedAssertCapture`)만 멈추지 않고 개수를 센다 — 가로채기는 Debug 에만 있다
#endif
            // `SW_ASSERT( false )` 는 Release 에서 사라져 호출자의 `*pFound` 가 널 역참조로 이어졌다. 필수 서비스가 없는 것은 모든 구성에서 멈출 결함이다.
            Logger::flushGlobalForCrash(); // 비동기 싱크가 보관하고 있는 위 한 줄을 멈추기 전에 내보낸다
            std::abort();
        }
    } // namespace internal

    SubsystemScope* findEditorScope()
    {
        assertEditorThread();
        return s_editorScope._pScope;
    }

    void setEditorScope( SubsystemScope* pScope )
    {
        s_editorScope._ownerThreadID.store( pScope != nullptr ? std::this_thread::get_id() : std::thread::id{} );
        s_editorScope._pScope = pScope;
    }

    namespace internal
    {
        const ISubsystemLocator* findHostServices()
        {
            return s_pHostServices;
        }
    } // namespace internal

    EditorToolDefaults& getEditorToolDefaults()
    {
        // **참조를 반환하는 API 에는 "없다" 고 답할 방법이 없다.** 값의 원본은 editortooldefaults.json 이고 헤더는 형식만 가지므로
        // 지어낼 기본값도 없다(빈 글꼴 목록 · 0 색). 기동(`ImGuiEditor::initializeEditorData`)은 파일을 못 읽으면 서지 않으니,
        // 연결 전에 묻는 것은 호출 순서의 결함이다 — 모든 구성에서 이유를 남기고 멈춘다.
        if ( s_pEditorToolDefaults == nullptr )
        {
            SW_LOG_ERROR( "EditorToolDefaults is read before the editor bound it (setEditorToolDefaults) - call order is wrong" );
            Logger::flushGlobalForCrash(); // 비동기 싱크가 보관하고 있는 위 한 줄을 멈추기 전에 내보낸다
            std::abort();
        }
        return *s_pEditorToolDefaults;
    }

    void setEditorToolDefaults( EditorToolDefaults* pData )
    {
        s_pEditorToolDefaults = pData;
    }

    Scene* getActiveScene()
    {
        SceneManager* pSceneManager = findSubsystem<SceneManager>();
        return pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
    }

    Scene* getFocusedScene()
    {
        SceneManager* pSceneManager = findSubsystem<SceneManager>();
        return pSceneManager != nullptr ? pSceneManager->getFocusedScene() : nullptr;
    }

    CommandStack* getFocusedCommandStack()
    {
        assertEditorThread();
        return s_pFocusedCommandStack != nullptr ? s_pFocusedCommandStack : findSubsystem<CommandStack>();
    }

    void setFocusedCommandStack( CommandStack* pStack )
    {
        assertEditorThread();
        s_pFocusedCommandStack = pStack;
    }

    EditorSelection* getFocusedSelection()
    {
        assertEditorThread();
        if ( s_pFocusedSelection != nullptr )
            return s_pFocusedSelection;
        return findMainSceneSelectionInScope();
    }

    void setFocusedSelection( EditorSelection* pSelection )
    {
        assertEditorThread();
        s_pFocusedSelection = pSelection;
    }

    EditorSelection& getMainSceneSelection()
    {
        EditorSelection* pSelection = findMainSceneSelectionInScope();
        SW_ASSERT( pSelection != nullptr );
        return *pSelection;
    }

    EditorSelection* findSelectionOf( const GameObjectManager& manager )
    {
        assertEditorThread();
        for ( EditorSelection* pSelection : s_listPreviewSelection )
        {
            const Scene* pScene = pSelection->getScene();
            if ( pScene != nullptr && pScene->getObjectManager() == &manager )
                return pSelection;
        }
        return getActiveObjectManager() == &manager ? findMainSceneSelectionInScope() : nullptr;
    }

    const vector<EditorSelection*>& getPreviewSelections()
    {
        assertEditorThread();
        return s_listPreviewSelection;
    }

    void registerPreviewSelection( EditorSelection& selection )
    {
        assertEditorThread();
        if ( VectorUtil::contains( s_listPreviewSelection, &selection ) == false )
            s_listPreviewSelection.push_back( &selection );
    }

    void unregisterPreviewSelection( EditorSelection& selection )
    {
        assertEditorThread();
        VectorUtil::removeValue( s_listPreviewSelection, &selection );
    }

    GameObjectManager* getActiveObjectManager()
    {
        const SceneManager* pSceneManager = findSubsystem<SceneManager>();
        return pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
    }

    Scene* findSceneOf( const GameObjectManager* pManager )
    {
        const SceneManager* pSceneManager = findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr || pManager == nullptr )
            return nullptr;
        Scene* pActive = pSceneManager->getActiveScene();
        if ( pActive != nullptr && pActive->getObjectManager() == pManager )
            return pActive;
        for ( const PreviewSceneEntry& entry : pSceneManager->getPreviewScenes() )
        {
            if ( entry._scene->getObjectManager() == pManager )
                return entry._scene.get();
        }
        return nullptr;
    }

    GameObject* findGameObject( GameObjectHandle handle )
    {
        GameObjectManager* pManager = getActiveObjectManager();
        return ( pManager != nullptr && handle.isValid() ) ? pManager->resolveGameObject( handle ) : nullptr;
    }

    Component* findComponent( ComponentHandle handle )
    {
        GameObjectManager* pManager = getActiveObjectManager();
        return ( pManager != nullptr && handle.isValid() ) ? pManager->resolveComponent( handle ) : nullptr;
    }
} // namespace sw::editor
