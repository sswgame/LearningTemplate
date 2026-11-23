#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/GameStateSnapshot.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/ReflectedInstance.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDataLayerSubsystem.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"
#include "Engine/Scene/SceneSubLevelSubsystem.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Inherit/AssetRedirect.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/IContentConfiguration.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/SnapshotFaultHook.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/StatefulComponentRegistrar.h"

namespace sw
{
    namespace
    {
        struct StateEnvelopeInternal
        {
            static constexpr uint32 kMagic = FourCCUtil::make( "SWST" );
            /**
             * @brief 스냅샷 봉투의 버전입니다. 읽을 때도 이 버전만 받습니다.
             * @details 봉투 머리에 프로세스 토큰이 있고, 씬 섹션의 오브젝트마다 런타임 id 가 상태 앞에 실린다.
             *          섹션은 씬 오브젝트, 파생의 리플렉션 상태, 컴포넌트 상태(`ComponentStateStore`), 씬 몫(늘 올라 있는 오브젝트의 파일 id · 스트리밍 셀 상태 · 데이터 레이어 상태) 넷이다.
             *          토큰이 지금 프로세스와 같으면(핫 리로드) id 를 복원하고, 다르면(다른 실행의 세이브 파일) id 를 읽고 버린다.
             *          다른 실행에서 나간 id 를 복원하면 이 실행에서 이미 나간 id 와 겹칠 수 있기 때문이다.
             *          머리의 씬 정체는 찍을 때 활성 씬의 리소스 id(`Scene::getSourcePath`)다 — 읽을 때 지금 활성 씬과 다르면 섞지 않고 거절한다.
             */
            static constexpr uint32 kVersion = 8; // 5: 머리에 콘텐츠 구성(켠 모드) 글이 실린다, 6: 머리에 씬 정체(활성 씬 리소스 id)가 실린다, 7: 넷째 섹션 씬 몫(파일 id · 스트리밍 셀), 8: 씬 몫에 런타임 하위 레벨

            /** @brief 봉투 머리(매직 · 판 뒤의 칸)입니다. */
            struct Header
            {
                uint64 _processToken{ 0 }; ///< 찍은 프로세스의 토큰 — 같으면 id 를 복원한다
                string _sceneID;           ///< 찍을 때 활성 씬의 지금 리소스 id(옮긴 씬의 옛 id 는 읽을 때 푼다 — 씬이 없었으면 빈 글)
                string _contentText;       ///< 찍을 때 콘텐츠 구성(켠 모드) 글
            };

            /**
             * @brief @p pInBuffer 의 머리를 읽어 @p outHeader 를 채우고 @p arch 를 섹션 앞에 둡니다. 머리가 없거나 판이 다르거나 잘렸으면 false 입니다(머리 · 판은 까닭을 남긴다).
             * @details 봉투 읽기와 머리만 읽기(`readSceneID`)가 같은 길을 쓴다 — 머리 형식을 두 곳에서 따로 읽지 않는다.
             */
            static bool readHeader( const void* pInBuffer, uint32 size, Archive& arch, Header& outHeader )
            {
                if ( pInBuffer == nullptr || size < sizeof( uint32 ) )
                    return false;
                uint32 magic = 0;
                Memory::copy( &magic, pInBuffer, sizeof( uint32 ) );
                if ( magic != kMagic )
                {
                    SW_LOG_ERROR_ID( "GameStateSnapshot.StateSnapshotNoSWST", "State snapshot has no SWST header - not written by GameStateSnapshot::writeEnvelope" );
                    return false;
                }
                if ( size < sizeof( uint32 ) * 4 )
                    return false;
                arch >> magic;
                uint32 version = 0;
                arch >> version;
                if ( version != kVersion )
                {
                    SW_LOG_ERROR_ID( "GameStateSnapshot.StateSnapshotVersionNot", "State snapshot version %# is not this build's %#", version, kVersion );
                    return false;
                }
                arch >> outHeader._processToken;
                auto loadedSceneID     = arch.readString();
                auto loadedContentText = arch.readString();
                if ( arch.isError() )
                    return false;
                outHeader._sceneID     = AssetRedirect::toCurrentID( loadedSceneID ); // 씬을 옮긴(팩을 흡수한) 뒤에도 옛 세이브가 제 씬을 찾는다
                outHeader._contentText = std::move( loadedContentText );
                return arch.isError() == false;
            }

