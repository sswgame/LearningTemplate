#include "pch.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorToolDefaults.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"

SW_LOG_CALLER( "EditorService" );
namespace sw::editor
{
    namespace
    {
        const ISubsystemLocator* s_pHostServices{ nullptr }; ///< App 이 넘긴 호스트 서비스 범위
        SubsystemScope*          s_pEditorScope{ nullptr };  ///< 지금 에디터 범위(에디터 컨텍스트 · 테스트가 건다)
        EditorToolDefaults*      s_pEditorToolDefaults{ nullptr };
        CommandStack*            s_pFocusedCommandStack{ nullptr }; ///< 포커스된 툴 창의 Undo 스택(`setFocusedCommandStack`)
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
        return s_pEditorScope;
    }

    void setEditorScope( SubsystemScope* pScope )
    {
        s_pEditorScope = pScope;
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
        // **참조를 반환하는 API 에는 "없다" 고 답할 자리가 없다.** 값의 정본은 editortooldefaults.json 이고 헤더는 형식만 가지므로
        // 지어낼 기본값도 없다(빈 글꼴 목록 · 0 색). 기동(`ImGuiEditor::initializeEditorData`)은 파일을 못 읽으면 서지 않으니,
        // 연결 전에 묻는 것은 호출 순서의 결함이다 — 모든 구성에서 이유를 남기고 멈춘다.
        if ( s_pEditorToolDefaults == nullptr )
        {
            SW_LOG_ERROR( "EditorToolDefaults is read before the editor bound it (setEditorToolDefaults) - call order is wrong" );
            Logger::flushGlobalForCrash(); // 비동기 싱크가 들고 있는 위 한 줄을 멈추기 전에 내보낸다
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
        return s_pFocusedCommandStack != nullptr ? s_pFocusedCommandStack : findSubsystem<CommandStack>();
    }

    void setFocusedCommandStack( CommandStack* pStack )
    {
        s_pFocusedCommandStack = pStack;
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
