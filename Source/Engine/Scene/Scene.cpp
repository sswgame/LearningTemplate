#include "pch.h"

#include "Engine/Scene/Scene.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Scene/SceneDocument.h"

namespace sw
{
    /**
     * @brief `-gv_defaultMaterial=<path>`: 씬 기본 머티리얼을 EngineData 대신 이 경로로 정합니다.
     * @details 벤치 · 시각 검증용입니다(예: engine/materials/benchtextured.material 로 텍스처 샘플링 경로를 봅니다).
     *          비어 있으면 EngineData._defaultMaterial 을 씁니다.
     */
    SW_GLOBAL_VARIABLE_STRING( gv_defaultMaterial, "", "씬 기본 머티리얼 경로 덮어쓰기 (비면 EngineData)" );

    namespace
    {
        struct SceneInternal
        {
            /**
             * @brief 기본 머티리얼 경로를 반환합니다(-gv_defaultMaterial 이 있으면 그것, 없으면 엔진 데이터 설정).
             */
            static string resolveDefaultMaterialPath()
            {
                // -gv_defaultMaterial 이 있으면 그것이 먼저다(선언은 이 파일 위쪽).
                if ( gv_defaultMaterial.empty() == false )
                    return gv_defaultMaterial;
                return engine::getEngineData()._defaultMaterial;
            }

            /**
             * @brief MeshComponent 의 프리미티브 메시와 머티리얼(저장된 참조, 없으면 씬 기본)을 채우고, 잡은 머티리얼을 @p pRhiDevice 로 올립니다.
             */
            static void bindSceneMeshDefaults( Scene* pScene, IRHIDevice* pRhiDevice )
            {
                if ( pScene == nullptr )
                    return;
                GameObjectManager* pObjectManager = pScene->getObjectManager();
                if ( pObjectManager == nullptr )
                    return;

                Material* pDefaultMaterial = pScene->getMaterial();
                pObjectManager->forEachGameObject( [&]( GameObject* pObj )
                {
                    if ( pObj == nullptr )
                        return;
                    MeshComponent* pMeshComp = pObj->getComponent<MeshComponent>();
                    if ( pMeshComp == nullptr )
                        return;
                    pMeshComp->resolveRenderAssets();
                    // 인스턴스가 붙은 메시는 건너뛴다. 그 메시의 머티리얼은 인스턴스의 부모이고, GpuSceneBuilder 가 그렇게 고른다.
                    // 여기서 씬 기본을 넣으면 배치가 기본 머티리얼(그룹 · 텍스처)과 인스턴스(원소 바이트 · 퍼뮤테이션)로 섞였다.
                    if ( pMeshComp->getMaterial() == nullptr && pMeshComp->getRawMaterialInstance() == nullptr && pDefaultMaterial != nullptr )
                        pMeshComp->setMaterial( pDefaultMaterial );
                } );
                engine::getResourceManager().getMaterialManager().initializePending( pRhiDevice );
                pObjectManager->flushSceneTransforms();
            }

            /**
             * @brief 켜져 있는 첫 방향광(등록 순서)을 찾습니다. @p bRequireShadow 면 그림자를 드리우는 것만 봅니다.
             * @details 활성 판정은 여기서 한다. 등록부는 "무엇이 있나"만 안다(PrimitiveRegistry 와 같은 규약). `Component::isActive` 가
             *          소유 오브젝트의 계층 활성까지 본다.
             */
            static DirectionalLightComponent* findDirectionalLight( const GameObjectManager* pObjectManager, bool bRequireShadow )
            {
                if ( pObjectManager == nullptr )
                    return nullptr;
                for ( LightComponent* pLight : pObjectManager->getLightRegistry().getAll( shaderslot::kLightTypeDirectional ) )
                {
                    if ( pLight == nullptr || pLight->isActive() == false )
                        continue;
                    DirectionalLightComponent* pDirectional = static_cast<DirectionalLightComponent*>( pLight );
                    if ( bRequireShadow && pDirectional->castsShadow() == false )
                        continue;
                    return pDirectional;
                }
                return nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "Scene" );

