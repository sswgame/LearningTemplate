#include "pch.h"

#include "Engine/Scene/Scene.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
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
    namespace
    {
        struct SceneInternal
        {
            /**
             * @brief 엔진 데이터 설정으로부터 기본 머티리얼 경로를 반환합니다.
             */
            static string resolveDefaultMaterialPath()
            {
                return engine::getEngineData()._defaultMaterial;
            }

            /** @brief MeshComponent의 프리미티브 메시와 씬 기본 머티리얼을 채웁니다. */
            static void bindSceneMeshDefaults( Scene* pScene )
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
                    pMeshComp->resolveRuntimeMesh();
                    if ( pMeshComp->getMaterial() == nullptr && pDefaultMaterial != nullptr )
                        pMeshComp->setMaterial( pDefaultMaterial );
                } );
                pObjectManager->flushSceneTransforms();
            }

            static CameraComponent* findOrCreateCamera( GameObjectManager* pObjectManager, hashed_string name, CameraRole role, const float3& pos, const float3& lookTarget )
            {
                GameObject* pObj = pObjectManager->findGameObjectByName( name );
                if ( pObj == nullptr )
                    pObj = pObjectManager->createGameObject( name );
                if ( pObj == nullptr )
                    return nullptr;

                CameraComponent* pCam = pObj->getComponent<CameraComponent>();
                if ( pCam == nullptr )
                    pCam = pObj->addComponent<CameraComponent>();
                if ( pCam == nullptr )
                    return nullptr;

                pCam->setRole( role );
                pCam->setLocalPosition( pos );
                pCam->lookAt( lookTarget );
                pCam->setFieldOfViewY( 0.70f );
                pCam->setNearPlane( 0.1f );
                pCam->setFarPlane( 100.0f );
                return pCam;
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
        , _pFrameRenderer{ nullptr }
        , _activeGameCamera{}
        , _bCamerasEnsured{ false }
    {
    }

    Scene::~Scene()
    {
        shutdown();
    }

    /**
     * @brief 씬을 초기화하고 기본 머티리얼 리소스를 획득하며 기본 카메라를 설정합니다.
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
        SceneInternal::bindSceneMeshDefaults( this );
        return true;
    }

    bool Scene::instantiate( const SceneDocument& doc )
    {
        if ( _objectManager == nullptr )
            return false;

        vector<pair<GameObject*, string_view>> listRebindTarget;
        listRebindTarget.reserve( doc._listEntityNode.size() );

        for ( const SceneDocument::EntityNode& ent : doc._listEntityNode )
        {
            SW_LOG_TRACE( "Spawning entity '%#' prefab '%#'", ent._name, ent._prefab );
            GameObject* pGo{ nullptr };
            if ( ent._prefab.empty() == false )
            {
                pGo = engine::getResourceManager().getPrefabManager().spawn( _objectManager.get(), ent._prefab, ent._name.c_str() );
                if ( pGo == nullptr )
                    SW_LOG_WARNING( "Prefab spawn failed '%#' (%#)", ent._name, ent._prefab );
            }
            else
            {
                pGo = _objectManager->createGameObject( hashed_string( ent._name.c_str() ) );
            }

            if ( pGo != nullptr )
            {
                if ( ent._prefab.empty() == false )
                    _mapPrefabSource[pGo->getObjectId()] = ent._prefab;

                if ( ent._embeddedXml.empty() == false )
                {
                    if ( ObjectStateSerializer::loadFromXmlString( pGo, ent._embeddedXml ) == false )
                        SW_LOG_WARNING( "Embedded state apply failed for '%#'", ent._name );

                    const bool bHasHierarchy = ( ent._embeddedXml.find( "_attachOwner=" ) != string::npos );
                    if ( bHasHierarchy )
                        listRebindTarget.emplace_back( pGo, ent._embeddedXml );
                }
            }
        }

        for ( const auto& [pTargetGo, xmlView] : listRebindTarget )
        {
            if ( pTargetGo != nullptr )
                ObjectStateSerializer::rebindSceneHierarchy( pTargetGo, xmlView );
        }

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

        _objectManager->forEachGameObject( [&]( GameObject* pGo )
        {
            if ( pGo == nullptr || pGo->getParent() != nullptr )
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
        return true;
    }

    /**
     * @brief 씬 리소스를 정리하고 머티리얼 참조를 해제합니다.
     */
    void Scene::shutdown()
    {
        releaseDefaultMaterial();
        _mapPrefabSource.clear();
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
     * @brief 씬 내부의 모든 게임 오브젝트 및 컴포넌트를 매 프레임 업데이트합니다.
     */
    void Scene::tick( float32 deltaTime )
    {
        if ( _objectManager != nullptr )
            _objectManager->tick( deltaTime );
    }

    /**
     * @brief FrameRenderer로 씬의 GameObject를 그립니다.
     */
    void Scene::render( IRHIDevice* pRhiDevice )
    {
        if ( pRhiDevice == nullptr )
            return;

        if ( _pFrameRenderer == nullptr )
        {
            SW_LOG_WARNING( "render: FrameRenderer not set" );
            return;
        }

        if ( _pFrameRenderer->isReady() == false )
            return; // initialize 실패 시 FrameRenderer가 ERROR 로깅

        if ( _pFrameRenderer->execute( pRhiDevice, _pMaterial, this ) == false )
            SW_LOG_ERROR( "FrameRenderer execute failed." );
    }

    /**
     * @brief 게임 카메라가 씬에 존재하는지 검사하고 없으면 기본 위치에 자동 생성합니다.
     */
    bool Scene::ensureDefaultCameras()
    {
        if ( _objectManager == nullptr )
            return false;

        if ( _bCamerasEnsured && getActiveGameCamera() != nullptr && getActiveGameCamera()->isPendingKill() == false )
            return true;

        _objectManager->flushSceneTransforms();

        SceneInternal::findOrCreateCamera( _objectManager.get(), hashed_string( "GameCamera" ), CameraRole::Game, float3( 0.0f, 1.2f, 3.2f ), float3( 0.0f, 0.0f, 0.0f ) );

        _objectManager->flushSceneTransforms();

        GameObject* pGameObj = _objectManager->findGameObjectByName( hashed_string( "GameCamera" ) );
        if ( pGameObj != nullptr )
        {
            CameraComponent* pCam = pGameObj->getComponent<CameraComponent>();
            if ( pCam != nullptr )
                pCam->lookAt( float3( 0.0f, 0.0f, 0.0f ) );
        }

        refreshCameraCache();
        if ( getActiveGameCamera() == nullptr && pGameObj != nullptr )
            setActiveGameCamera( pGameObj->getComponent<CameraComponent>() );

        _bCamerasEnsured = true;
        return true;
    }

    DirectionalLightComponent* Scene::findActiveDirectionalLight() const
    {
        if ( _objectManager == nullptr )
            return nullptr;

        DirectionalLightComponent* pBest{ nullptr };
        _objectManager->forEachGameObject( [&]( GameObject* pObj )
        {
            if ( pObj == nullptr || pObj->isActiveInHierarchy() == false || pBest != nullptr )
                return;
            DirectionalLightComponent* pLight = pObj->getComponent<DirectionalLightComponent>();
            if ( pLight != nullptr && pLight->isActive() )
                pBest = pLight;
        } );
        return pBest;
    }

    void Scene::refreshCameraCache()
    {
        _activeGameCamera = {};
        if ( _objectManager == nullptr )
            return;

        int32            bestGamePri = MathUtil::MinInt32;
        CameraComponent* pBestGame{ nullptr };

        _objectManager->forEachGameObject( [&]( GameObject* pObj )
        {
            if ( pObj == nullptr || pObj->isActive() == false )
                return;
            CameraComponent* pCam = pObj->getComponent<CameraComponent>();
            if ( pCam == nullptr || pCam->isActive() == false )
                return;
            if ( pCam->getRole() != CameraRole::Game )
                return;
            if ( pCam->getPriority() < bestGamePri )
                return;
            bestGamePri = pCam->getPriority();
            pBestGame   = pCam;
        } );

        storeCameraHandle( pBestGame, _activeGameCamera );
    }

    void Scene::setActiveGameCamera( CameraComponent* pCamera )
    {
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