            /**
             * @brief 세이브 머리의 씬 정체 @p savedSceneID 가 지금 활성 씬과 같은지 봅니다. 아니면 까닭을 오류로 남기고 false 입니다.
             * @details 복원은 활성 씬을 비우고 채우므로, 다른 씬(타이틀 · 다른 맵) 위에서 풀면 그 씬에 다른 맵의 오브젝트가 선다. 언리얼 게임은 세이브에 맵 이름을 적고
             *          `OpenLevel` 뒤에 적용한다 — 슬롯 불러오기는 `GameInstance` 가 머리(`readSceneID`)를 보고 그 씬을 먼저 연다. 여기는 그 흐름을 거치지 않은 읽기의 마지막 막기다.
             */
            static bool validateScene( const string& savedSceneID, const string& activeSceneID )
            {
                if ( savedSceneID == activeSceneID )
                    return true;
                SW_LOG_ERROR_ID( "GameStateSnapshot.SceneMismatch", "This save was made in scene '%#' but the active scene is '%#' - open that scene before loading it",
                                 savedSceneID.c_str(), activeSceneID.c_str() );
                return false;
            }

            /** @brief 지금 실행의 콘텐츠 구성(`IContentConfiguration`)입니다. 걸린 것이 없으면 nullptr(구성 없음)입니다. */
            static const IContentConfiguration* findContent() { return game::findSubsystem<IContentConfiguration>(); }

            /**
             * @brief 세이브 머리의 콘텐츠 구성 글 @p savedText 를 지금 구성으로 읽어도 되는지 봅니다. 아니면 까닭을 오류로 남기고 false 입니다.
             * @details 머리는 신뢰하지 않는 파일의 글이다. 구성이 없는 실행이 모드를 켠 세이브를 받으면 없는 리소스를 가리키게 되므로 거절한다(빈 글이면 늘 받는다 —
             *          모드 없는 세이브는 모드를 켠 실행에서도 읽힌다, 새 모드는 내용을 더할 뿐이다).
             */
            static bool validateContent( const string& savedText )
            {
                const IContentConfiguration* const pContent = findContent();
                if ( pContent != nullptr )
                    return pContent->validateSaveCompatible( savedText );
                if ( savedText.empty() )
                    return true;
                SW_LOG_ERROR_ID( "GameStateSnapshot.SaveNeedsContent", "This save was made with content '%#' (mods) but this run has no content configuration - start with the same mods to load it",
                                 savedText.c_str() );
                return false;
            }
        };

        /** @brief 봉투 넷째 섹션(씬 몫)을 푼 것입니다. */
        struct ScenePart
        {
            vector<Scene::SavedFileID>         _listFileID; ///< 찍은 오브젝트 id → 파일 id(`_objectID` 는 찍은 실행의 id)
            SceneStreamingSubsystem::SaveState _streaming;
            SceneDataLayerSubsystem::SaveState _dataLayers;
            SceneSubLevelSubsystem::SaveState  _subLevels;
            double3                            _worldOrigin{}; ///< 씬 섹션 오브젝트의 자리가 기준한 월드 원점(`Scene::getWorldOrigin`)
        };

