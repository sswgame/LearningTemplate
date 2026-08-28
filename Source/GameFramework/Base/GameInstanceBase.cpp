#include "pch.h"

#include "GameFramework/Base/GameInstanceBase.h"

#include "Core/Memory/Memory.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	SW_LOG_CALLER( "GameInstanceBase" );

	bool GameInstanceBase::initialize( IWindow* pWindow, IRHIDevice* pRhiDevice )
	{
		_pWindow	= pWindow;
		_pRhiDevice = pRhiDevice;
		configureBootstrap( _bootstrap );
		const GameConfig& gameCfg = GameConfig::getActive();
		if ( gameCfg._packRoot.empty() == false )
			_bootstrap._packRoot = gameCfg._packRoot;
		const string_view gameDataFile =
			gameCfg._gameDataFile.empty() ? string_view( "data/gamedata.xml" ) : string_view( gameCfg._gameDataFile );
		if ( _bootstrap.load( gameDataFile ) == false )
			SW_LOG_WARNING( "Bootstrap load failed for pack '%#' — using defaults.", _bootstrap._packRoot );
		return onInitialize();
	}

	void GameInstanceBase::shutdown()
	{
		onShutdown();
		_pWindow	= nullptr;
		_pRhiDevice = nullptr;
	}

	void GameInstanceBase::update( float32 deltaTime )
	{
		onUpdate( deltaTime );
	}

	bool GameInstanceBase::serializeState( void* pOutBuffer, uint32* pInOutSize )
	{
		if ( pInOutSize == nullptr )
			return false;

		if ( game::areGameServicesBound() == false )
			return false;

		Scene* pActiveScene = game::getService<SceneManager>()->getActiveScene();
		if ( pActiveScene == nullptr || pActiveScene->getObjectManager() == nullptr )
			return false;

		vector<GameObject*> gameObjectList = pActiveScene->getObjectManager()->getAllGameObjects();

		vector<GameObject*> validObjectList;
		validObjectList.reserve( gameObjectList.size() );
		for ( GameObject* pObj : gameObjectList )
		{
			if ( pObj == nullptr || pObj->isPendingKill() )
				continue;

			validObjectList.push_back( pObj );
		}

		vector<uint8> bufferList;

		// 게임오브젝트 갯수를 맨 앞에 기록
		uint32 count   = static_cast<uint32>( validObjectList.size() );
		size_t oldSize = bufferList.size();
		bufferList.resize( oldSize + sizeof( uint32 ) );
		Memory::copy( bufferList.data() + oldSize, &count, sizeof( uint32 ) );

		for ( GameObject* pObj : validObjectList )
		{
			ObjectStateSerializer::saveToBinaryBuffer( pObj, bufferList );
		}

		if ( pOutBuffer == nullptr )
		{
			*pInOutSize = static_cast<uint32>( bufferList.size() );
			return true;
		}
		else if ( *pInOutSize >= bufferList.size() )
		{
			Memory::copy( pOutBuffer, bufferList.data(), bufferList.size() );
			*pInOutSize = static_cast<uint32>( bufferList.size() );
			return true;
		}

		return false;
	}

	bool GameInstanceBase::deserializeState( const void* pInBuffer, uint32 size )
	{
		if ( pInBuffer == nullptr || size < sizeof( uint32 ) )
			return false;

		if ( game::areGameServicesBound() == false )
			return false;

		Scene* pActiveScene = game::getService<SceneManager>()->getActiveScene();
		if ( pActiveScene == nullptr || pActiveScene->getObjectManager() == nullptr )
			return false;

		// 핫리로드 전 기존 엔티티들 싹 삭제
		pActiveScene->getObjectManager()->clear();

		const uint8* pData	= static_cast<const uint8*>( pInBuffer );
		size_t		 offset = 0;

		uint32 count = 0;
		Memory::copy( &count, pData + offset, sizeof( uint32 ) );
		offset += sizeof( uint32 );

		struct RestoredObject
		{
			GameObject* pObj{ nullptr };
			string		parentName{};
		};
		vector<RestoredObject> restoredObjectList;
		restoredObjectList.reserve( count );

		// 1차: 모든 게임오브젝트 생성 및 직렬화 복구
		for ( uint32 objectIndex = 0; objectIndex < count; ++objectIndex )
		{
			GameObject* pObj = pActiveScene->getObjectManager()->createGameObject();
			string		parentName;
			size_t		readBytes = ObjectStateSerializer::loadFromBinaryBuffer( pObj, pData + offset, size - offset, parentName );
			if ( readBytes == 0 )
			{
				SW_LOG_ERROR( "Failed to load binary object state at index %u", objectIndex );
				break;
			}
			restoredObjectList.push_back( { pObj, parentName } );
			offset += readBytes;
		}

		// 2차: 씬 계층 구조(Hierarchy) 및 부모-자식 관계 복원
		for ( const RestoredObject& restoredObj : restoredObjectList )
		{
			if ( restoredObj.parentName.empty() )
				continue;

			GameObject* pParent = pActiveScene->getObjectManager()->findGameObjectByName( hashed_string( restoredObj.parentName.c_str() ) );
			if ( pParent == nullptr )
			{
				SW_LOG_WARNING( "HotReload ParentGO not found: %s", restoredObj.parentName.c_str() );
				continue;
			}
			restoredObj.pObj->attachToParent( pParent );
		}

		// 핫리로드 된 모든 오브젝트들의 월드 매트릭스를 강제 동기화
		pActiveScene->getObjectManager()->flushSceneTransforms();

		return true;
	}
} // namespace sw
