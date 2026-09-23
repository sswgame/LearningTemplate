#include "pch.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Core/Container/map.h"

#include "Editor/Common/Config/EditorData.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

SW_LOG_CALLER( "EditorService" );
namespace sw::editor
{
    namespace
    {
        ModuleService      s_editorService{};
        map<uint64, void*> s_mapLocalService{};
        EditorData*        s_pEditorData{ nullptr };
    } // namespace

    void bindEditorService( const ModuleService& service )
    {
        s_editorService = service;
    }

    void unbindEditorService()
    {
        s_editorService = {};
        s_mapLocalService.clear();
    }

    namespace internal
    {
        void* getRawService( sw::internal::ModuleServiceId id )
        {
            const uint32 rawId = sw::internal::toRawServiceId( id );
            if ( rawId >= sw::internal::kModuleServiceCount )
                return nullptr;
            return const_cast<void*>( s_editorService.arrServices[rawId] );
        }

        void bindRawLocalService( uint64 typeHash, void* pService )
        {
            if ( pService != nullptr )
                s_mapLocalService[typeHash] = pService;
            else
                s_mapLocalService.erase( typeHash );
        }

        void* getRawLocalService( uint64 typeHash )
        {
            const auto it = s_mapLocalService.find( typeHash );
            return it != s_mapLocalService.end() ? it->second : nullptr;
        }
    } // namespace internal

    EditorData& getEditorData()
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
        // `engine::getXxx()` 의 같은 모양은 일부러 그대로 두었다. 그쪽은 `TaskManager` 같은 하위
        // 시스템이라 지어낼 기본값이 없고, 필수 서비스가 연결됐는지는
        // `CheckEngineServiceBinding` 린트가 따로 지킨다.
        SW_LOG_ASSERT( s_pEditorData != nullptr, "EditorData is not bound — falling back to defaults" );
        if ( s_pEditorData == nullptr )
        {
            static EditorData s_defaultEditorData{};
            return s_defaultEditorData;
        }
        return *s_pEditorData;
    }

    void setEditorData( EditorData* pData )
    {
        s_pEditorData = pData;
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