    Scene::Scene( string_view name )
        : _name{ name }
        , _sourcePath{}
        , _defaultMaterialPath{}
        , _objectManager{ make_unique<GameObjectManager>() }
        , _pMaterial{ nullptr }
        , _activeGameCamera{}
        , _gameCameraOverride{}
    {
    }

    Scene::~Scene()
    {
        Scene::shutdown();
    }

    /**
     * @brief 씬을 초기화합니다. 기본 머티리얼을 얻고 기본 카메라를 설정합니다.
     */
    bool Scene::initialize( IRHIDevice* pRhiDevice )
    {
        const string materialPath = SceneInternal::resolveDefaultMaterialPath();
        if ( materialPath.empty() == false )
        {
            if ( _pMaterial == nullptr || _defaultMaterialPath != materialPath )
            {
                releaseDefaultMaterial();
                _defaultMaterialPath = materialPath;
                _pMaterial           = engine::getResourceManager().getMaterialManager().acquire( materialPath, pRhiDevice );
                if ( _pMaterial == nullptr )
                {
                    SW_LOG_ERROR( "Failed to acquire Material from %#", materialPath );
                    _defaultMaterialPath.clear();
                    return false;
                }
            }
        }
        ensureDefaultCameras();
        SceneInternal::bindSceneMeshDefaults( this, pRhiDevice );
        return true;
    }

    bool Scene::instantiate( const SceneDocument& doc )
    {
        if ( _objectManager == nullptr )
            return false;

        // 오브젝트 **사이의** 부착은 모든 엔티티가 생긴 뒤라야 풀 수 있다. 그래서 두 번째 단계가 있다.
        vector<GameObject*> listRebindTarget;
        listRebindTarget.reserve( doc._listEntityNode.size() );

        for ( const SceneDocument::EntityNode& entity : doc._listEntityNode )
        {
            SW_LOG_TRACE( "Spawning entity '%#' prefab '%#'", entity._name, entity._prefab );
            GameObject* pGo{ nullptr };
            if ( entity._prefab.empty() == false )
            {
                pGo = engine::getResourceManager().getPrefabManager().spawn( _objectManager.get(), entity._prefab, entity._name.c_str() );
                if ( pGo == nullptr )
                {
                    // 버리지 않는다 — 버리면 다음 저장이 파일에서 지운다(덮어쓴 값 · 프리팹 GUID 까지). 문서 그대로 들고 있다가 저장 때
                    // 다시 써 넣는다(유니티의 "Missing Prefab" 과 같은 자리). 예전에는 경고 한 줄 뒤에 사라졌다.
                    SW_LOG_WARNING( "Prefab spawn failed for entity '%#' (%#, guid %#) - kept as-is and written back on save", entity._name, entity._prefab,
                                    entity._prefabGuid.empty() ? "none" : entity._prefabGuid.c_str() );
                    _listUnresolvedEntity.push_back( entity );
                }
            }
            else
            {
                pGo = _objectManager->createGameObject( hashed_string( entity._name.c_str() ) );
            }

            if ( pGo != nullptr )
            {
                if ( entity._prefab.empty() == false )
                    _mapPrefabSource[pGo->getObjectId()] = entity._prefab;

                // **구워진 바이너리 상태가 있으면 그것이 기준이다.** 쿠커가 왕복 검증에 성공한
                // 엔티티만 이쪽에 담고 XML 을 비우므로, 둘 다 차 있는 문서는 없다.
                if ( entity._embeddedStateBytes.empty() == false )
                {
                    string parentName;
                    if ( ObjectStateSerializer::loadFromBinaryBuffer( pGo, entity._embeddedStateBytes.data(), entity._embeddedStateBytes.size(), parentName ) == 0 )
                        SW_LOG_WARNING( "Embedded binary state apply failed for '%#'", entity._name );
                    else
                        listRebindTarget.push_back( pGo );
                }
                else if ( entity._embeddedXml.empty() == false )
                {
                    if ( ObjectStateSerializer::loadFromXmlString( pGo, entity._embeddedXml ) == false )
                        SW_LOG_WARNING( "Embedded state apply failed for '%#'", entity._name );

                    const bool bHasHierarchy = ( entity._embeddedXml.find( "_attachOwner=" ) != string::npos );
                    if ( bHasHierarchy )
                        listRebindTarget.push_back( pGo );
                }
            }
        }

        for ( GameObject* pTargetGo : listRebindTarget )
            ObjectStateSerializer::rebindSceneHierarchy( pTargetGo );

        _objectManager->mergePendingAdds();
        _objectManager->flushSceneTransforms();
        return true;
    }

