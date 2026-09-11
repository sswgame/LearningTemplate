#include "pch.h"

#include "GameFramework/Base/GameInstanceBase.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"

#include "GameFramework/Base/GameService.h"

namespace sw
{
    namespace
    {
        struct StateEnvelopeInternal
        {
            static constexpr uint32 kMagic   = 0x53575354u; // 'SWST' (SW State Snapshot)
            static constexpr uint32 kVersion = 1;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameInstanceBase" );

    bool GameInstanceBase::initialize( IWindow* pWindow, IRHIDevice* pRhiDevice )
    {
        _pWindow    = pWindow;
        _pRhiDevice = pRhiDevice;
        configureBootstrap( _bootstrap );
        const GameConfig& gameCfg = GameConfig::getActive();
        if ( gameCfg._packRoot.empty() == false )
            _bootstrap._packRoot = gameCfg._packRoot;
        const string_view gameDataFile =
            gameCfg._gameDataFile.empty() ? string_view( "data/gamedata.xml" ) : string_view( gameCfg._gameDataFile );
        if ( _bootstrap.load( gameDataFile ) == false )
            SW_LOG_TRACE( "No custom bootstrap in pack '%#' — using defaults.", _bootstrap._packRoot );
        return onInitialize();
    }

    void GameInstanceBase::shutdown()
    {
        onShutdown();
        _pWindow    = nullptr;
        _pRhiDevice = nullptr;
    }

    void GameInstanceBase::update( float32 deltaTime )
    {
        onUpdate( deltaTime );
    }

    bool GameInstanceBase::serializeSceneObjects( vector<uint8>& outBytes )
    {
        if ( game::areGameServicesBound() == false )
            return false;

        Scene* pActiveScene = game::getService<SceneManager>()->getActiveScene();
        if ( pActiveScene == nullptr || pActiveScene->getObjectManager() == nullptr )
            return false;

        vector<GameObject*> listGameObject = pActiveScene->getObjectManager()->getAllGameObjects();

        vector<GameObject*> listValidObject;
        listValidObject.reserve( listGameObject.size() );
        for ( GameObject* pObj : listGameObject )
        {
            if ( pObj == nullptr || pObj->isPendingKill() == true )
                continue;

            listValidObject.push_back( pObj );
        }

        outBytes.clear();

        // 게임오브젝트 갯수를 맨 앞에 기록
        const uint32 count   = static_cast<uint32>( listValidObject.size() );
        const size_t oldSize = outBytes.size();
        outBytes.resize( oldSize + sizeof( uint32 ) );
        Memory::copy( outBytes.data() + oldSize, &count, sizeof( uint32 ) );

        for ( GameObject* pObj : listValidObject )
        {
            ObjectStateSerializer::saveToBinaryBuffer( pObj, outBytes );
        }

        return true;
    }

    bool GameInstanceBase::deserializeSceneObjects( const uint8* pData, size_t size )
    {
        if ( pData == nullptr || size < sizeof( uint32 ) )
            return false;

        if ( game::areGameServicesBound() == false )
            return false;

        Scene* pActiveScene = game::getService<SceneManager>()->getActiveScene();
        if ( pActiveScene == nullptr || pActiveScene->getObjectManager() == nullptr )
            return false;

        // 기존 엔티티들 정리
        pActiveScene->getObjectManager()->clear();

        size_t offset = 0;
        uint32 count  = 0;
        Memory::copy( &count, pData + offset, sizeof( uint32 ) );
        offset += sizeof( uint32 );

        struct RestoredObject
        {
            GameObject* _pObj{ nullptr };
            string      _parentName{};
        };
        vector<RestoredObject> listRestoredObject;
        listRestoredObject.reserve( count );

        // 1차: 모든 게임오브젝트 생성 및 직렬화 복구
        for ( uint32 objectIndex = 0; objectIndex < count; ++objectIndex )
        {
            GameObject* pObj = pActiveScene->getObjectManager()->createGameObject();
            string      parentName;
            size_t      readBytes = ObjectStateSerializer::loadFromBinaryBuffer( pObj, pData + offset, size - offset, parentName );
            if ( readBytes == 0 )
            {
                SW_LOG_ERROR( "Failed to load binary object state at index %u", objectIndex );
                break;
            }
            listRestoredObject.push_back( { pObj, parentName } );
            offset += readBytes;
        }

        // 2차: 씬 계층 구조(Hierarchy) 및 부모-자식 관계 복원
        for ( const RestoredObject& restoredObj : listRestoredObject )
        {
            if ( restoredObj._parentName.empty() )
                continue;

            GameObject* pParent = pActiveScene->getObjectManager()->findGameObjectByName( hashed_string( restoredObj._parentName.c_str() ) );
            if ( pParent == nullptr )
            {
                SW_LOG_WARNING( "State Restore ParentGO not found: %s", restoredObj._parentName.c_str() );
                continue;
            }
            restoredObj._pObj->attachToParent( pParent );
        }

        // 복원된 모든 오브젝트들의 월드 매트릭스를 강제 동기화
        pActiveScene->getObjectManager()->flushSceneTransforms();

        return true;
    }

