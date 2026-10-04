#include "pch.h"

#include "GameFramework/Framework/GameInstanceBase.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Data/GameStrings.h"
#include "GameFramework/Framework/ComponentStateStore.h"
#include "GameFramework/Framework/GameEventUtil.h"
#include "GameFramework/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct StateEnvelopeInternal
        {
            static constexpr uint32 kMagic = 0x53575354u; // 'SWST' (SW State Snapshot)
            /**
             * @brief 봉투 버전입니다. 읽기도 이 판만 받습니다. 머리에 프로세스 토큰이 있고, 씬 섹션의 오브젝트마다 런타임 id 가 상태 앞에 실립니다.
             *        섹션은 씬 오브젝트 · 파생의 리플렉션 상태 · 컴포넌트 상태(`ComponentStateStore`) 셋입니다.
             * @details 토큰이 지금 프로세스와 같으면(핫 리로드) id 를 되살리고, 다르면(다른 실행의 세이브 파일) 읽고 버립니다.
             *          다른 실행에서 나간 id 를 되살리면 이 실행에서 이미 나간 id 와 겹칠 수 있기 때문입니다.
             */
            static constexpr uint32 kVersion = 3;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameInstanceBase" );

    GameInstanceBase::GameInstanceBase()
        : _bootstrap{}
        , _pWindow{ nullptr }
        , _pRhiDevice{ nullptr }
        , _listPendingSceneLoad{}
        , _pComponentStateStore{ make_unique<ComponentStateStore>() }
        , _bResumingWorld{ SW_FALSE }
    {
    }

    GameInstanceBase::~GameInstanceBase() = default;

    GameObjectManager* GameInstanceBase::findActiveObjectManager()
    {
        // `areGameServicesBound()` 가 바로 이 서비스(SceneManager 슬롯)를 보므로 아래 널 검사는 사실상 닿지 않지만,
        // `game::getService<T>()` 가 nullptr 을 반환할 수 있는 함수라 `CheckNullableServiceUse` 린트가 요구하는 모양을 지킨다.
        if ( game::areGameServicesBound() == false )
            return nullptr;
        SceneManager* pSceneManager = game::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return nullptr;
        Scene* pActiveScene = pSceneManager->getActiveScene();
        return ( pActiveScene != nullptr ) ? pActiveScene->getObjectManager() : nullptr;
    }

    bool GameInstanceBase::initialize( IWindow* pWindow, IRHIDevice* pRhiDevice )
    {
        _pWindow        = pWindow;
        _pRhiDevice     = pRhiDevice;
        _bResumingWorld = findActiveObjectManager() != nullptr ? SW_TRUE : SW_FALSE;
        configureBootstrap( _bootstrap );
        const GameConfig& gameCfg = GameConfig::getActive();
        if ( gameCfg._packRoot.empty() == false )
            _bootstrap._packRoot = gameCfg._packRoot;
        const string_view gameSettingsFile =
            gameCfg._gameSettingsFile.empty() ? string_view( "data/gamesettings.xml" ) : string_view( gameCfg._gameSettingsFile );
        if ( _bootstrap.load( gameSettingsFile ) == false )
            SW_LOG_TRACE( "No custom bootstrap in pack '%#' — using defaults.", _bootstrap._packRoot );
        game::bindLocalService<GameSettings>( &_bootstrap._data );
        applyBootstrap();
        const bool bInitialized = onInitialize();

        // 플레이어 설정을 다시 넣는다 — 언어 팩 · 입력 맵(키 바인딩 · 누르기/토글)은 위에서 막 생겼다. 엔진 기동 때의 적용은 그 대상이 없을 때였다.
        // 액션을 더 늦게(컴포넌트 시작 때) 만드는 게임은 그 뒤에 `UserSettingsManager::reapplyAll` 을 부른다.
        UserSettingsManager* pUserSettings = game::getService<UserSettingsManager>();
        if ( pUserSettings != nullptr )
            pUserSettings->reapplyAll();
        return bInitialized;
    }

    void GameInstanceBase::shutdown()
    {
        onShutdown();
        // 다른 인스턴스(리로드가 먼저 만든 새 것)가 묶은 것은 건드리지 않는다.
        if ( game::getService<GameSettings>() == &_bootstrap._data )
            game::unbindLocalService<GameSettings>();
        _listPendingSceneLoad.clear();
        _pWindow    = nullptr;
        _pRhiDevice = nullptr;
    }

    void GameInstanceBase::applyBootstrap()
    {
        const GameSettings& data = _bootstrap._data;

        // 다국어 — 게임의 로컬라이제이션 프로젝트를 올린다(엔진 프로젝트는 엔진이 기동 때 올렸다).
        if ( data._localizationProject.empty() == false && GameStrings::initialize( data._localizationProject, data._defaultLanguage, data._fallbackLanguage ) == false )
            SW_LOG_WARNING( "GameSettings localization project '%#' could not be loaded - strings show their keys", data._localizationProject.c_str() );

        // 게임플레이 입력 맵 — `InputManager::beginFrame` 이 프레임마다 갱신하는 통합 맵(`PlayerController` 가 읽는 맵)이다.
        if ( data._inputMap.empty() == false )
        {
            InputManager* pInput = game::getService<InputManager>();
            if ( pInput == nullptr )
                SW_LOG_WARNING( "GameSettings input map '%#' is not loaded - no InputManager service", data._inputMap.c_str() );
            else if ( pInput->getInputMap().loadFromResource( data._inputMap ) == false )
                SW_LOG_WARNING( "GameSettings input map '%#' could not be loaded - gameplay actions keep their current bindings", data._inputMap.c_str() );
        }
    }

    /**
     * @brief `"-gv_firstScene=<리소스 경로>"` — 실행 설정의 시작 씬 대신 이 씬을 처음 엽니다(에디터 없이 다른 씬을 띄워 보는 자리 — 환경 쇼케이스).
     * @note PowerShell 은 점이 든 인자를 쪼갠다 — 따옴표로 감쌀 것.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( sw::string, gv_firstScene, "", "실행 설정의 시작 씬 대신 처음 열 씬의 리소스 경로 (비우면 사용 안 함)" );

    const string& GameInstanceBase::getFirstScene() const
    {
        if ( gv_firstScene.empty() == false )
            return gv_firstScene;
        const string& runScene = GameConfig::getActive()._startupScene;
        if ( runScene.empty() == false )
            return runScene;
        return _bootstrap._data._titleScene.empty() ? _bootstrap._data._startMap : _bootstrap._data._titleScene;
    }

    const string& GameInstanceBase::getEntranceScene() const
    {
        return _bootstrap._data._entranceScene.empty() ? _bootstrap._data._startMap : _bootstrap._data._entranceScene;
    }

    bool GameInstanceBase::requestFirstScene()
    {
        if ( _bResumingWorld == SW_TRUE )
        {
            SW_LOG_INFO( "The game instance was recreated over a live scene - keeping it instead of loading the first scene" );
            return false;
        }
        return requestSceneLoad( getFirstScene(), "first" );
    }

    bool GameInstanceBase::requestEntranceScene()
    {
        return requestSceneLoad( getEntranceScene(), "entrance" );
    }

    bool GameInstanceBase::requestSceneLoad( const string& scenePath, const utf8* pWhich )
    {
        if ( scenePath.empty() )
            return false;
        SceneManager* pSceneManager = game::getService<SceneManager>();
        if ( pSceneManager == nullptr )
        {
            SW_LOG_WARNING( "The %# scene '%#' is not opened - no SceneManager service", pWhich, scenePath.c_str() );
            return false;
        }
        TaskFuture<Scene*> future = pSceneManager->requestLoadFuture( scenePath );
        if ( future.isValid() == false )
        {
            SW_LOG_ERROR( "The %# scene '%#' could not be requested", pWhich, scenePath.c_str() );
            return false;
        }
        _listPendingSceneLoad.push_back( PendingSceneLoad{ scenePath, std::move( future ) } );

        SceneLoadRequestedEvent event{};
        event._levelName = scenePath;
        GameEventUtil::send( event );
        return true;
    }

    void GameInstanceBase::publishFinishedSceneLoads()
    {
        // 끝난 것만 순서를 지키며 뺀다. 대기열에서 밀린 요청은 nullptr 로 끝나 실패로 알린다.
        size_t keptCount = 0;
        for ( size_t index = 0; index < _listPendingSceneLoad.size(); ++index )
        {
            PendingSceneLoad& pending = _listPendingSceneLoad[index];
            if ( pending._future.isReady() == false )
            {
                if ( keptCount != index )
                    _listPendingSceneLoad[keptCount] = std::move( pending );
                ++keptCount;
                continue;
            }
            SceneLoadCompletedEvent event{};
            event._levelName = pending._scenePath;
            event._bSuccess  = ( pending._future.get() != nullptr );
            GameEventUtil::send( event );
        }
        _listPendingSceneLoad.resize( keptCount );
    }

    void GameInstanceBase::update( float32 deltaTime )
    {
        if ( _listPendingSceneLoad.empty() == false )
            publishFinishedSceneLoads();
        onUpdate( deltaTime );
    }

    bool GameInstanceBase::serializeSceneObjects( vector<uint8>& outBytes )
    {
        GameObjectManager* pObjectManager = findActiveObjectManager();
        if ( pObjectManager == nullptr )
            return false;

        // 살아 있는 것만 담는다. `forEachGameObject` 는 파괴 대기 오브젝트를 이미 건너뛴다(값 반환 목록을 받아 다시 거를 일이 없다).
        vector<GameObject*> listValidObject;
        pObjectManager->forEachGameObject( [&listValidObject]( GameObject* pObj )
        { listValidObject.push_back( pObj ); } );

        outBytes.clear();

        // 게임오브젝트 개수를 맨 앞에 기록
        const uint32 count   = static_cast<uint32>( listValidObject.size() );
        const size_t oldSize = outBytes.size();
        outBytes.resize( oldSize + sizeof( uint32 ) );
        Memory::copy( outBytes.data() + oldSize, &count, sizeof( uint32 ) );

        for ( GameObject* pObj : listValidObject )
        {
            ObjectStateSerializer::writeIdentity( ObjectStateSerializer::captureIdentity( pObj ), outBytes );
            // 개수를 이미 앞에 적었다 — 하나라도 못 쓰면 개수와 본문이 어긋나므로 통째로 실패한다(버리고 성공이라 하지 않는다).
            if ( ObjectStateSerializer::saveToBinaryBuffer( pObj, outBytes ) == false )
            {
                SW_LOG_ERROR( "Scene object '%#' could not be serialized - the snapshot is not taken", pObj->getName().c_str() );
                outBytes.clear();
                return false;
            }
        }

        return true;
    }

    bool GameInstanceBase::deserializeSceneObjects( const uint8* pData, size_t size, SceneObjectFormat format )
    {
        if ( pData == nullptr || size < sizeof( uint32 ) )
            return false;

        GameObjectManager* pObjectManager = findActiveObjectManager();
        if ( pObjectManager == nullptr )
            return false;

        // 기존 엔티티들 정리
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

        // 모든 게임오브젝트를 만들고 읽은 뒤 묶음이 계층을 한 번에 잇는다. 같은 프로세스의 스냅샷이면 원래 id 로 만들고 컴포넌트 id 도
        // 되살린다. 부착은 상태의 부착 필드(부모의 **id** · 컴포넌트 키)로 잇는다 — 주의: 부모 이름으로 찾아 부모의 primary 에 붙이면
        // 부모보다 먼저 읽힌 소켓 자식(손에 든 무기)이 몸통으로 옮겨 간다. 다른 실행의 세이브는 id 를 되살리지 않지만, 스트림에 적힌
        // 그때의 id 로 묶음 안에서 찾는다.
        // **하나라도 못 읽으면 실패다.** 주의: 멈추고도 true 를 돌려주면 핫 리로드가 스냅샷을 버리고 저장 막기를 푼다 — 씬은 이미
        // 비운 뒤라 못 읽은 오브젝트부터 뒤가 사라진 채 저장할 수 있다.
        const bool       bRestoreIdentity = ( format == SceneObjectFormat::RestoreIdentity );
        bool             bComplete        = true;
        ObjectStateBatch batch( bRestoreIdentity ? ObjectIdSpace::Live : ObjectIdSpace::Saved );
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

            GameObject*       pObj = bRestoreIdentity ? pObjectManager->createGameObjectWithId( hashed_string( "GameObject" ), identity._objectId )
                                                      : pObjectManager->createGameObject();
            ObjectLoadContext context{};
            context._pIdentity     = bRestoreIdentity ? &identity : nullptr;
            context._pBatch        = &batch;
            context._savedId       = identity._objectId;
            const size_t readBytes = ObjectStateSerializer::loadFromBinaryBuffer( pObj, pData + offset, size - offset, context );
            if ( readBytes == 0 )
            {
                SW_LOG_ERROR( "Failed to load binary object state at index %u", objectIndex );
                pObjectManager->destroyObject( pObj ); // 읽지 못한 자리에 빈 오브젝트를 남기지 않는다
                bComplete = false;
                break;
            }
            listRestoredObject.push_back( pObj );
            offset += readBytes;
        }

        // 씬 계층 구조(부모 · 소켓)를 한 번에 잇는다 — 자식이 부모보다 먼저 읽혔어도 부모를 찾는다.
        batch.finish();

        // 복원된 모든 오브젝트들의 월드 매트릭스를 강제 동기화
        pObjectManager->flushSceneTransforms();

        if ( bComplete == false )
        {
            SW_LOG_ERROR( "Scene restore stopped after %# of %# objects - the snapshot is kept and saving stays blocked",
                          static_cast<uint32>( listRestoredObject.size() ), count );
            return false;
        }
        return true;
    }

    bool GameInstanceBase::serializeState( void* pOutBuffer, uint32* pInOutSize )
    {
        if ( pInOutSize == nullptr )
            return false;

        _pComponentStateStore->clear();
        onBeforeStateSerialize();

        Archive arch;
        arch << StateEnvelopeInternal::kMagic;
        arch << StateEnvelopeInternal::kVersion;
        arch << ObjectStateSerializer::getProcessToken();

        // 1) 씬 오브젝트 바이너리 스냅샷
        //
        // **씬이 없는 것은 실패가 아니다.** 씬 없이 커스텀 상태만 스냅샷하는 것은 지원되는 사용법이라
        // (GameFrameworkTest.GameInstanceBaseSnapshotAndFileRoundTrip 이 그렇게 쓴다) 여기서 끊으면 안 된다.
        // 다만 반환값을 흔적 없이 버리면 씬이 있어야 할 상황에서 오브젝트가 하나도 없는 세이브가 나와도
        // 로드할 때까지 아무도 모른다. 빈 섹션은 그대로 쓰되 실마리는 남긴다.
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
            // serialize 는 void 라 성공 여부를 반환하지 않는다. 결과가 비면 **단정하지 않고 남긴다.**
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

        // 3) PROPERTY 가 아닌 컴포넌트 상태(디렉터의 시뮬레이션) — 훅이 실어 둔 것
        Archive componentArch;
        _pComponentStateStore->write( componentArch );
        arch.writeSection( componentArch.getData(), static_cast<uint32>( componentArch.getSize() ) );
        _pComponentStateStore->clear();

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

        if ( magic != StateEnvelopeInternal::kMagic )
        {
            SW_LOG_ERROR( "State snapshot has no SWST header - not written by GameInstanceBase::serializeState" );
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

        if ( bytesScene.empty() == false && deserializeSceneObjects( bytesScene.data(), bytesScene.size(), format ) == false )
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

        // 3) 컴포넌트 상태 — 훅이 다시 만든 컴포넌트에 넘긴다. 못 읽으면 알리고 넘어간다(씬은 이미 섰다 — 디렉터는 새 판으로 시작한다).
        vector<uint8> bytesComponent;
        if ( arch.readSection( bytesComponent ) == false )
            return false;
        _pComponentStateStore->clear();
        if ( bytesComponent.empty() == false )
        {
            Archive componentArch( bytesComponent.data(), bytesComponent.size() );
            if ( _pComponentStateStore->read( componentArch ) == false )
                SW_LOG_WARNING( "Component state section could not be read - components start fresh" );
        }

        if ( arch.isError() )
            return false;

        onAfterStateDeserialize();
        _pComponentStateStore->clear();
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
        const string_view path = filePath.empty() ? string_view( _bootstrap._data._defaultSavePath ) : filePath;
        if ( path.empty() )
        {
            SW_LOG_ERROR( "No save path was given and GameSettings has no defaultSavePath - nothing is saved" );
            return false;
        }
        vector<uint8> snapshotBytes;
        const bool    bSaved = captureSnapshot( snapshotBytes ) && FileUtil::writeFile( path, snapshotBytes.data(), snapshotBytes.size() );

        SaveGameSavedEvent event{};
        event._savePath = string( path );
        event._bSuccess = bSaved;
        GameEventUtil::send( event );
        return bSaved;
    }

    bool GameInstanceBase::loadStateFromFile( string_view filePath )
    {
        const string_view path = filePath.empty() ? string_view( _bootstrap._data._defaultSavePath ) : filePath;
        if ( path.empty() )
        {
            SW_LOG_ERROR( "No save path was given and GameSettings has no defaultSavePath - nothing is loaded" );
            return false;
        }
        vector<uint8> snapshotBytes;
        const bool    bLoaded = FileUtil::readFile( path, snapshotBytes ) && restoreSnapshot( snapshotBytes );

        SaveGameLoadedEvent event{};
        event._savePath = string( path );
        event._bSuccess = bLoaded;
        GameEventUtil::send( event );
        return bLoaded;
    }
} // namespace sw