        struct ScenePartInternal
        {
            /** @brief 씬 몫을 씁니다 — 실은 오브젝트(@p listObjectID)의 파일 id 와 스트리밍 셀 상태. 씬이 없으면 빈 바이트입니다. */
            static void write( const Scene* pScene, const vector<uint64>& listObjectID, vector<uint8>& outBytes )
            {
                outBytes.clear();
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                    return;
                Archive                    arch;
                vector<Scene::SavedFileID> listFileID;
                for ( const uint64 objectID : listObjectID )
                {
                    const uint64 fileID = pScene->findFileID( objectID );
                    if ( fileID != 0 )
                        listFileID.push_back( Scene::SavedFileID{ objectID, fileID } );
                }
                arch << static_cast<uint32>( listFileID.size() );
                for ( const Scene::SavedFileID& entry : listFileID )
                {
                    arch << entry._objectID << entry._fileID;
                }
                const double3& origin = pScene->getWorldOrigin();
                arch << origin._x << origin._y << origin._z;
                vector<uint8> bytesStreaming;
                pScene->getObjectManager()->getSubsystem<SceneStreamingSubsystem>().writeSaveState( bytesStreaming );
                arch.writeSection( bytesStreaming.data(), static_cast<uint32>( bytesStreaming.size() ) );
                vector<uint8> bytesDataLayers;
                pScene->getObjectManager()->getSubsystem<SceneDataLayerSubsystem>().writeSaveState( bytesDataLayers );
                arch.writeSection( bytesDataLayers.data(), static_cast<uint32>( bytesDataLayers.size() ) );
                vector<uint8> bytesSubLevels;
                pScene->getObjectManager()->getSubsystem<SceneSubLevelSubsystem>().writeSaveState( bytesSubLevels );
                arch.writeSection( bytesSubLevels.data(), static_cast<uint32>( bytesSubLevels.size() ) );
                outBytes.assign( arch.getData(), arch.getData() + arch.getSize() );
            }

            /** @brief `write` 의 짝입니다(검증 — 씬을 건드리지 않는다). 잘렸거나 셀 상태를 풀지 못하면 false 입니다. */
            [[nodiscard]] static bool read( const vector<uint8>& bytes, ScenePart& outPart )
            {
                Archive arch( bytes.data(), bytes.size() );
                uint32  fileIDCount{ 0 };
                fileIDCount = arch.readCount( sizeof( uint64 ) * 2 );
                if ( arch.isError() )
                    return false;
                outPart._listFileID.resize( fileIDCount );
                for ( Scene::SavedFileID& entry : outPart._listFileID )
                {
                    arch >> entry._objectID >> entry._fileID;
                }
                arch >> outPart._worldOrigin._x >> outPart._worldOrigin._y >> outPart._worldOrigin._z;
                vector<uint8> bytesStreaming;
                vector<uint8> bytesDataLayers;
                vector<uint8> bytesSubLevels;
                if ( arch.readSection( bytesStreaming ) == false || arch.readSection( bytesDataLayers ) == false || arch.readSection( bytesSubLevels ) == false || arch.isError() )
                    return false;
                return SceneStreamingSubsystem::readSaveState( bytesStreaming.data(), bytesStreaming.size(), outPart._streaming ) &&
                       SceneDataLayerSubsystem::readSaveState( bytesDataLayers.data(), bytesDataLayers.size(), outPart._dataLayers ) &&
                       SceneSubLevelSubsystem::readSaveState( bytesSubLevels.data(), bytesSubLevels.size(), outPart._subLevels );
            }

            /** @brief 교체한 활성 씬에 씬 몫을 얹습니다 — 다시 만든 오브젝트에 파일 id 를 다시 붙이고(@p mapObjectID 찍은 id → 이번 id) 스트리밍 셀에 세이브의 상태를 얹는다. */
            static void apply( Scene& scene, ScenePart part, const unordered_map<uint64, uint64>& mapObjectID )
            {
                vector<Scene::SavedFileID> listFileID;
                listFileID.reserve( part._listFileID.size() );
                for ( const Scene::SavedFileID& entry : part._listFileID )
                {
                    const auto liveIt = mapObjectID.find( entry._objectID );
                    if ( liveIt != mapObjectID.end() )
                        listFileID.push_back( Scene::SavedFileID{ liveIt->second, entry._fileID } );
                }
                scene.rebindRestoredFileIDs( listFileID );
                // 씬 섹션의 오브젝트는 저장 때의 원점 기준 자리로 다시 섰다 — 원점도 그때로 돌린다(셀 · 레이어 · 하위 레벨의 떠 둔 상태는 엔티티마다 원점을 든다).
                scene.restoreWorldOrigin( part._worldOrigin );
                // 켜진 레이어가 먼저다 — 스트리밍 셀 조각도 레이어로 오른다.
                scene.getObjectManager()->getSubsystem<SceneDataLayerSubsystem>().applySaveState( std::move( part._dataLayers ) );
                scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>().applySaveState( std::move( part._streaming ) );
                scene.getObjectManager()->getSubsystem<SceneSubLevelSubsystem>().applySaveState( scene, std::move( part._subLevels ) );
            }
        };

