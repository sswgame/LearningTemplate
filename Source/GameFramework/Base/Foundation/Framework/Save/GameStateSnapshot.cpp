#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Save/GameStateSnapshot.h"

#include "Core/Memory/Memory.h"
#include "Core/String/FourCcUtil.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct StateEnvelopeInternal
        {
            static constexpr uint32 kMagic = FourCcUtil::make( "SWST" );
            /**
             * @brief 스냅샷 봉투의 버전입니다. 읽을 때도 이 버전만 받습니다.
             * @details 봉투 머리에 프로세스 토큰이 있고, 씬 섹션의 오브젝트마다 런타임 id 가 상태 앞에 실린다.
             *          섹션은 씬 오브젝트, 파생의 리플렉션 상태, 컴포넌트 상태(`ComponentStateStore`) 셋이다.
             *          토큰이 지금 프로세스와 같으면(핫 리로드) id 를 복원하고, 다르면(다른 실행의 세이브 파일) id 를 읽고 버린다.
             *          다른 실행에서 나간 id 를 복원하면 이 실행에서 이미 나간 id 와 겹칠 수 있기 때문이다.
             */
            static constexpr uint32 kVersion = 4; // 4: 오브젝트 id 묶음에 컴포넌트 안정 키가 실린다
        };

        struct GameStateSnapshotInternal
        {
            /** @brief 게임 창구로 찾은 씬 매니저의 활성 씬 오브젝트 매니저입니다. 씬 매니저가 없거나 활성 씬이 없으면 nullptr 입니다. */
            static GameObjectManager* findActiveObjectManager()
            {
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                return pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameStateSnapshot" );

    GameStateSnapshot::GameStateSnapshot()
        : _listStatefulType{}
        , _pComponentStateStore{ make_unique<ComponentStateStore>() }
    {
        // 엔진 컴포넌트 중 PROPERTY 밖의 편집 상태를 가진 것 — 지형의 저장하지 않은 높이 · 스플랫이 모듈 리로드와 백엔드 교체를 넘어간다.
        registerStatefulComponent<TerrainComponent>();
        // 프레임워크 컴포넌트(카메라 디렉터 등)는 정의한 쪽이 정적으로 등록한다 — 게임마다 올리지 않아도 모든 게임의 스냅샷에 실린다.
        for ( const StatefulComponentRegistration* pRegistration : StatefulComponentRegistration::getRegistrations() )
        {
            StatefulComponentType type;
            type._pCapture = pRegistration->getCapture();
            type._pRestore = pRegistration->getRestore();
            _listStatefulType.push_back( type );
        }
    }

    GameStateSnapshot::~GameStateSnapshot() = default;

    void GameStateSnapshot::beginCapture()
    {
        _pComponentStateStore->clear();
        GameObjectManager* pManager = GameStateSnapshotInternal::findActiveObjectManager();
        if ( pManager == nullptr )
            return;
        // 모두 싣고 나서 걷는다. 걷기가 다른 타입의 상태(플레이어가 읽는 디렉터)를 바꾸지 않게 하려는 순서다.
        for ( const StatefulComponentType& type : _listStatefulType )
        {
            if ( type._pCapture != nullptr )
                type._pCapture( *_pComponentStateStore, *pManager );
        }
        for ( const StatefulComponentType& type : _listStatefulType )
        {
            if ( type._pDespawn != nullptr )
                type._pDespawn( *pManager );
        }
    }

    bool GameStateSnapshot::writeEnvelope( SerializePurpose purpose, const TypeInfo* pStateType, const void* pStateInstance, vector<uint8>& outBytes )
    {
        Archive arch;
        arch << StateEnvelopeInternal::kMagic;
        arch << StateEnvelopeInternal::kVersion;
        arch << ObjectStateSerializer::getProcessToken();

        // 1) 씬 오브젝트 바이너리 스냅샷
        //
        // **씬이 없는 것은 실패가 아니다.** 씬 없이 커스텀 상태만 스냅샷하는 것은 지원하는 사용법이다
        // (GameFrameworkTest.GameInstanceSnapshotAndFileRoundTrip 이 그렇게 쓴다). 그러니 여기서 끊으면 안 된다.
        // 다만 반환값을 흔적 없이 버리면, 씬이 있어야 할 상황에서 오브젝트가 하나도 없는 세이브가 나와도
        // 로드할 때까지 아무도 모른다. 빈 섹션은 그대로 쓰되 단서는 남긴다(지원하는 사용법이라 Warning 이 아니라 Info).
        vector<uint8> bytesScene;
        if ( serializeSceneObjects( purpose, bytesScene ) == false )
            SW_LOG_INFO( "The scene object snapshot is empty - there is no active scene or the game service is not bound. Only the custom state is saved." );
        arch.writeSection( bytesScene.data(), static_cast<uint32>( bytesScene.size() ) );

        // 2) 파생 클래스 커스텀 리플렉션 상태 스냅샷
        if ( pStateType != nullptr && pStateInstance != nullptr )
        {
            // 값 하나라도 싣지 못하면 세이브 전체를 실패로 돌려준다. 써 두면 로드에서 그 값이 빠진다. 이유는 직렬화기가 알린다.
            vector<uint8> bytesState;
            if ( BinarySerializer::serialize( pStateInstance, *pStateType, bytesState, SerializeContext::getDefault( purpose ) ) == false )
            {
                _pComponentStateStore->clear();
                return false;
            }
            arch.writeSection( bytesState.data(), static_cast<uint32>( bytesState.size() ) );
        }
        else
        {
            arch.writeSection( nullptr, 0 );
        }

        // 3) PROPERTY 가 아닌 컴포넌트 상태(디렉터의 시뮬레이션). 훅이 실어 둔 것을 쓴다.
        Archive componentArch;
        _pComponentStateStore->write( componentArch );
        arch.writeSection( componentArch.getData(), static_cast<uint32>( componentArch.getSize() ) );
        _pComponentStateStore->clear();

        // 봉투는 한 번 써서 그대로 넘긴다.
        outBytes.assign( arch.getData(), arch.getData() + arch.getSize() );
        return true;
    }

    bool GameStateSnapshot::readEnvelope( SerializePurpose purpose, const TypeInfo* pStateType, void* pStateInstance, const void* pInBuffer, uint32 size )
    {
        if ( pInBuffer == nullptr || size < sizeof( uint32 ) )
            return false;

        uint32 magic = 0;
        Memory::copy( &magic, pInBuffer, sizeof( uint32 ) );

        if ( magic != StateEnvelopeInternal::kMagic )
        {
            SW_LOG_ERROR( "State snapshot has no SWST header - not written by GameStateSnapshot::writeEnvelope" );
            return false;
        }

        if ( size < sizeof( uint32 ) * 4 )
            return false;

        Archive arch( static_cast<const uint8*>( pInBuffer ), size );
        arch >> magic;

        uint32 version = 0;
        arch >> version;
        if ( version != StateEnvelopeInternal::kVersion )
        {
            SW_LOG_ERROR( "State snapshot version %# is not this build's %#", version, StateEnvelopeInternal::kVersion );
            return false;
        }

        uint64 processToken = 0;
        arch >> processToken;
        const bool              bSameProcess = ( processToken == ObjectStateSerializer::getProcessToken() );
        const SceneObjectFormat format       = bSameProcess ? SceneObjectFormat::RestoreIdentity : SceneObjectFormat::WithIdentity;

        // 1) 씬 오브젝트 복원
        vector<uint8> bytesScene;
        if ( arch.readSection( bytesScene ) == false )
            return false;

        vector<ComponentStateStore::SavedComponentID> listComponentIDRemap;
        if ( bytesScene.empty() == false && deserializeSceneObjects( purpose, bytesScene.data(), bytesScene.size(), format, listComponentIDRemap ) == false )
            return false;

        // 2) 파생 클래스 커스텀 리플렉션 상태 복원
        vector<uint8> bytesState;
        if ( arch.readSection( bytesState ) == false )
            return false;

        if ( bytesState.empty() == false && pStateType != nullptr && pStateInstance != nullptr )
        {
            if ( BinarySerializer::deserialize( pStateInstance, *pStateType, bytesState.data(), bytesState.size(), SerializeContext::getDefault( purpose ) ) == false )
                return false;
        }

        // 3) 컴포넌트 상태는 훅이 다시 만든 컴포넌트에 넘긴다. 못 읽으면 알리고 넘어간다. 씬은 이미 섰고, 디렉터는 새로 시작한다.
        vector<uint8> bytesComponent;
        if ( arch.readSection( bytesComponent ) == false )
            return false;
        _pComponentStateStore->clear();
        if ( bytesComponent.empty() == false )
        {
            Archive componentArch( bytesComponent.data(), bytesComponent.size() );
            if ( _pComponentStateStore->read( componentArch ) == false )
                SW_LOG_WARNING( "Component state section could not be read - components start fresh" );
            // 다른 실행의 세이브는 컴포넌트 id 가 새로 나갔다. 씬 섹션이 만든 표로 저장된 id 를 이번 id 로 바꿔 id 로만 짝짓는다.
            if ( format == SceneObjectFormat::WithIdentity )
                _pComponentStateStore->remapComponentIDs( listComponentIDRemap );
        }

        if ( arch.isError() )
            return false;

        // 읽기 훅(`onAfterStateDeserialize`)은 부르는 쪽이 이 뒤에 부르고 `endRestore` 로 저장소를 비운다.
        restoreStatefulComponents();
        return true;
    }

    void GameStateSnapshot::endRestore()
    {
        _pComponentStateStore->clear();
    }

    void GameStateSnapshot::restoreStatefulComponents()
    {
        GameObjectManager* pManager = GameStateSnapshotInternal::findActiveObjectManager();
        if ( pManager == nullptr )
            return;
        for ( const StatefulComponentType& type : _listStatefulType )
        {
            if ( type._pRestore != nullptr )
                type._pRestore( *_pComponentStateStore, *pManager );
        }
    }

    bool GameStateSnapshot::serializeSceneObjects( SerializePurpose purpose, vector<uint8>& outBytes )
    {
        GameObjectManager* pObjectManager = GameStateSnapshotInternal::findActiveObjectManager();
        if ( pObjectManager == nullptr )
            return false;

        // 살아 있는 것만 담는다. `forEachGameObject` 가 파괴 대기 오브젝트를 이미 건너뛰므로 목록을 받아 다시 거를 일이 없다.
        vector<GameObject*> listValidObject;
        pObjectManager->forEachGameObject( [&listValidObject]( GameObject* pObj )
        { listValidObject.push_back( pObj ); } );

        outBytes.clear();

        // 게임오브젝트 개수를 맨 앞에 쓴다.
        const uint32 count   = static_cast<uint32>( listValidObject.size() );
        const size_t oldSize = outBytes.size();
        outBytes.resize( oldSize + sizeof( uint32 ) );
        Memory::copy( outBytes.data() + oldSize, &count, sizeof( uint32 ) );

        for ( GameObject* pObj : listValidObject )
        {
            ObjectStateSerializer::writeIdentity( ObjectStateSerializer::captureIdentity( pObj ), outBytes );
            // 개수를 이미 앞에 적었으므로 하나라도 못 쓰면 개수와 본문이 어긋난다. 그래서 통째로 실패로 처리한다.
            if ( ObjectStateSerializer::saveToBinaryBuffer( pObj, outBytes, ObjectSaveOptions{ purpose } ) == false )
            {
                SW_LOG_ERROR( "Scene object '%#' could not be serialized - the snapshot is not taken", pObj->getName().c_str() );
                outBytes.clear();
                return false;
            }
        }

        return true;
    }

    bool GameStateSnapshot::deserializeSceneObjects( SerializePurpose purpose, const uint8* pData, size_t size, SceneObjectFormat format,
                                                     vector<ComponentStateStore::SavedComponentID>& outListRemap )
    {
        outListRemap.clear();
        if ( pData == nullptr || size < sizeof( uint32 ) )
            return false;

        GameObjectManager* pObjectManager = GameStateSnapshotInternal::findActiveObjectManager();
        if ( pObjectManager == nullptr )
            return false;

        // 기존 오브젝트를 정리한다.
        pObjectManager->clear();

        size_t offset = 0;
        uint32 count  = 0;
        Memory::copy( &count, pData + offset, sizeof( uint32 ) );
        offset += sizeof( uint32 );

        vector<GameObject*> listRestoredObject;

        // **파일이 말한 개수를 그대로 잡아 두지 않는다.** 오브젝트 하나는 적어도 길이 4바이트를
        // 쓰므로, 남은 바이트 / 4 보다 많은 오브젝트는 어떤 스냅샷에도 있을 수 없다. 아래 읽기는
        // 잘린 데이터에서 어차피 멈추지만, **그 전에 이 `reserve` 가 먼저 터진다.** `count` 가
        // 40억이면 이 한 줄이 수십 기가를 요구한다. `SceneDocument::loadBinary` 가 같은 이유로
        // 같은 계산을 한다.
        constexpr size_t kMinBytesPerObject = sizeof( uint32 );
        const size_t     maxPossibleObject  = ( size - offset ) / kMinBytesPerObject;
        listRestoredObject.reserve( MathUtil::min( static_cast<size_t>( count ), maxPossibleObject ) );

        // 모든 게임오브젝트를 만들고 읽은 뒤에 묶음이 계층을 한 번에 잇는다.
        // 같은 프로세스의 스냅샷이면 원래 id 로 만들고 컴포넌트 id 도 복원한다.
        // 부착은 상태의 부착 필드(부모의 **id** 와 컴포넌트 키)로 잇는다. 부모 이름으로 찾아 부모의 primary 에 붙이면
        // 부모보다 먼저 읽힌 소켓 자식(손에 든 무기)이 몸통으로 옮겨 가므로 깨진다.
        // 다른 실행의 세이브는 id 를 복원하지 않고, 스트림에 적힌 그때의 id 로 묶음 안에서 부모를 찾는다.
        // **하나라도 못 읽으면 실패다.** 읽기를 멈추고도 true 를 돌려주면 핫 리로드가 스냅샷을 버리고 저장 막기를 풀어 버린다.
        // 씬은 이미 비운 뒤라서, 못 읽은 오브젝트부터 뒤가 사라진 채 저장될 수 있다.
        const bool       bRestoreIdentity = ( format == SceneObjectFormat::RestoreIdentity );
        bool             bComplete        = true;
        ObjectStateBatch batch( bRestoreIdentity ? ObjectIDSpace::Live : ObjectIDSpace::Saved );
        for ( uint32 objectIndex = 0; objectIndex < count; ++objectIndex )
        {
            ObjectIdentity identity;
            const size_t   identityBytes = ObjectStateSerializer::readIdentity( pData + offset, size - offset, identity );
            if ( identityBytes == 0 )
            {
                SW_LOG_ERROR( "Failed to read object identity at index %u", objectIndex );
                bComplete = false;
                break;
            }
            offset += identityBytes;

            GameObject*       pObj = bRestoreIdentity ? pObjectManager->createGameObjectWithID( hashed_string( GameObject::kDefaultName ), identity._objectID )
                                                      : pObjectManager->createGameObject();
            ObjectLoadContext context{ purpose, bRestoreIdentity ? &identity : nullptr };
            context._pBatch        = &batch;
            context._savedID       = identity._objectID;
            const size_t readBytes = ObjectStateSerializer::loadFromBinaryBuffer( pObj, pData + offset, size - offset, context );
            if ( readBytes == 0 )
            {
                SW_LOG_ERROR( "Failed to load binary object state at index %u", objectIndex );
                pObjectManager->destroyObject( pObj ); // 읽지 못한 자리에 빈 오브젝트를 남기지 않는다.
                bComplete = false;
                break;
            }
            listRestoredObject.push_back( pObj );
            offset += readBytes;
            if ( bRestoreIdentity == false )
                ComponentStateStore::appendComponentIDRemap( identity, *pObj, outListRemap );
        }

        // 씬 계층 구조(부모와 소켓)를 한 번에 잇는다. 자식이 부모보다 먼저 읽혔어도 부모를 찾을 수 있다.
        // 옮기지 못한 핸들 PROPERTY(다른 실행의 세이브)는 로드 오류다 — 비우고 이어 가면 값이 조용히 사라진다.
        bComplete = batch.finish() && bComplete;

        // 복원된 모든 오브젝트의 월드 매트릭스를 강제로 동기화한다.
        pObjectManager->flushSceneTransforms();

        if ( bComplete == false )
        {
            SW_LOG_ERROR( "Scene restore stopped after %# of %# objects - the snapshot is kept and saving stays blocked",
                          static_cast<uint32>( listRestoredObject.size() ), count );
            return false;
        }
        return true;
    }
} // namespace sw
