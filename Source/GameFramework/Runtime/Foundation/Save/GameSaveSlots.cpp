#include "pch.h"

#include "GameFramework/Runtime/Foundation/Save/GameSaveSlots.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Save/GameLocalStore.h"
#include "GameFramework/Runtime/Foundation/Save/GameStateSnapshot.h"
#include "GameFramework/Runtime/Foundation/Save/SaveLock.h"
#include "GameFramework/Runtime/Foundation/Service/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Service/GameEvents.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {
        struct GameSaveSlotsInternal
        {
            /** @brief 게임 상태 세이브 슬롯의 형식 버전입니다(봉투에 적힌다). 스냅샷 봉투(`GameStateSnapshot`)의 바이트 형식을 바꾸면 올린다. */
            static constexpr uint32 kSaveSlotFormatVersion = 0;
            /** @brief `game.save [슬롯]` · `game.load [슬롯]` 의 몸통입니다. 슬롯을 비우면 기본 슬롯입니다. 맡기기만 하고 끝은 감시 값 `Save.Written` · `Save.Loaded` 로 본다. */
            template <bool bSave>
            static bool runSlotCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                GameSaveSlots* const pSaveSlots = game::findSubsystem<GameSaveSlots>();
                const string_view    slot       = listArgument.empty() ? string_view{} : string_view( listArgument[0] );
                const utf8* const    pVerb      = bSave ? "game.save" : "game.load";
                if ( pSaveSlots == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "%#: no game is running", pVerb );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                const bool                               bQueued = bSave ? pSaveSlots->save( slot ) : pSaveSlots->load( slot );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "%#: %# '%#'", pVerb, bQueued ? "queued" : "refused", slot.empty() ? string_view( "<default>" ) : slot );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief 활성 씬의 리소스 id(`Scene::getSourcePath`)입니다. 씬 매니저나 활성 씬이 없으면 빈 글입니다 — 세이브 머리의 씬 정체와 같은 뜻이다. */
            static string findActiveSceneID()
            {
                const SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                const Scene* const        pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
                return pScene != nullptr ? pScene->getSourcePath() : string{};
            }

            /**
             * @brief 넷 세션이 열려 있어 세이브를 이 씬에 풀 수 없으면 알리고 true 입니다.
             * @details 씬의 상태는 서버의 것이고 피어가 함께 본다 — 한 피어가 로컬 세이브로 씬을 비우고 다시 세우면 복제 대상(폰 · 오브젝트 번호)이 그 피어에서만 갈린다.
             *          씬을 바꾸는 미니게임을 거절하는 것과 같은 규칙이다(`MinigameFlow::validateEntry`). 불러온 뒤 세션을 여는 것은 된다.
             */
            static bool isRestoreRefused( string_view slot )
            {
                const GameNetSession* const pNetSession = game::findSubsystem<GameNetSession>();
                if ( pNetSession == nullptr || pNetSession->isActive() == false )
                    return false;
                SW_LOG_WARNING_ID( "GameSaveSlots.LoadInNetSession", "Save slot '%#' is not loaded - a net session is open and a local restore would split this peer from the session", slot );
                return true;
            }

            /** @brief 감시 값 `Save.Blocked` 입니다 — 세이브 막기 문이 들렸으면 1(씬 로드 · 깨진 복원 · 연출). */
            [[nodiscard]] static bool readSaveBlocked( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const SaveLock* const pSaveLock = game::findSubsystem<SaveLock>();
                if ( pSaveLock == nullptr )
                    return false;
                outValue = pSaveLock->isBlocked() ? 1.0 : 0.0;
                return true;
            }

            template <bool bWritten>
            [[nodiscard]] static bool readSlotCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const GameSaveSlots* const pSaveSlots = game::findSubsystem<GameSaveSlots>();
                if ( pSaveSlots == nullptr )
                    return false;
                const SaveSlotCounts& counts = pSaveSlots->getCounts();
                outValue                     = static_cast<float64>( bWritten ? counts._written : counts._loaded );
                return true;
            }
        };
    } // namespace

    GameSaveSlots::GameSaveSlots( GameInstance& owner, const GameSettings& settings, SaveLock& saveLock )
        : _owner{ owner }
        , _settings{ settings }
        , _saveLock{ saveLock }
        , _listSlotRequest{}
        , _pendingSlotRestore{}
        , _brokenRestoreBlock{}
        , _playTime{ 0.0 }
        , _counts{}
    {
    }

    GameSaveSlots::~GameSaveSlots() = default;

    span<const utf8* const> GameSaveSlots::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { GameSceneFlow::kSubsystemName, GameLocalStore::kSubsystemName };
        return kArrDependency;
    }

    void GameSaveSlots::shutdownSubsystem()
    {
        _pendingSlotRestore = nullopt; // 씬 흐름이 맡긴 로드를 잊는다 — 기다리던 세이브(와 그 완료 함수)도 잊는다
    }

    void GameSaveSlots::notifySceneBroken( SerializePurpose purpose )
    {
        SW_LOG_ERROR_ID( "GameSaveSlots.RestoreBrokeScene", "The restore failed after the scene was cleared - saving is blocked until a restore succeeds or the scene is reopened" );
        _brokenRestoreBlock = _saveLock.block( "restore.broken" );
        // 세이브 불러오기(슬롯 · 자동 저장)는 씬을 디스크에서 다시 연다 — 핫 리로드 스냅샷은 저장하지 않은 편집이 든 씬이라 다시 열지 않고 막기만 둔다.
        if ( purpose == SerializePurpose::SaveGame )
            reopenBrokenScene();
    }

    void GameSaveSlots::notifySceneRestored()
    {
        _brokenRestoreBlock.reset();
    }

    void GameSaveSlots::reopenBrokenScene()
    {
        const string scenePath = GameSaveSlotsInternal::findActiveSceneID();
        if ( scenePath.empty() )
        {
            SW_LOG_ERROR_ID( "GameSaveSlots.BrokenSceneNotReopened", "The broken scene has no source to reopen - saving stays blocked until a restore succeeds" );
            return;
        }
        // 미니게임 안이면 칸 스택을 둔 채 다시 연다 — 같은 미니게임의 맵이다.
        // 이 불러오기를 잇는 이동이다 — 게임 이동으로 맡기면 열려 있는 그 이동이 막는다.
        if ( _owner.getSubsystem<MinigameFlow>().requestScene( scenePath, SW_DELEGATE_METHOD( GameSceneFlow::SceneLoadedFunction, &GameSaveSlots::onBrokenSceneReopened, this ),
                                                               _owner.getSubsystem<GameSceneFlow>().beginSessionTravel() ) == false )
            SW_LOG_ERROR_ID( "GameSaveSlots.BrokenSceneNotRequested", "The broken scene '%#' could not be requested again - saving stays blocked until a restore succeeds", scenePath.c_str() );
    }

    void GameSaveSlots::onBrokenSceneReopened( Scene* pScene )
    {
        if ( pScene == nullptr )
        {
            SW_LOG_ERROR_ID( "GameSaveSlots.BrokenSceneReopenFailed", "Reopening the broken scene failed - saving stays blocked until a restore succeeds" );
            return;
        }
        _brokenRestoreBlock.reset(); // 디스크에서 다시 선 씬이다 — 일부만 선 씬이 아니다
        SW_LOG_INFO_ID( "GameSaveSlots.BrokenSceneReopened", "The broken scene '%#' was reopened - saving is allowed again", pScene->getSourcePath().c_str() );
    }

    bool GameSaveSlots::resolveSlot( string_view slot, string& outSlot ) const
    {
        const string& defaultSaveSlot = _settings._defaultSaveSlot;
        if ( slot.empty() && defaultSaveSlot.empty() )
        {
            SW_LOG_ERROR_ID( "GameSaveSlots.NoSaveSlotGiven", "No save slot was given and GameSettings has no defaultSaveSlot - nothing is saved or loaded" );
            return false;
        }
        // 세이브는 로컬 저장 슬롯이다 - 자리(사용자 데이터 폴더)는 `GameLocalStore` 가 정하고 여기는 이름만 고른다(언리얼 `SaveGameToSlot` 의 슬롯 이름).
        const string_view chosenSlot = slot.empty() ? string_view( defaultSaveSlot ) : slot;
        if ( ILocalStore::isValidSlotName( chosenSlot ) == false )
        {
            SW_LOG_ERROR_ID( "GameSaveSlots.SaveSlotBreaksSlot", "Save slot '%#' breaks the slot name rule ([0-9a-z_.-], at most one '/' group, 64 characters)", chosenSlot );
            return false;
        }
        outSlot = string( chosenSlot );
        return true;
    }

    void GameSaveSlots::holdSlotRequest( Subscription request )
    {
        (void)VectorUtil::removeIf( _listSlotRequest, []( const Subscription& held )
        { return held.isActive() == false; } );
        _listSlotRequest.push_back( std::move( request ) );
    }

    bool GameSaveSlots::save( string_view slot )
    {
        string slotName;
        if ( resolveSlot( slot, slotName ) == false )
            return false;
        vector<uint8> snapshotBytes;
        if ( _owner.captureSnapshot( SerializePurpose::SaveGame, snapshotBytes ) == false )
        {
            SaveGameSavedEvent event{};
            event._slot     = slotName;
            event._bSuccess = false;
            GameEventUtil::send( event );
            return false;
        }
        LocalStoreWriteOptions options{};
        options._formatVersion = GameSaveSlotsInternal::kSaveSlotFormatVersion;
        holdSlotRequest( _owner.getSubsystem<GameLocalStore>().submitWrite( slotName, std::move( snapshotBytes ), options,
                                                                            SW_DELEGATE_METHOD( GameLocalStore::CompletionFunction, &GameSaveSlots::onSlotWritten, this ) ) );
        return true;
    }

    void GameSaveSlots::onSlotWritten( const LocalStoreCompletion& completion )
    {
        if ( completion._result != LocalStoreResult::Ok )
            SW_LOG_ERROR_ID( "GameSaveSlots.SaveSlotCouldNot", "Save slot '%#' could not be written (local store result %#)", completion._slot.c_str(), static_cast<int32>( completion._result ) );
        else
            ++_counts._written;
        SaveGameSavedEvent event{};
        event._slot     = completion._slot;
        event._bSuccess = completion._result == LocalStoreResult::Ok;
        GameEventUtil::send( event );
    }

    void GameSaveSlots::updatePlayTime( float32 deltaTime )
    {
        const GameSceneFlow& sceneFlow = _owner.getSubsystem<GameSceneFlow>();
        if ( sceneFlow.isOnTitleScene() == false && sceneFlow.hasPendingSceneLoad() == false )
            _playTime += static_cast<float64>( deltaTime );
    }

    bool GameSaveSlots::load( string_view slot )
    {
        string slotName;
        return resolveSlot( slot, slotName ) && loadSlot( slotName, {} );
    }

    bool GameSaveSlots::loadSlot( const string& slot, const AppliedFunction& onApplied )
    {
        if ( GameSaveSlotsInternal::isRestoreRefused( slot ) )
            return false;
        const uint32 sessionTravel = _owner.getSubsystem<GameSceneFlow>().beginSessionTravel();
        holdSlotRequest( _owner.getSubsystem<GameLocalStore>().submitRead( slot, SW_DELEGATE_LAMBDA( GameLocalStore::CompletionFunction, [this, onApplied, sessionTravel]( const LocalStoreCompletion& completion )
        { onSlotRead( completion, onApplied, sessionTravel ); } ) ) );
        return true;
    }

    void GameSaveSlots::onSlotRead( const LocalStoreCompletion& completion, const AppliedFunction& onApplied, uint32 sessionTravel )
    {
        if ( completion._result != LocalStoreResult::Ok )
        {
            SW_LOG_ERROR_ID( "GameSaveSlots.SaveSlotCouldNot2", "Save slot '%#' could not be read (local store result %#)", completion._slot.c_str(), static_cast<int32>( completion._result ) );
            publishSlotLoaded( completion._slot, false, onApplied, sessionTravel );
            return;
        }
        if ( completion._formatVersion != GameSaveSlotsInternal::kSaveSlotFormatVersion )
        {
            // 옛 형식 리더는 두지 않는다 - 다른 판의 세이브는 거절한다.
            SW_LOG_ERROR_ID( "GameSaveSlots.SaveSlotFormatVersion", "Save slot '%#' has format version %#, this build reads %#", completion._slot.c_str(), completion._formatVersion,
                             GameSaveSlotsInternal::kSaveSlotFormatVersion );
            publishSlotLoaded( completion._slot, false, onApplied, sessionTravel );
            return;
        }
        applySlot( completion._slot, completion._bytes, onApplied, sessionTravel );
    }

    void GameSaveSlots::applySlot( const string& slot, vector<uint8> bytes, const AppliedFunction& onApplied, uint32 sessionTravel )
    {
        GameSceneFlow& sceneFlow = _owner.getSubsystem<GameSceneFlow>();
        if ( sceneFlow.isSessionTravelCurrent( sessionTravel ) == false )
        {
            SW_LOG_INFO_ID( "GameSaveSlots.LoadSuperseded", "Save slot '%#' is not applied - the player chose to go somewhere else after it", slot.c_str() );
            publishSlotLoaded( slot, false, onApplied, sessionTravel );
            return;
        }
        if ( GameSaveSlotsInternal::isRestoreRefused( slot ) || GameStateSnapshot::upgradeEnvelope( bytes ) == false )
        {
            publishSlotLoaded( slot, false, onApplied, sessionTravel ); // 까닭은 검사 · 판 올림이 남겼다
            return;
        }
        // 머리만 읽어 찍은 씬과 미니게임을 본다. 같은 씬 · 같은 미니게임(또는 씬 없이 찍은 세이브)이면 바로 적용하고, 아니면 그 미니게임을 켜고 그 씬을 연 뒤 적용한다 —
        // 언리얼 게임이 세이브의 맵 이름으로 `OpenLevel` 한 뒤 적용하는 것과 같다(유니티는 `LoadSceneAsync` 의 완료에서 적용). 미니게임은 URL 로 경험 · 게임 모드를 함께 싣는 자리다.
        const optional<string>        savedSceneID  = GameStateSnapshot::readSceneID( bytes.data(), static_cast<uint32>( bytes.size() ) );
        const optional<vector<uint8>> minigameBytes = GameStateSnapshot::readMinigameBytes( bytes.data(), static_cast<uint32>( bytes.size() ) );
        MinigameSavedState            minigameState;
        const bool                    bHeaderRead = savedSceneID.has_value() && minigameBytes.has_value() && MinigameStack::readSaveState( *minigameBytes, minigameState );
        if ( bHeaderRead == false )
        {
            publishSlotLoaded( slot, false, onApplied, sessionTravel ); // 머리가 틀린 까닭은 읽기가 남겼다
            return;
        }
        vector<MinigameSavedFrame>& listFrame    = minigameState._listFrame;
        MinigameFlow&               minigameFlow = _owner.getSubsystem<MinigameFlow>();
        const bool                  bInMinigame  = minigameFlow.isInSavedMinigame( listFrame );
        // 맡긴 이동이 남았으면 활성 씬은 곧 내려간다 — 거기 풀면 성공을 알린 뒤 씬과 함께 사라진다. 세이브의 씬을 다시 요청해 그 이동을 대신한다.
        // 서버의 월드(잃은 서버의 사본)에도 풀지 않는다 — 복제된 오브젝트가 남고 저장이 막힌 채다. 같은 씬이어도 새로 연다.
        const bool bStaysInScene = bInMinigame && sceneFlow.hasPendingSceneLoad() == false && _owner.isInServerWorld() == false && *savedSceneID == GameSaveSlotsInternal::findActiveSceneID();
        if ( savedSceneID->empty() || bStaysInScene )
        {
            const bool bRestored = restoreSlot( slot, bytes );
            if ( bRestored && bInMinigame )
                minigameFlow.applySavedMinigame( std::move( listFrame ) );
            publishSlotLoaded( slot, bRestored, onApplied, sessionTravel );
            return;
        }
        if ( _pendingSlotRestore.has_value() )
        {
            // 앞 불러오기는 이 불러오기에 물렸다 — 그 씬이 서도 적용하지 않는다.
            const PendingSlotRestore previous = std::move( *_pendingSlotRestore );
            _pendingSlotRestore               = nullopt;
            publishSlotLoaded( previous._slot, false, previous._onApplied, previous._sessionTravel );
        }
        _pendingSlotRestore = PendingSlotRestore{ slot, std::move( bytes ), sessionTravel, onApplied };
        // 미니게임 밖의 세이브는 장치 밖 이동이다 — 켜진 미니게임이 있으면 흐름이 비운다.
        const GameSceneFlow::SceneLoadedFunction onLoaded   = SW_DELEGATE_LAMBDA( GameSceneFlow::SceneLoadedFunction, [this, sessionTravel]( Scene* pScene )
          { onSaveSceneLoaded( pScene, sessionTravel ); } );
        const bool                               bRequested = listFrame.empty() ? sceneFlow.requestSceneThen( *savedSceneID, onLoaded, sessionTravel )
                                                                                : minigameFlow.travelToSavedMinigame( std::move( listFrame ), *savedSceneID, onLoaded, sessionTravel );
        if ( bRequested == false )
        {
            SW_LOG_ERROR_ID( "GameSaveSlots.SaveSceneNotRequested", "Save slot '%#' was made in scene '%#', which could not be requested - nothing is loaded", slot.c_str(), savedSceneID->c_str() );
            _pendingSlotRestore = nullopt;
            publishSlotLoaded( slot, false, onApplied, sessionTravel );
        }
    }

    void GameSaveSlots::onSaveSceneLoaded( Scene* pScene, uint32 sessionTravel )
    {
        if ( _pendingSlotRestore.has_value() == false || _pendingSlotRestore->_sessionTravel != sessionTravel )
            return; // 종료가 거뒀거나 뒤 불러오기가 물렸다(물릴 때 알렸다)
        const PendingSlotRestore pending = std::move( *_pendingSlotRestore );
        _pendingSlotRestore              = nullopt;
        const bool bCurrent              = _owner.getSubsystem<GameSceneFlow>().isSessionTravelCurrent( sessionTravel );
        if ( pScene == nullptr && bCurrent )
            SW_LOG_ERROR_ID( "GameSaveSlots.SaveSceneNotOpened", "The scene of save slot '%#' did not open (failed or replaced by a later request) - nothing is loaded", pending._slot.c_str() );
        publishSlotLoaded( pending._slot, pScene != nullptr && bCurrent && restoreSlot( pending._slot, pending._bytes ), pending._onApplied, sessionTravel );
    }

    bool GameSaveSlots::restoreSlot( const string& slot, const vector<uint8>& bytes )
    {
        // 씬을 여는 사이 세션이 열렸을 수 있다.
        if ( GameSaveSlotsInternal::isRestoreRefused( slot ) || _owner.restoreSnapshot( SerializePurpose::SaveGame, bytes ) == false )
            return false;
        const optional<GameStateSnapshot::SaveInfo> info = GameStateSnapshot::readSaveInfo( bytes.data(), static_cast<uint32>( bytes.size() ) );
        if ( info.has_value() )
            _playTime = info->_playTime;
        // 넘기지 못한 결과도 이 세이브의 것이 된다 — 앞 진행이 번 것은 버린다(머리는 앞에서 읽혔다).
        const optional<vector<uint8>> minigameBytes = GameStateSnapshot::readMinigameBytes( bytes.data(), static_cast<uint32>( bytes.size() ) );
        MinigameSavedState            minigameState;
        if ( minigameBytes.has_value() && MinigameStack::readSaveState( *minigameBytes, minigameState ) )
            _owner.getSubsystem<MinigameFlow>().setPendingResults( std::move( minigameState._listResult ) );
        return true;
    }

    void GameSaveSlots::publishSlotLoaded( const string& slot, bool bLoaded, const AppliedFunction& onApplied, uint32 sessionTravel )
    {
        if ( bLoaded )
        {
            ++_counts._loaded;
            if ( AutosaveManager* const pAutosave = game::findSubsystem<AutosaveManager>() )
                pAutosave->resetProgress(); // 앞 진행의 체크포인트 · 요청으로 되돌린 진행을 저장하지 않는다(자동 저장 되돌리기는 아래 완료가 제 슬롯의 것으로 잇는다)
        }
        GameSceneFlow&      sceneFlow = _owner.getSubsystem<GameSceneFlow>();
        SaveGameLoadedEvent event{};
        event._slot        = slot;
        event._bSuccess    = bLoaded;
        event._bSuperseded = bLoaded == false && sceneFlow.isSessionTravelCurrent( sessionTravel ) == false;
        sceneFlow.endSessionTravel( sessionTravel );
        GameEventUtil::send( event );
        if ( onApplied.isBound() )
            onApplied( bLoaded );
    }

    // 세이브 메뉴 자리의 개발 명령 — 시나리오가 플레이어의 "저장 · 불러오기" 를 같은 창구(`save` · `load`)로 부른다. 메뉴 화면은 아직 없다.
    SW_DEV_COMMAND( GameSave, "game.save", "game.save [slot]", "Save the scene and game state to a save slot (empty: the default slot of gamesettings)",
                    &GameSaveSlotsInternal::runSlotCommand<true> );
    SW_DEV_COMMAND( GameLoad, "game.load", "game.load [slot]", "Load the scene and game state from a save slot (empty: the default slot of gamesettings)",
                    &GameSaveSlotsInternal::runSlotCommand<false> );

    SW_AUTOMATION_WATCH( saveWritten, "Save.Written", "Game state save slots written successfully by the running game", &GameSaveSlotsInternal::readSlotCount<true> );
    // 막기는 씬 로드(워커)와 세이브 읽기(저장소)의 끝을 따르므로 벽시계 감시 값이다.
    // 활동 수 없음: 저장 잠금은 게임 상태(씬 로드 · 깨진 복원 · 컷신)가 풀어 주는 문이다 — 셀 수 있는 일의 양이 아니다.
    SW_AUTOMATION_WALL_WATCH( saveBlocked, "Save.Blocked", "1 while the running game's save gate refuses saving (scene loading, broken restore, cutscene)", &GameSaveSlotsInternal::readSaveBlocked,
                              nullptr );
    SW_AUTOMATION_WATCH( saveLoaded, "Save.Loaded", "Game state save slots read and restored successfully by the running game", &GameSaveSlotsInternal::readSlotCount<false> );
} // namespace sw
