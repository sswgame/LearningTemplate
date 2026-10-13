#include "pch.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Editor/Common/Config/EditorToolDefaults.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

SW_LOG_CALLER( "EditorService" );
namespace sw::editor
{
    namespace
    {
        ModuleServiceTable  s_editorService{};         ///< App 이 넘긴 호스트 서비스 표
        SubsystemScope*     s_pEditorScope{ nullptr }; ///< 지금 에디터 범위(에디터 컨텍스트 · 테스트가 건다)
        EditorToolDefaults* s_pEditorToolDefaults{ nullptr };
    } // namespace

    void bindEditorService( const ModuleServiceTable& service )
    {
        s_editorService = service;
    }

    void unbindEditorService()
    {
        s_editorService = {};
    }

    SubsystemScope* findEditorScope()
    {
        return s_pEditorScope;
    }

    void setEditorScope( SubsystemScope* pScope )
    {
        s_pEditorScope = pScope;
    }

    namespace internal
    {
        const ModuleServiceTable& getHostServiceTable()
        {
            return s_editorService;
        }
    } // namespace internal

    EditorToolDefaults& getEditorToolDefaults()
    {
        // **참조를 반환하는 API 에는 "없다" 고 답할 자리가 없다.** 그런데 `SW_LOG_ASSERT` 는
        // Debug 에서만 멈추고 Release · Shipping 에서는 로그만 남긴 뒤 **그대로 널을 역참조한다.**
        // 막으려던 것을 막지 못한다.
        //
        // 이것은 하위 시스템이 아니라 **설정 데이터**이고, 모든 필드가 의미 있는 기본값을 갖고
        // 있다(폰트 크기 16, 폴더 이름 등). 그래서 연결되기 전에 물으면 기본값을 반환한다.
        // 에디터가 죽는 대신 기본 설정으로 뜬다. Debug 의 assert 는 그대로 두어 "연결을 잊었다" 는
        // 사실은 분명히 드러나게 한다.
        //
        // `engine::getXxx()` 의 같은 모양은 기본값으로 떨어지지 않는다. 그쪽은 `TaskManager` 같은 하위
        // 시스템이라 지어낼 기본값이 없고, 필수 서비스가 연결됐는지는
        // 엔진 루프가 기동에서 지킨다(필수 행이 비면 서지 않는다).
        SW_LOG_ASSERT( s_pEditorToolDefaults != nullptr, "EditorToolDefaults is not bound — falling back to defaults" );
        if ( s_pEditorToolDefaults == nullptr )
        {
            static EditorToolDefaults s_defaultEditorToolDefaults{};
            return s_defaultEditorToolDefaults;
        }
        return *s_pEditorToolDefaults;
    }

    void setEditorToolDefaults( EditorToolDefaults* pData )
    {
        s_pEditorToolDefaults = pData;
    }

    Scene* getActiveScene()
    {
        SceneManager* pSceneManager = getService<SceneManager>();
        return pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
    }

    GameObjectManager* getActiveObjectManager()
    {
        Scene* pScene = getActiveScene();
        return pScene != nullptr ? pScene->getObjectManager() : nullptr;
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