        struct GameStateSnapshotInternal
        {
            /** @brief 게임 창구로 찾은 씬 매니저의 활성 씬 오브젝트 매니저입니다. 씬 매니저가 없거나 활성 씬이 없으면 nullptr 입니다. */
            static GameObjectManager* findActiveObjectManager()
            {
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                return pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
            }
            /**
             * @brief 개발 구성의 결함 주입이 교체 단계를 지게 하라는지 봅니다(`ISnapshotFaultHook` — 배포는 창구가 없어 늘 false).
             * @details 깨진 복원 뒤 세이브 막기 · 씬 다시 열기를 시나리오가 밟는 길이다. 런타임은 창구만 묻고 테스트 변수는 개발 전용 모듈(`GameFramework/Developer` — GameFrameworkDev)이 든다.
             */
            static bool shouldBreakReplace()
            {
                const ISnapshotFaultHook* const pFaultHook = ISnapshotFaultHook::find();
                return pFaultHook != nullptr && pFaultHook->shouldBreakReplace();
            }
            /** @brief 게임 창구로 찾은 씬 매니저의 활성 씬입니다. 없으면 nullptr 입니다. */
            static Scene* findActiveScene()
            {
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                return pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
            }
            /** @brief 활성 씬의 지금 리소스 id(`Scene::getSourcePath` 를 리다이렉트로 푼 것 — 세이브 머리와 같은 꼴)입니다. 씬 매니저나 활성 씬이 없으면(씬 없는 스냅샷) 빈 글입니다. */
            static string findActiveSceneID()
            {
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                const Scene* const  pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
                return pScene != nullptr ? AssetRedirect::toCurrentID( pScene->getSourcePath() ) : string{};
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
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
            type._pDespawn = pRegistration->getDespawn();
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
                type._pCapture( SceneIterationKey{}, *_pComponentStateStore, *pManager );
        }
        for ( const StatefulComponentType& type : _listStatefulType )
        {
            if ( type._pDespawn != nullptr )
                type._pDespawn( SceneIterationKey{}, *pManager );
        }
    }

    bool GameStateSnapshot::writeEnvelope( SerializePurpose purpose, const TypeInfo* pStateType, const void* pStateInstance, vector<uint8>& outBytes )
    {
        Archive arch;
        arch << StateEnvelopeInternal::kMagic;
        arch << StateEnvelopeInternal::kVersion;
        arch << ObjectStateSerializer::getProcessToken();
        // 이 세이브를 찍은 씬 — 읽는 쪽이 같은 씬 위인지 봉투를 풀기 전에 본다(씬이 없으면 빈 글).
        arch.writeString( GameStateSnapshotInternal::findActiveSceneID() );
        // 이 세이브를 만든 콘텐츠 구성(켠 모드) — 읽는 쪽이 같은 구성인지 봉투를 풀기 전에 본다(없으면 빈 글).
        const IContentConfiguration* const pContent = StateEnvelopeInternal::findContent();
        arch.writeString( pContent != nullptr ? pContent->makeContentConfigText() : string{} );

        // 1) 씬 오브젝트 바이너리 스냅샷
        //
        // **씬이 없는 것은 실패가 아니다.** 씬 없이 커스텀 상태만 스냅샷하는 것은 지원하는 사용법이다
        // (GameFrameworkTest.GameInstanceSnapshotAndFileRoundTrip 이 그렇게 쓴다). 그러니 여기서 끊으면 안 된다.
        // 다만 반환값을 흔적 없이 버리면, 씬이 있어야 할 상황에서 오브젝트가 하나도 없는 세이브가 나와도
        // 로드할 때까지 아무도 모른다. 빈 섹션은 그대로 쓰되 단서는 남긴다(지원하는 사용법이라 Warning 이 아니라 Info).
        vector<uint8>  bytesScene;
        vector<uint64> listSceneObjectID;
        if ( serializeSceneObjects( purpose, bytesScene, listSceneObjectID ) == false )
            SW_LOG_INFO_ID( "GameStateSnapshot.SceneObjectSnapshotEmpty", "The scene object snapshot is empty - there is no active scene or the game service is not bound. Only the custom state is saved." );
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

        // 4) 씬 몫 — 실은 오브젝트의 파일 id(다른 실행에서 새 id 를 받아도 스트리밍 셀이 파일 id 로 찾는다)와 스트리밍 셀 상태(셀에 든 오브젝트는 씬 섹션 대신 여기).
        vector<uint8> bytesScenePart;
        if ( bytesScene.empty() == false )
            ScenePartInternal::write( GameStateSnapshotInternal::findActiveScene(), listSceneObjectID, bytesScenePart );
        arch.writeSection( bytesScenePart.data(), static_cast<uint32>( bytesScenePart.size() ) );

        // 봉투는 한 번 써서 그대로 넘긴다.
        outBytes.assign( arch.getData(), arch.getData() + arch.getSize() );
        return true;
    }

