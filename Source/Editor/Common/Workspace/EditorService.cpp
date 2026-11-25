#include "pch.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/IEditorPanelService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <atomic>

namespace sw::editor
{
    /**
     * @class EditorFocus
     * @brief 에디터가 지금 겨누는 대상(초점 Undo 스택 · 초점 선택)과 미리보기 씬의 선택 목록입니다 — `getFocusedCommandStack` · `getFocusedSelection` · `registerPreviewSelection` 의 상태.
     * @details 에디터 서브시스템이라 컨텍스트와 함께 생기고 사라진다(정적 칸에 두지 않는다 — 범위마다 새로 선다). 이 파일의 함수만 찾으므로 헤더를 두지 않는다.
     *          미리보기 패널이 포커스를 얻으면 자기 스택 · 선택을 걸고 잃으면 푼다. 빌린 포인터만 든다. 에디터 UI 스레드에서만 쓴다.
     */
    class EditorFocus final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorFocus";

        string                   _focusedPanelID;               ///< 앞 포커스 패널의 등록 id(Ctrl+Z 를 그 패널의 지금 스택으로 보낸다). 비면 메인 스택
        EditorSelection*         _pFocusedSelection{ nullptr }; ///< 겨누는 미리보기 선택(목록에 오른 것만 — 그 구독이 풀리면 비운다). nullptr 이면 메인 선택
        vector<EditorSelection*> _listPreviewSelection;         ///< 미리보기 씬에 묶인 선택(빌림 — 구독이 사는 동안만 오른다)
        /** @brief 미리보기 선택의 등록을 구독으로 내준다. 구독이 풀리면 `releasePreviewSelection` 이 목록 · 대상에서 뺀다. 소멸이 남은 구독을 무효로 만든다. */
        ItemSubscriptionList<EditorSelection> _previewSubscriptions{ SW_DELEGATE_METHOD( ItemSubscriptionList<EditorSelection>::ReleaseFunction, &EditorFocus::releasePreviewSelection, this ) };
        const utf8*                           getSubsystemName() const override { return kSubsystemName; }
        /** @brief 겨누는 대상을 메인으로 돌립니다. 미리보기 선택 목록은 그 창의 구독이 풀릴 때 빠진다. */
        void resetToFreshEditor() override
        {
            _focusedPanelID.clear();
            _pFocusedSelection = nullptr;
        }

        /** @brief 미리보기 선택 @p pSelection 의 등록이 풀렸습니다. 목록에서 빼고, 대상 선택이었으면 메인으로 돌린다. */
        void releasePreviewSelection( EditorSelection* pSelection )
        {
            VectorUtil::removeValue( _listPreviewSelection, pSelection );
            if ( _pFocusedSelection == pSelection )
                _pFocusedSelection = nullptr;
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        /** @brief 지금 에디터 범위와 그 범위를 등록한 스레드입니다. 둘은 `setEditorScope` 가 함께 바꾸는 한 칸이다. */
        struct EditorScopeSlot
        {
            SubsystemScope* _pScope{ nullptr }; ///< 지금 에디터 범위(에디터 컨텍스트 · 테스트가 건다)
            /// 지금 범위를 등록한 스레드(에디터 UI 가 도는 스레드)입니다. 범위가 없으면 기본값입니다. 읽는 쪽이 다른 스레드일 수 있어 atomic 입니다.
            atomic<std::thread::id> _ownerThreadID{};
        };

        const ISubsystemLocator* s_pHostServices{ nullptr }; ///< App 이 넘긴 호스트 서비스 범위
        EditorScopeSlot          s_editorScope{};

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
#if defined( SW_DO_CHECKS )
            if ( sw::internal::tryCaptureAssert() )
            {
                // 단언 가로채기를 건 테스트(`ScopedAssertCapture`)만 멈추지 않고 개수를 센다 — 가로채기는 검사 구성에만 있다
                SW_LOG_ERROR_ID( "EditorService.EditorStateReadOff", "Editor state is read off the editor thread - look the service up on the editor thread and pass it by value (see findEditorScope)" );
                return;
            }
#endif
            reportFatalError( "Editor state is read off the editor thread - look the service up on the editor thread and pass it by value (see findEditorScope)" );
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
        // 참조를 반환하는 API 에는 "없다" 고 답할 방법이 없다. 값의 원본은 editortooldefaults.json 이고 헤더는 형식만 가지므로
        // 지어낼 기본값도 없다(빈 글꼴 목록 · 0 색). 에디터 컨텍스트가 범위에 건 뒤에 묻는 것이 계약이라, 없으면 `getSubsystem` 이 모든 구성에서 멈춘다.
        return getSubsystem<EditorToolDefaults>();
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
        const EditorFocus* const         pFocus  = findSubsystem<EditorFocus>();
        CommandStack* const              pMain   = findSubsystem<CommandStack>();
        const IEditorPanelService* const pPanels = findSubsystem<IEditorPanelService>(); // 셸 없는 테스트 호스트에는 패널이 없다
        if ( pPanels == nullptr )
            return pMain;
        uint32                          focusedWindowID{ 0 };
        vector<EditorPanelCommandStack> listPanel;
        pPanels->collectPanelCommandStacks( focusedWindowID, listPanel );
        return IEditorPanelService::chooseFocusedCommandStack( focusedWindowID, listPanel, pFocus != nullptr ? string_view( pFocus->_focusedPanelID ) : string_view{}, pMain );
    }