    bool Scene::serializeToDocument( SceneDocument& outDoc ) const
    {
        if ( _objectManager == nullptr )
            return false;

        outDoc._name       = _name;
        outDoc._sourcePath = _sourcePath;
        outDoc._listEntityNode.clear();
        outDoc._bValid = true;

        // **자식 오브젝트도 자기 엔티티로 적는다.** 오브젝트 상태에는 자식 목록이 없고, 자식은 제 씬 컴포넌트의 `_attachOwner` 로 읽은 뒤
        // 되붙는다(`instantiate` 의 두 번째 단계 — 그래서 순서도 상관없다). 예전에는 부모가 있는 오브젝트를 건너뛰어, 계층 아래의 오브젝트가
        // 저장할 때마다 파일에서 사라졌다.
        _objectManager->forEachGameObject( [&]( GameObject* pGo )
        {
            if ( pGo == nullptr )
                return;
            CameraComponent* pCamera = pGo->getComponent<CameraComponent>();
            if ( pCamera != nullptr && pCamera->getRole() == CameraRole::Editor )
                return;
            SceneDocument::EntityNode node{};
            node._name = pGo->getName().c_str();

            const auto prefabIt = _mapPrefabSource.find( pGo->getObjectId() );
            if ( prefabIt != _mapPrefabSource.end() )
                node._prefab = prefabIt->second;

            if ( node._prefab.empty() == false && engine::areEngineServicesBound() )
            {
                const Uuid guid = engine::getResourceManager().getAssetDatabase().ensureMeta( node._prefab );
                if ( guid.isNull() == false )
                    node._prefabGuid = guid.toString();
            }
            node._embeddedXml = ObjectStateSerializer::saveToXmlString( pGo );
            if ( node._embeddedXml.empty() == false || node._prefab.empty() == false )
                outDoc._listEntityNode.push_back( std::move( node ) );
        } );
        // 프리팹을 찾지 못한 엔티티는 읽은 그대로 다시 쓴다(`instantiate` 설명).
        for ( const SceneDocument::EntityNode& unresolved : _listUnresolvedEntity )
            outDoc._listEntityNode.push_back( unresolved );
        return true;
    }

    /**
     * @brief 씬 리소스를 정리하고 머티리얼 참조를 해제합니다.
     */
    void Scene::shutdown()
    {
        releaseDefaultMaterial();
        _mapPrefabSource.clear();
        _listUnresolvedEntity.clear();
        if ( _objectManager != nullptr )
        {
            for ( GameObject* pObj : _objectManager->getAllGameObjects() )
            {
                if ( pObj != nullptr )
                    _objectManager->destroyObject( pObj );
            }
        }
    }

    /**
     * @brief 씬 안의 게임 오브젝트와 컴포넌트를 매 프레임 갱신합니다.
     */
    void Scene::tick( float32 deltaTime )
    {
        if ( _objectManager != nullptr )
            _objectManager->tick( deltaTime );
    }