    expected<void, SnapshotRestoreFailure> GameStateSnapshot::readEnvelope( SerializePurpose purpose, const TypeInfo* pStateType, void* pStateInstance, const void* pInBuffer,
                                                                            uint32 size )
    {
        const unexpected<SnapshotRestoreFailure> rejected{ SnapshotRestoreFailure::Rejected };
        _pComponentStateStore->clear();

        // ---- 1 단계: 검증. 봉투를 끝까지 읽고 분리된 사본에 풀어 본다 — 지금 씬과 상태 인스턴스는 건드리지 않는다. ----
        Archive                       arch( static_cast<const uint8*>( pInBuffer ), size );
        StateEnvelopeInternal::Header header;
        if ( StateEnvelopeInternal::readHeader( pInBuffer, size, arch, header ) == false )
            return rejected;
        if ( StateEnvelopeInternal::validateContent( header._contentText ) == false ||
             StateEnvelopeInternal::validateScene( header._sceneID, GameStateSnapshotInternal::findActiveSceneID() ) == false )
            return rejected;
        const bool              bSameProcess = ( header._processToken == ObjectStateSerializer::getProcessToken() );
        const SceneObjectFormat format       = bSameProcess ? SceneObjectFormat::RestoreIdentity : SceneObjectFormat::WithIdentity;

        // 섹션 넷(씬 오브젝트 · 파생의 리플렉션 상태 · 컴포넌트 상태 · 씬 몫)을 모두 읽은 뒤에 푼다 — 끝이 잘린 봉투가 씬을 비운 뒤에 지지 않게 한다.
        vector<uint8> bytesScene;
        vector<uint8> bytesState;
        vector<uint8> bytesComponent;
        vector<uint8> bytesScenePart;
        if ( arch.readSection( bytesScene ) == false || arch.readSection( bytesState ) == false || arch.readSection( bytesComponent ) == false ||
             arch.readSection( bytesScenePart ) == false || arch.isError() )
            return rejected;
        ScenePart scenePart;
        if ( bytesScenePart.empty() == false && ScenePartInternal::read( bytesScenePart, scenePart ) == false )
        {
            SW_LOG_ERROR_ID( "GameStateSnapshot.ScenePartNotRead", "State snapshot scene part (file ids, streaming cells) could not be read - the save is not used" );
            return rejected;
        }

        GameObjectManager* const pLiveManager = GameStateSnapshotInternal::findActiveObjectManager();
        if ( bytesScene.empty() == false )
        {
            if ( pLiveManager == nullptr )
            {
                SW_LOG_ERROR_ID( "GameStateSnapshot.NoActiveSceneForObjects", "State snapshot carries scene objects but there is no active scene to restore them into" );
                return rejected;
            }
            // 분리된 매니저 — 같은 엔진 서비스를 받되 활성 씬과 오브젝트를 나누지 않는다(프리팹 원형이 쓰는 손수 만든 매니저와 같다). 풀어 본 것은 여기서 버린다.
            GameObjectManager                             probeManager( pLiveManager->getSceneEngineServices() );
            vector<ComponentStateStore::SavedComponentID> listProbeRemap;
            unordered_map<uint64, uint64>                 mapProbeObjectID;
            if ( deserializeSceneObjects( purpose, bytesScene.data(), bytesScene.size(), format, probeManager, listProbeRemap, mapProbeObjectID ) == false )
                return rejected;
        }

        const bool bHasState = bytesState.empty() == false && pStateType != nullptr && pStateInstance != nullptr;
        if ( bHasState )
        {
            // 상태 인스턴스에 바로 풀면 반쯤 덮인 채 진다 — 새 사본에 먼저 풀어 본다.
            const ReflectedInstance probeState = ReflectedInstance::create( *pStateType );
            if ( probeState.isValid() == false )
            {
                SW_LOG_ERROR_ID( "GameStateSnapshot.StateTypeNotConstructible", "State type '%#' cannot be default-constructed, so the save cannot be checked before it is applied",
                                 pStateType->_name.c_str() );
                return rejected;
            }
            if ( BinarySerializer::deserialize( probeState.get(), *pStateType, bytesState.data(), bytesState.size(), SerializeContext::getDefault( purpose ) ) == false )
                return rejected;
        }

        // 컴포넌트 상태는 훅이 다시 만든 컴포넌트에 넘긴다. 못 읽으면 알리고 비운다 — 디렉터는 새로 시작한다(씬 · 상태는 그대로 선다).
        if ( bytesComponent.empty() == false )
        {
            Archive componentArch( bytesComponent.data(), bytesComponent.size() );
            if ( _pComponentStateStore->read( componentArch ) == false )
            {
                SW_LOG_WARNING_ID( "GameStateSnapshot.ComponentStateSectionCould", "Component state section could not be read - components start fresh" );
                _pComponentStateStore->clear();
            }
        }

        // ---- 2 단계: 교체. 검증을 지난 봉투만 씬을 비우고 채운다. 같은 바이트 · 같은 코드라 여기서 지는 것은 검증이 못 본 결함이다 — 씬이 깨졌다고 알린다. ----
        const unexpected<SnapshotRestoreFailure>      broken{ SnapshotRestoreFailure::SceneBroken };
        vector<ComponentStateStore::SavedComponentID> listComponentIDRemap;
        unordered_map<uint64, uint64>                 mapObjectID;
        if ( bytesScene.empty() == false )
        {
            // 비우면 스트리밍 셀이 문서 내용으로 돌아간다(월드는 남는다) — 오브젝트를 다시 만든 뒤 씬 몫을 얹으면 다음 갱신이 세이브의 상태로 셀을 짓는다.
            pLiveManager->clear();
            if ( GameStateSnapshotInternal::shouldBreakReplace() ||
                 deserializeSceneObjects( purpose, bytesScene.data(), bytesScene.size(), format, *pLiveManager, listComponentIDRemap, mapObjectID ) == false )
            {
                _pComponentStateStore->clear();
                return broken;
            }
            Scene* const pActiveScene = GameStateSnapshotInternal::findActiveScene();
            if ( pActiveScene != nullptr && pActiveScene->getObjectManager() == pLiveManager )
                ScenePartInternal::apply( *pActiveScene, std::move( scenePart ), mapObjectID );
        }
        if ( bHasState && BinarySerializer::deserialize( pStateInstance, *pStateType, bytesState.data(), bytesState.size(), SerializeContext::getDefault( purpose ) ) == false )
        {
            _pComponentStateStore->clear();
            return broken;
        }
        // 다른 실행의 세이브는 컴포넌트 id 가 새로 나갔다. 씬 섹션이 만든 표로 저장된 id 를 이번 id 로 바꿔 id 로만 짝짓는다.
        if ( format == SceneObjectFormat::WithIdentity )
            _pComponentStateStore->remapComponentIDs( listComponentIDRemap );

        // 읽기 훅(`onAfterStateDeserialize`)은 부르는 쪽이 이 뒤에 부르고 `endRestore` 로 저장소를 비운다.
        restoreStatefulComponents();
        return {};
    }

