#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Utility/Resource/ResourceManager.h"

namespace sw
{

	namespace
	{

		/**
		 * @brief 엔진 데이터 설정으로부터 기본 머티리얼 경로를 반환합니다.
		 */
		string resolveDefaultMaterialPath()
		{
			return engine::getEngineData()._defaultMaterial;
		}


	} // namespace

	Scene::Scene( string_view name )
		: _name{ name }
		, _sourcePath{}
		, _defaultMaterialPath{}
		, _objectManager{ make_unique<GameObjectManager>() }
		, _pMaterial{ nullptr }
		, _pFrameRenderer{ nullptr }
		, _activeGameCamera{}
		, _activeEditorCamera{}
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
		const string materialPath = resolveDefaultMaterialPath();
		if ( materialPath.empty() == false )
		{
			if ( _pMaterial == nullptr || _defaultMaterialPath != materialPath )
			{
				releaseDefaultMaterial();
				_defaultMaterialPath = materialPath;
				_pMaterial			 = engine::getResourceManager().getMaterialManager().acquire( materialPath, pRhiDevice );
				if ( _pMaterial == nullptr )
				{
					SW_LOG_ERROR( "[Scene] Failed to acquire Material from %#", materialPath );
					_defaultMaterialPath.clear();
					return false;
				}
			}
		}
		ensureDefaultCameras();
		return true;
	}

	/**
	 * @brief 씬 리소스를 정리하고 머티리얼 참조를 해제합니다.
	 */
	void Scene::shutdown()
	{
		releaseDefaultMaterial();
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
			SW_LOG_WARNING( "[Scene] render: FrameRenderer not set" );
			return;
		}

		if ( _pFrameRenderer->isReady() == false )
			return; // initialize 실패 시 FrameRenderer가 ERROR 로깅

		if ( _pFrameRenderer->execute( pRhiDevice, _pMaterial, this ) == false )
			SW_LOG_ERROR( "[Scene] FrameRenderer::execute failed" );
	}

	namespace
	{
		static CameraComponent* findOrCreateCamera( GameObjectManager* pObjectManager, hashed_string name, CameraRole role, const float3& pos, const float3& lookTarget )
		{
			GameObject* pObj = pObjectManager->findGameObjectByName( name );
			if ( pObj == nullptr )
				pObj = pObjectManager->createGameObject( name );
			if ( pObj == nullptr )
				return nullptr;

			CameraComponent* pCam = pObj->getComponent<CameraComponent>().get();
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
	} // namespace

	/**
	 * @brief 게임 카메라와 에디터 카메라가 씬에 존재하는지 검사하고 없으면 기본 위치에 자동 생성합니다.
	 */
	bool Scene::ensureDefaultCameras()
	{
		if ( _objectManager == nullptr )
			return false;

		if ( _bCamerasEnsured && getActiveEditorCamera() != nullptr && getActiveGameCamera() != nullptr &&
			 getActiveEditorCamera()->isPendingKill() == false && getActiveGameCamera()->isPendingKill() == false )
			return true;

		_objectManager->flushSceneTransforms();

		findOrCreateCamera( _objectManager.get(), hashed_string( "EditorCamera" ), CameraRole::Editor, float3( 2.15f, 1.55f, 2.65f ), float3( 0.0f, 0.0f, 0.0f ) );
		findOrCreateCamera( _objectManager.get(), hashed_string( "GameCamera" ), CameraRole::Game, float3( 0.0f, 1.2f, 3.2f ), float3( 0.0f, 0.0f, 0.0f ) );

		_objectManager->flushSceneTransforms();

		GameObject* pEditorObj = _objectManager->findGameObjectByName( hashed_string( "EditorCamera" ) );
		GameObject* pGameObj   = _objectManager->findGameObjectByName( hashed_string( "GameCamera" ) );

		if ( pEditorObj != nullptr )
		{
			TComponentHandle<CameraComponent> camHandle = pEditorObj->getComponent<CameraComponent>();
			if ( camHandle.isValid() )
				camHandle->lookAt( float3( 0.0f, 0.0f, 0.0f ) );
		}
		if ( pGameObj != nullptr )
		{
			TComponentHandle<CameraComponent> camHandle = pGameObj->getComponent<CameraComponent>();
			if ( camHandle.isValid() )
				camHandle->lookAt( float3( 0.0f, 0.0f, 0.0f ) );
		}

		refreshCameraCache();
		if ( getActiveEditorCamera() == nullptr && pEditorObj != nullptr )
			setActiveEditorCamera( pEditorObj->getComponent<CameraComponent>().get() );
		if ( getActiveGameCamera() == nullptr && pGameObj != nullptr )
			setActiveGameCamera( pGameObj->getComponent<CameraComponent>().get() );

		_bCamerasEnsured = true;
		return true;
	}

	void Scene::refreshCameraCache()
	{
		_activeGameCamera	= {};
		_activeEditorCamera = {};
		if ( _objectManager == nullptr )
			return;

		int32			 bestGamePri   = MathUtil::MinInt32;
		int32			 bestEditorPri = MathUtil::MinInt32;
		CameraComponent* pBestGame{ nullptr };
		CameraComponent* pBestEditor{ nullptr };

		for ( GameObject* pObj : _objectManager->getAllGameObjects() )
		{
			if ( pObj == nullptr || pObj->isActive() == false )
				continue;
			auto pCam = pObj->getComponent<CameraComponent>();
			if ( pCam == nullptr || pCam->isActive() == false )
				continue;
			if ( pCam->getRole() == CameraRole::Game && pCam->getPriority() >= bestGamePri )
			{
				bestGamePri = pCam->getPriority();
				pBestGame	= pCam.get();
			}
			else if ( pCam->getRole() == CameraRole::Editor && pCam->getPriority() >= bestEditorPri )
			{
				bestEditorPri = pCam->getPriority();
				pBestEditor	  = pCam.get();
			}
		}

		storeCameraHandle( pBestGame, _activeGameCamera );
		storeCameraHandle( pBestEditor, _activeEditorCamera );
	}

	void Scene::setActiveGameCamera( CameraComponent* pCamera )
	{
		storeCameraHandle( pCamera, _activeGameCamera );
	}

	void Scene::setActiveEditorCamera( CameraComponent* pCamera )
	{
		storeCameraHandle( pCamera, _activeEditorCamera );
	}

	CameraComponent* Scene::getActiveGameCamera() const
	{
		return resolveCamera( _activeGameCamera );
	}

	CameraComponent* Scene::getActiveEditorCamera() const
	{
		return resolveCamera( _activeEditorCamera );
	}

	CameraComponent* Scene::getActiveRenderCamera( bool bEditorViewport ) const
	{
		CameraComponent* pEditorCam = getActiveEditorCamera();
		CameraComponent* pGameCam	= getActiveGameCamera();
		if ( bEditorViewport )
		{
			if ( pEditorCam != nullptr && pEditorCam->isActive() )
				return pEditorCam;
			if ( pGameCam != nullptr && pGameCam->isActive() )
				return pGameCam;
			return nullptr;
		}
		if ( pGameCam != nullptr && pGameCam->isActive() )
			return pGameCam;
		if ( pEditorCam != nullptr && pEditorCam->isActive() )
			return pEditorCam;
		return nullptr;
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
} // namespace sw