    /**
     * @brief 씬에 게임 카메라가 있는지 보고, 없으면 기본 위치에 만듭니다.
     */
    bool Scene::ensureDefaultCameras()
    {
        if ( _objectManager == nullptr )
            return false;

        // 직접 고른 카메라가 먼저, 다음은 등록부의 규칙. 둘 다 없으면 지난번 카메라를 그대로 둔다 — 하나뿐인 게임 카메라를 끈 경우다.
        // 그때 새로 만들거나 기본값으로 되돌리면 꺼 둔 카메라를 프레임마다 옮기게 된다. 아무것도 없을 때만 만든다.
        CameraComponent* pCamera = resolveCamera( _gameCameraOverride );
        if ( CameraRegistry::isUsableCamera( pCamera ) == false )
            pCamera = _objectManager->getCameraRegistry().selectCamera( CameraRole::Game );
        if ( pCamera == nullptr )
            pCamera = resolveCamera( _activeGameCamera );
        if ( pCamera == nullptr || pCamera->isPendingDestroy() )
        {
            // 기본 GameCamera. 이미 있으면 **그대로** 쓴다 — `findOrCreateNamed` 는 있는 것의 자리 · 렌즈를 기본값으로 되돌린다.
            GameObject*      pNamed       = _objectManager->findGameObjectByName( hashed_string( "GameCamera" ) );
            CameraComponent* pNamedCamera = ( pNamed != nullptr ) ? pNamed->getComponent<CameraComponent>() : nullptr;
            if ( pNamedCamera != nullptr && pNamedCamera->isPendingDestroy() == false )
                pCamera = pNamedCamera;
            else
            {
                _objectManager->flushSceneTransforms();
                pCamera = CameraComponent::findOrCreateNamed( _objectManager.get(), hashed_string( "GameCamera" ), CameraRole::Game,
                                                              float3( 0.0f, 1.2f, 3.2f ), float3( 0.0f, 0.0f, 0.0f ) );
                _objectManager->flushSceneTransforms();
            }
        }

        storeCameraHandle( pCamera, _activeGameCamera );
        return pCamera != nullptr;
    }

    /**
     * @brief 지금 켜져 있는 방향광 하나를 반환합니다. 없으면 nullptr 입니다.
     * @details 등록부만 봅니다. 빛의 수에 비례하고 씬 크기와 무관합니다. 예전에는 **모든 GameObject**
     *          를 돌며 `getComponent<DirectionalLightComponent>()` 를 물었고, 찾은 뒤에도
     *          `forEachGameObject` 에 중단이 없어 끝까지 돌았습니다. EngineLoop 이 매 프레임 부르므로
     *          큐브 20,000 개 벤치에서 이 한 줄이 게임 스레드 프레임의 38%(7.6ms 중 2.9ms)였습니다.
     */
    DirectionalLightComponent* Scene::findActiveDirectionalLight() const
    {
        return SceneInternal::findDirectionalLight( _objectManager.get(), false );
    }

    DirectionalLightComponent* Scene::findShadowCastingDirectionalLight() const
    {
        return SceneInternal::findDirectionalLight( _objectManager.get(), true );
    }

    void Scene::setActiveGameCamera( CameraComponent* pCamera )
    {
        storeCameraHandle( pCamera, _gameCameraOverride );
        storeCameraHandle( pCamera, _activeGameCamera );
    }

    CameraComponent* Scene::getActiveGameCamera() const
    {
        return resolveCamera( _activeGameCamera );
    }

    void Scene::releaseDefaultMaterial()
    {
        if ( _defaultMaterialPath.empty() == false )
            engine::getResourceManager().getMaterialManager().release( _defaultMaterialPath );
        _pMaterial = nullptr;
        _defaultMaterialPath.clear();
    }

    CameraComponent* Scene::resolveCamera( sw::ComponentHandle handle ) const
    {
        if ( _objectManager == nullptr )
            return nullptr;
        return static_cast<CameraComponent*>( _objectManager->resolveComponent( handle ) );
    }

    void Scene::storeCameraHandle( CameraComponent* pCamera, sw::ComponentHandle& handle )
    {
        handle = pCamera != nullptr ? pCamera->getHandle() : sw::ComponentHandle{};
    }

    void Scene::setEntityPrefabPath( uint64 objectId, string_view prefabPath )
    {
        if ( objectId == 0 )
            return;
        if ( prefabPath.empty() )
            _mapPrefabSource.erase( objectId );
        else
            _mapPrefabSource[objectId] = string{ prefabPath };
    }

    const string& Scene::getEntityPrefabPath( uint64 objectId ) const
    {
        static const string s_emptyString{};
        const auto          it = _mapPrefabSource.find( objectId );
        if ( it != _mapPrefabSource.end() )
            return it->second;
        return s_emptyString;
    }
} // namespace sw
