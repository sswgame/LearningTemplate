#include "pch.h"

#include "Games/Demo/DemoGameHelpers.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/GameService.h"
#include "GameFramework/Data/GameData.h"
#include "GameFramework/Data/UnitStatsComponent.h"

namespace sw
{
	SW_LOG_CALLER( "DemoGame" );

	namespace
	{

		shared_ptr<MaterialInstance> s_glassMaterialInstance;

	} // namespace

	void destroyModuleSampleActors()
	{
		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr )
			return;

		GameObjectManager* pGameObjectManager = pScene->getObjectManager();
		if ( pGameObjectManager == nullptr )
			return;

		const hashed_string sampleName( "SampleActor" );
		vector<GameObject*> listToDestroy;
		for ( GameObject* pObj : pGameObjectManager->getAllGameObjects() )
		{
			if ( pObj != nullptr && pObj->getName() == sampleName )
				listToDestroy.push_back( pObj );
		}
		for ( GameObject* pObj : listToDestroy )
		{
			pGameObjectManager->destroyObject( pObj );
		}
	}

	void spawnSampleActorIfMissing()
	{
		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr )
			return;

		GameObjectManager* pGameObjectManager = pScene->getObjectManager();
		if ( pGameObjectManager == nullptr )
			return;

		const hashed_string sampleName( "SampleActor" );
		if ( pGameObjectManager->findGameObjectByName( sampleName ) != nullptr )
			return;

		GameObject* pSample = pGameObjectManager->createGameObject( sampleName );
		if ( pSample == nullptr )
			return;

		SceneComponent* pRoot = pSample->addComponent<SceneComponent>();
		if ( pRoot != nullptr )
			pRoot->setLocalPosition( float3( 0.0f, 1.0f, 0.0f ) );

		pSample->addComponent<UnitStatsComponent>();

		SW_LOG_TRACE( "Spawned SampleActor with SceneComponent + UnitStatsComponent." );
	}

	void spawnDemoCubeIfMissing()
	{
		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr )
			return;

		GameObjectManager* pGameObjectManager = pScene->getObjectManager();
		if ( pGameObjectManager == nullptr )
			return;

		const hashed_string cubeName( "DemoCube" );
		if ( pGameObjectManager->findGameObjectByName( cubeName ) != nullptr )
			return;

		GameObject* pCube = pGameObjectManager->createGameObject( cubeName );
		if ( pCube == nullptr )
			return;

		MeshComponent* pMeshComp = pCube->addComponent<MeshComponent>();
		if ( pMeshComp == nullptr )
			return;

		pMeshComp->setMesh( Mesh::createUnitCube() );
		pMeshComp->setLocalPosition( float3( 0.0f, 0.0f, 0.0f ) );
		pMeshComp->setLocalRotation( float3( 0.35f, 0.7f, 0.15f ) );
		pMeshComp->setLocalScale( float3( 1.0f, 1.0f, 1.0f ) );
		if ( pScene->getMaterial() != nullptr )
			pMeshComp->setMaterial( pScene->getMaterial() );

		SW_LOG_TRACE( "Spawned DemoCube GameObject with MeshComponent (unit cube)." );

		const hashed_string glassName( "DemoGlassCube" );
		if ( pGameObjectManager->findGameObjectByName( glassName ) == nullptr )
		{
			GameObject* pGlass = pGameObjectManager->createGameObject( glassName );
			if ( pGlass != nullptr )
			{
				MeshComponent* pGlassMesh = pGlass->addComponent<MeshComponent>();
				if ( pGlassMesh != nullptr )
				{
					pGlassMesh->setMesh( Mesh::createUnitCube() );
					pGlassMesh->setLocalPosition( float3( 1.6f, 0.2f, 0.0f ) );
					pGlassMesh->setLocalScale( float3( 0.55f, 0.55f, 0.55f ) );
					pGlassMesh->setBlendMode( RHIBlendMode::Transparent );
					if ( pScene->getMaterial() != nullptr )
					{
						pGlassMesh->setMaterial( pScene->getMaterial() );
						s_glassMaterialInstance	  = sw::make_shared<MaterialInstance>( pScene->getMaterial() );
						const GameData* pGameData = game::getService<GameData>();
						const string	micPath	  = pGameData != nullptr ? pGameData->_glassMaterialInstance : "game/demo/materials/glassorange.materialinstance";
						if ( s_glassMaterialInstance->loadFromFile( micPath ) == false )
						{
							constexpr float4 glassColor{ 1.0f, 0.4f, 0.0f, 0.5f };
							s_glassMaterialInstance->setVectorParameter( hashed_string( "color" ), glassColor );
							s_glassMaterialInstance->setScalarParameter( hashed_string( "roughness" ), 0.05f );
						}
						pGlassMesh->setMaterialInstance( s_glassMaterialInstance );
					}
					SW_LOG_TRACE( "Spawned DemoGlassCube (Transparent + MaterialInstance overrides)." );
				}
			}
		}
	}

	void destroyDemoCube()
	{
		Scene* pScene = game::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr )
			return;
		GameObjectManager* pGameObjectManager = pScene->getObjectManager();
		if ( pGameObjectManager == nullptr )
			return;

		const hashed_string cubeName( "DemoCube" );
		vector<GameObject*> listToDestroy;
		for ( GameObject* pObj : pGameObjectManager->getAllGameObjects() )
		{
			if ( pObj != nullptr && pObj->getName() == cubeName )
				listToDestroy.push_back( pObj );
		}
		for ( GameObject* pObj : listToDestroy )
		{
			pGameObjectManager->destroyObject( pObj );
		}
	}

	string resolveSavePath( string_view relativePath )
	{
		string absolutePath = ResourceUtil::getResourcePath( relativePath );
		if ( absolutePath.empty() )
			absolutePath = relativePath;
		return absolutePath;
	}

	float32 safeFill( int32 current, int32 max )
	{
		if ( max <= 0 )
			return 0.0f;
		return MathUtil::saturate( static_cast<float32>( current ) / static_cast<float32>( max ) );
	}
} // namespace sw