    void setFocusedPanel( string_view panelID )
    {
        assertEditorThread();
        EditorFocus* const pFocus = findSubsystem<EditorFocus>(); // 에디터 범위가 내려간 뒤(종료 중 패널 소멸)에는 걸 곳이 없다
        if ( pFocus != nullptr && pFocus->_focusedPanelID != panelID )
            pFocus->_focusedPanelID = string{ panelID };
    }

    EditorSelection* getFocusedSelection()
    {
        assertEditorThread();
        const EditorFocus* const pFocus = findSubsystem<EditorFocus>();
        if ( pFocus != nullptr && pFocus->_pFocusedSelection != nullptr )
            return pFocus->_pFocusedSelection;
        return findMainSceneSelectionInScope();
    }

    void setFocusedSelection( EditorSelection* pSelection )
    {
        assertEditorThread();
        EditorFocus* const pFocus = findSubsystem<EditorFocus>();
        if ( pFocus == nullptr )
            return;
        // 목록에 오른(구독이 사는) 미리보기 선택만 대상이 된다 — 풀린 선택을 가리키는 길이 없다.
        pFocus->_pFocusedSelection = pSelection != nullptr && VectorUtil::contains( pFocus->_listPreviewSelection, pSelection ) ? pSelection : nullptr;
    }

    EditorSelection& getMainSceneSelection()
    {
        EditorSelection* pSelection = findMainSceneSelectionInScope();
        if ( pSelection == nullptr )
            ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE ); // Release 에서도 멈춘다
        return *pSelection;
    }

    EditorSelection* findSelectionOf( const GameObjectManager& manager )
    {
        assertEditorThread();
        for ( EditorSelection* pSelection : getPreviewSelections() )
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
        static const vector<EditorSelection*> s_empty; // 에디터 범위가 없을 때(테스트 · 종료)의 빈 목록
        const EditorFocus* const              pFocus = findSubsystem<EditorFocus>();
        return pFocus != nullptr ? pFocus->_listPreviewSelection : s_empty;
    }

    Subscription registerPreviewSelection( EditorSelection& selection )
    {
        assertEditorThread();
        EditorFocus* const pFocus = findSubsystem<EditorFocus>();
        if ( pFocus == nullptr )
            return Subscription{};
        if ( VectorUtil::contains( pFocus->_listPreviewSelection, &selection ) == false )
            pFocus->_listPreviewSelection.push_back( &selection );
        return pFocus->_previewSubscriptions.issue( &selection );
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

namespace sw::editor
{
    SW_EDITOR_SUBSYSTEM( EditorFocus, 10 );
} // namespace sw::editor