    optional<string> GameStateSnapshot::readSceneID( const void* pInBuffer, uint32 size )
    {
        Archive                       arch( static_cast<const uint8*>( pInBuffer ), size );
        StateEnvelopeInternal::Header header;
        if ( StateEnvelopeInternal::readHeader( pInBuffer, size, arch, header ) == false )
            return nullopt;
        return header._sceneID;
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
                type._pRestore( SceneIterationKey{}, *_pComponentStateStore, *pManager );
        }
    }

    bool GameStateSnapshot::serializeSceneObjects( SerializePurpose purpose, vector<uint8>& outBytes, vector<uint64>& outListObjectID )
    {
        outListObjectID.clear();
        GameObjectManager* pObjectManager = GameStateSnapshotInternal::findActiveObjectManager();
        if ( pObjectManager == nullptr )
            return false;

        // 살아 있는 것만 담는다. `forEachGameObject` 가 파괴 대기 오브젝트를 이미 건너뛴다. 실행 중에 세운 파생 오브젝트(`isTransient`)는 세운 쪽이 다시 세운다.
        // 스트리밍 셀 · 데이터 레이어 묶음에 든 오브젝트는 그 상태로 따로 싣는다(씬 몫) — 여기에 실으면 복원 뒤 셀 · 묶음이 다시 지을 때 겹친다.
        const SceneStreamingSubsystem& streaming  = pObjectManager->getSubsystem<SceneStreamingSubsystem>();
        const SceneDataLayerSubsystem& dataLayers = pObjectManager->getSubsystem<SceneDataLayerSubsystem>();
        const SceneSubLevelSubsystem&  subLevels  = pObjectManager->getSubsystem<SceneSubLevelSubsystem>();
        vector<GameObject*>            listValidObject;
        pObjectManager->forEachGameObject( SceneIterationKey{}, [&listValidObject, &streaming, &dataLayers, &subLevels]( GameObject* pObj )
        {
            const bool bPartOwned = streaming.isCellObject( pObj->getObjectID() ) || dataLayers.isGroupObject( pObj->getObjectID() ) || subLevels.isSubLevelObject( pObj->getObjectID() );
            if ( pObj->isTransient() == false && bPartOwned == false )
                listValidObject.push_back( pObj );
        } );

        outBytes.clear();
        outListObjectID.reserve( listValidObject.size() );
        for ( const GameObject* pObj : listValidObject )
        {
            outListObjectID.push_back( pObj->getObjectID() );
        }

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
                SW_LOG_ERROR_ID( "GameStateSnapshot.SceneObjectCouldNot", "Scene object '%#' could not be serialized - the snapshot is not taken", pObj->getName().c_str() );
                outBytes.clear();
                return false;
            }
        }

        return true;
    }

    bool GameStateSnapshot::deserializeSceneObjects( SerializePurpose purpose, const uint8* pData, size_t size, SceneObjectFormat format, GameObjectManager& manager,
                                                     vector<ComponentStateStore::SavedComponentID>& outListRemap, unordered_map<uint64, uint64>& outMapObjectID )
    {
        outListRemap.clear();
        outMapObjectID.clear();
        if ( pData == nullptr || size < sizeof( uint32 ) )
            return false;

        GameObjectManager* const pObjectManager = &manager;

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
        // **하나라도 못 읽으면 실패다.** 읽기를 멈추고도 true 를 돌려주면 검증이 지나가 활성 씬이 못 읽은 오브젝트부터 뒤가 사라진 채 교체된다.
        const bool       bRestoreIdentity = ( format == SceneObjectFormat::RestoreIdentity );
        bool             bComplete        = true;
        ObjectStateBatch batch( bRestoreIdentity ? ObjectIDSpace::Live : ObjectIDSpace::Saved );
        for ( uint32 objectIndex = 0; objectIndex < count; ++objectIndex )
        {
            ObjectIdentity identity;
            const size_t   identityBytes = ObjectStateSerializer::readIdentity( pData + offset, size - offset, identity );
            if ( identityBytes == 0 )
            {
                SW_LOG_ERROR_ID( "GameStateSnapshot.FailedReadObjectIdentity", "Failed to read object identity at index %#", objectIndex );
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
                SW_LOG_ERROR_ID( "GameStateSnapshot.FailedLoadBinaryObject", "Failed to load binary object state at index %#", objectIndex );
                pObjectManager->destroyGameObject( pObj ); // 읽지 못한 자리에 빈 오브젝트를 남기지 않는다.
                bComplete = false;
                break;
            }
            listRestoredObject.push_back( pObj );
            outMapObjectID[identity._objectID] = pObj->getObjectID();
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
            SW_LOG_ERROR_ID( "GameStateSnapshot.SceneRestoreStoppedAfter", "Scene restore stopped after %# of %# objects",
                             static_cast<uint32>( listRestoredObject.size() ), count );
            return false;
        }
        return true;
    }
} // namespace sw
