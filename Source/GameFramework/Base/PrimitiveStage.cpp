#include "pch.h"

#include "GameFramework/Base/PrimitiveStage.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "PrimitiveStage" );

    namespace
    {
        struct PrimitiveStageInternal
        {
            static constexpr const utf8* kTranslucentMaterialPath = "engine/materials/glassmaterial.material";

            static bool isSameColor( const float4& lhs, const float4& rhs )
            {
                return MathUtil::abs( lhs._x - rhs._x ) < 1.0e-4f && MathUtil::abs( lhs._y - rhs._y ) < 1.0e-4f && MathUtil::abs( lhs._z - rhs._z ) < 1.0e-4f &&
                       MathUtil::abs( lhs._w - rhs._w ) < 1.0e-4f;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PrimitiveLook PrimitiveLook::makeColor( const float4& color )
    {
        PrimitiveLook look;
        look._color = color;
        return look;
    }

    PrimitiveLook PrimitiveLook::makeTranslucent( const float4& color )
    {
        PrimitiveLook look;
        look._color        = color;
        look._materialPath = PrimitiveStageInternal::kTranslucentMaterialPath;
        return look;
    }

    PrimitiveStage::PrimitiveStage()
        : _listObject{}
        , _listInstance{}
        , _sceneGeneration{ 0 }
        , _bActive{ SW_FALSE }
    {
    }

    PrimitiveStage::~PrimitiveStage() = default;

    bool PrimitiveStage::begin( string_view sceneName )
    {
        SceneManager* pSceneManager = game::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return false;
        Scene* pScene = pSceneManager->getActiveScene();
        if ( pScene == nullptr )
            pScene = pSceneManager->createEmptyActiveScene( sceneName );
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
        {
            SW_LOG_WARNING( "Could not get an active scene for '%#'", sceneName );
            return false;
        }
        (void)pScene->ensureDefaultCameras(); // 이미 있으면 그대로 — 게임이 매 프레임 옮긴다
        _sceneGeneration = pSceneManager->getSceneGeneration();
        _bActive         = SW_TRUE;
        return true;
    }

    void PrimitiveStage::clear()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager != nullptr && isSceneChanged() == false )
        {
            for ( const GameObjectHandle& handle : _listObject )
            {
                GameObject* pObject = pManager->resolveGameObject( handle );
                if ( pObject != nullptr )
                    pManager->destroyObject( pObject );
            }
        }
        forget();
    }

    bool PrimitiveStage::isSceneChanged() const
    {
        if ( _bActive == SW_FALSE )
            return false;
        SceneManager* pSceneManager = game::getService<SceneManager>();
        return pSceneManager == nullptr || pSceneManager->getSceneGeneration() != _sceneGeneration;
    }

    void PrimitiveStage::forget()
    {
        _listObject.clear();
        _listInstance.clear();
        _bActive = SW_FALSE;
    }

    GameObject* PrimitiveStage::createMeshObject( const utf8* pName, const shared_ptr<Mesh>& mesh, const PrimitiveLook& look, const float3& position,
                                                  const float3& scale, const float3& rotation )
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr || mesh == nullptr )
            return nullptr;
        GameObject* pObject = pManager->createGameObject( hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        MeshComponent* pMesh = pObject->addComponent<MeshComponent>();
        if ( pMesh == nullptr )
        {
            pManager->destroyObject( pObject );
            return nullptr;
        }
        pMesh->setMesh( mesh );
        applyLook( *pMesh, look );
        pMesh->setLocalPosition( position );
        pMesh->setLocalRotation( rotation );
        pMesh->setLocalScale( scale );
        pMesh->setVisible( true );
        _listObject.push_back( pObject->getHandle() );
        return pObject;
    }

    GameObject* PrimitiveStage::createPrimitiveObject( const utf8* pName, string_view meshId, const PrimitiveLook& look, const float3& position, const float3& scale,
                                                       const float3& rotation )
    {
        const shared_ptr<Mesh> mesh = MeshUtil::acquirePrimitive( meshId );
        if ( mesh == nullptr )
        {
            SW_LOG_WARNING( "Unknown primitive '%#'", meshId );
            return nullptr;
        }
        return createMeshObject( pName, mesh, look, position, scale, rotation );
    }

    GameObject* PrimitiveStage::createSun( const float3& euler, float32 intensity, float32 shadowExtent )
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return nullptr;
        GameObject* pObject = pManager->createGameObject( hashed_string( "StageSun" ) );
        if ( pObject == nullptr )
            return nullptr;
        DirectionalLightComponent* pLight = pObject->addComponent<DirectionalLightComponent>();
        if ( pLight != nullptr )
        {
            pLight->setLocalRotation( euler );
            pLight->setIntensity( intensity );
            pLight->setShadowExtent( shadowExtent );
            pLight->setShadowDistance( shadowExtent * 2.0f );
            pLight->setCastShadow( true );
        }
        _listObject.push_back( pObject->getHandle() );
        return pObject;
    }

    void PrimitiveStage::adoptObject( const GameObject& object )
    {
        _listObject.push_back( object.getHandle() );
    }

    void PrimitiveStage::destroyObject( GameObjectHandle handle )
    {
        for ( size_t objectIndex = 0; objectIndex < _listObject.size(); ++objectIndex )
        {
            if ( _listObject[objectIndex] == handle )
            {
                _listObject[objectIndex] = _listObject.back();
                _listObject.pop_back();
                break;
            }
        }
        GameObjectManager* pManager = getObjectManager();
        GameObject*        pObject  = pManager != nullptr ? pManager->resolveGameObject( handle ) : nullptr;
        if ( pObject != nullptr )
            pManager->destroyObject( pObject );
    }

    void PrimitiveStage::setLook( MeshComponent& meshComponent, const PrimitiveLook& look )
    {
        applyLook( meshComponent, look );
    }

    shared_ptr<MaterialInstance> PrimitiveStage::acquireInstance( Material* pMaterial, const PrimitiveLook& look )
    {
        if ( pMaterial == nullptr )
            return nullptr;
        for ( const InstanceEntry& entry : _listInstance )
        {
            if ( entry._pMaterial == pMaterial && entry._texturePath == look._texturePath && PrimitiveStageInternal::isSameColor( entry._color, look._color ) )
                return entry._instance;
        }
        InstanceEntry entry;
        entry._instance = MaterialInstance::create( pMaterial );
        if ( entry._instance == nullptr )
            return nullptr;
        entry._instance->setVectorParameter( hashed_string( "color" ), look._color );
        if ( look._texturePath.empty() == false )
            entry._instance->setTextureParameter( hashed_string( "albedoMap" ), look._texturePath );
        entry._pMaterial   = pMaterial;
        entry._color       = look._color;
        entry._texturePath = look._texturePath;
        _listInstance.push_back( entry );
        return entry._instance;
    }

    void PrimitiveStage::placeCameras( const float3& position, const float3& target, float32 orthoHeight, float32 farPlane )
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        // 씬의 카메라를 모두 맞춘다 — 에디터 게임 뷰도 같은 카메라 등록부를 본다.
        for ( CameraComponent* pCamera : pManager->getCameraRegistry().getAll() )
        {
            if ( pCamera == nullptr )
                continue;
            pCamera->setOrthographic( orthoHeight > 0.0f );
            if ( orthoHeight > 0.0f )
                pCamera->setOrthoHeight( orthoHeight );
            pCamera->setFarPlane( farPlane );
            pCamera->setLocalPosition( position );
            pCamera->lookAt( target );
        }
    }

    void PrimitiveStage::placeCamerasWithRotation( const float3& position, const float3& euler, float32 fieldOfViewY, float32 farPlane )
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        for ( CameraComponent* pCamera : pManager->getCameraRegistry().getAll() )
        {
            if ( pCamera == nullptr )
                continue;
            pCamera->setOrthographic( false );
            pCamera->setFieldOfViewY( fieldOfViewY );
            pCamera->setNearPlane( 0.05f );
            pCamera->setFarPlane( farPlane );
            pCamera->setLocalPosition( position );
            pCamera->setLocalRotation( euler );
        }
    }

    Scene* PrimitiveStage::getScene() const
    {
        SceneManager* pSceneManager = game::getService<SceneManager>();
        return pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
    }

    GameObjectManager* PrimitiveStage::getObjectManager() const
    {
        Scene* pScene = getScene();
        return pScene != nullptr ? pScene->getObjectManager() : nullptr;
    }

    GameObject* PrimitiveStage::resolveObject( GameObjectHandle handle ) const
    {
        GameObjectManager* pManager = getObjectManager();
        return pManager != nullptr ? pManager->resolveGameObject( handle ) : nullptr;
    }

    MeshComponent* PrimitiveStage::findMesh( GameObjectHandle handle ) const
    {
        GameObject* pObject = resolveObject( handle );
        return pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
    }

    void PrimitiveStage::applyLook( MeshComponent& meshComponent, const PrimitiveLook& look )
    {
        Material* pMaterial = nullptr;
        if ( look._materialPath.empty() == false )
        {
            meshComponent.setMaterialPath( look._materialPath ); // 캐시가 잡고 컴포넌트가 놓는다
            pMaterial = meshComponent.getMaterial();
        }
        if ( pMaterial == nullptr )
        {
            Scene* pScene = getScene();
            pMaterial     = pScene != nullptr ? pScene->getMaterial() : nullptr;
            meshComponent.setMaterial( pMaterial ); // 직접 만든 씬은 기본 머티리얼이 붙지 않는다(BenchScene 참고)
        }
        const shared_ptr<MaterialInstance> instance = acquireInstance( pMaterial, look );
        if ( instance != nullptr )
            meshComponent.setMaterialInstance( instance );
    }
} // namespace sw