    bool GameInstanceBase::serializeState( void* pOutBuffer, uint32* pInOutSize )
    {
        if ( pInOutSize == nullptr )
            return false;

        onBeforeStateSerialize();

        Archive arch;
        arch << StateEnvelopeInternal::kMagic;
        arch << StateEnvelopeInternal::kVersion;

        // 1) 씬 오브젝트 바이너리 스냅샷
        //
        // **씬이 없는 것은 실패가 아니다.** 씬 없이 커스텀 상태만 스냅샷하는 것은 지원되는 사용법이라
        // (GameFrameworkTest.GameInstanceBaseSnapshotAndFileRoundTrip 이 그렇게 쓴다) 여기서 끊으면 안 된다.
        // 다만 예전엔 반환값을 **아무 흔적 없이** 버렸다 — 씬이 있어야 할 상황에서 오브젝트가 하나도 없는
        // 세이브가 나와도 로드할 때까지 아무도 몰랐다. 빈 섹션은 그대로 쓰되 실마리는 남긴다.
        vector<uint8> bytesScene;
        if ( serializeSceneObjects( bytesScene ) == false )
        {
            SW_LOG_WARNING( "씬 오브젝트 스냅샷이 비었습니다 — 활성 씬이 없거나 게임 서비스가 바인딩되지 않았습니다. "
                            "커스텀 상태만 저장됩니다." );
        }
        arch.writeSection( bytesScene.data(), static_cast<uint32>( bytesScene.size() ) );

        // 2) 파생 클래스 커스텀 리플렉션 상태 스냅샷
        const TypeInfo* pStateTypeInfo = getStateTypeInfo();
        const void*     pStateInstance = getStateInstance();
        if ( pStateTypeInfo != nullptr && pStateInstance != nullptr )
        {
            // serialize 는 void 라 성공 여부를 돌려주지 않는다. 결과가 비면 **단정하지 않고 남긴다** —
            // 프로퍼티가 없는 상태 타입도 있을 수 있어 여기서 실패로 끊으면 멀쩡한 저장을 막는다.
            // 나중에 "상태가 비어서 돌아왔다" 를 추적할 실마리는 있어야 한다.
            vector<uint8> bytesState;
            BinarySerializer::serialize( pStateInstance, *pStateTypeInfo, bytesState );
            if ( bytesState.empty() )
                SW_LOG_WARNING( "'%#' 의 커스텀 상태가 빈 채로 직렬화되었습니다 — 로드하면 기본값이 됩니다.", pStateTypeInfo->_name.c_str() );
            arch.writeSection( bytesState.data(), static_cast<uint32>( bytesState.size() ) );
        }
        else
        {
            arch.writeSection( nullptr, 0 );
        }

        const size_t totalSize = arch.getSize();
        if ( pOutBuffer == nullptr )
        {
            *pInOutSize = static_cast<uint32>( totalSize );
            return true;
        }

        if ( *pInOutSize < totalSize )
            return false;

        Memory::copy( pOutBuffer, arch.getData(), totalSize );
        *pInOutSize = static_cast<uint32>( totalSize );
        return true;
    }

    bool GameInstanceBase::deserializeState( const void* pInBuffer, uint32 size )
    {
        if ( pInBuffer == nullptr || size < sizeof( uint32 ) )
            return false;

        uint32 magic = 0;
        Memory::copy( &magic, pInBuffer, sizeof( uint32 ) );

        // 구버전/레거시 포맷 폴백
        if ( magic != StateEnvelopeInternal::kMagic )
        {
            const bool bOk = deserializeSceneObjects( static_cast<const uint8*>( pInBuffer ), size );
            onAfterStateDeserialize();
            return bOk;
        }

        if ( size < sizeof( uint32 ) * 4 )
            return false;

        Archive arch( static_cast<const uint8*>( pInBuffer ), size );
        arch >> magic;

        uint32 version = 0;
        arch >> version;

        // 1) 씬 오브젝트 복원
        vector<uint8> bytesScene;
        if ( arch.readSection( bytesScene ) == false )
            return false;

        if ( bytesScene.empty() == false && deserializeSceneObjects( bytesScene.data(), bytesScene.size() ) == false )
            return false;

        // 2) 파생 클래스 커스텀 리플렉션 상태 복원
        vector<uint8> bytesState;
        if ( arch.readSection( bytesState ) == false )
            return false;

        if ( bytesState.empty() == false )
        {
            const TypeInfo* pStateTypeInfo = getStateTypeInfo();
            void*           pStateInstance = getStateInstance();
            if ( pStateTypeInfo != nullptr && pStateInstance != nullptr )
            {
                if ( BinarySerializer::deserialize( pStateInstance, *pStateTypeInfo, bytesState.data(), bytesState.size() ) == false )
                    return false;
            }
        }

        if ( arch.isError() )
            return false;

        onAfterStateDeserialize();
        return true;
    }

    bool GameInstanceBase::captureSnapshot( vector<uint8>& outBytes )
    {
        uint32 size = 0;
        if ( serializeState( nullptr, &size ) == false || size == 0 )
            return false;

        outBytes.resize( size );
        return serializeState( outBytes.data(), &size );
    }

    bool GameInstanceBase::restoreSnapshot( const vector<uint8>& inBytes )
    {
        if ( inBytes.empty() )
            return false;
        return deserializeState( inBytes.data(), static_cast<uint32>( inBytes.size() ) );
    }

    bool GameInstanceBase::saveStateToFile( string_view filePath )
    {
        vector<uint8> snapshotBytes;
        if ( captureSnapshot( snapshotBytes ) == false )
            return false;
        return FileUtil::writeFile( filePath, snapshotBytes.data(), snapshotBytes.size() );
    }

    bool GameInstanceBase::loadStateFromFile( string_view filePath )
    {
        vector<uint8> snapshotBytes;
        if ( FileUtil::readFile( filePath, snapshotBytes ) == false )
            return false;
        return restoreSnapshot( snapshotBytes );
    }
} // namespace sw
