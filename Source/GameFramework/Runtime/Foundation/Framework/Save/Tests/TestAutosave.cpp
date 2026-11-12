#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/GameLocalStore.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/MemoryLocalStore.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/SaveGate.h"

// AutosaveTest — 자동 저장 정책(순환 슬롯 · 요청 합치기 · 최소 간격 · 막기 문 · 종료 · 체크포인트 되돌리기 · 깨진 복원). 슬롯은 메모리 로컬 저장소에 쓴다(nogpu, 디스크 없음).

namespace
{
    /**
     * @brief 상태 만들기 · 되살리기를 흉내 냅니다. 저장마다 번호를 붙인 바이트를 만들고, 되살리기는 받은 바이트를 적어 둡니다.
     * @details 게임 인스턴스처럼 세이브 막기 문을 들고, `_bBreakRestore` 면 되살리기가 "씬을 비운 뒤 졌다" 로 막기를 건다(`GameInstance::readStateEnvelope`). 성공한 되살리기가 푼다.
     */
    struct FakeGame
    {
        sw::vector<sw::string>                        _listSavedState{};
        sw::string                                    _loadedState{};
        sw::SaveGate                                  _saveGate{};
        sw::Subscription                              _brokenRestoreBlock{};
        sw::vector<uint8>                             _deferredBytes{};
        sw::AutosaveManager::RestoreCompletedFunction _deferredCompleted{};
        uint8                                         _bBreakRestore : 1;
        uint8                                         _bDeferRestore : 1;

        FakeGame()
            : _bBreakRestore{ SW_FALSE }
            , _bDeferRestore{ SW_FALSE }
        {
        }

        sw::AutosaveManager::SaveDelegate makeSave()
        {
            return sw::AutosaveManager::SaveDelegate::create( [this]( sw::vector<uint8>& outBytes )
            {
                sw::StringBuilder<sw::constant::buffer::kMax256> stateBuilder;
                stateBuilder.appendFormat( "state%#", _listSavedState.size() );
                const sw::string state{ stateBuilder.view() };
                _listSavedState.push_back( state );
                outBytes.assign( state.begin(), state.end() );
                return true;
            } );
        }
        /** @brief 바로 적용하고 완료를 안에서 알린다. `_bDeferRestore` 면 씬을 옮기는 게임처럼 바이트와 완료를 들고 있다가 `finishDeferredRestore` 에서 적용한다. */
        sw::AutosaveManager::LoadDelegate makeLoad()
        {
            return sw::AutosaveManager::LoadDelegate::create( [this]( const sw::string& slot, const sw::vector<uint8>& bytes, const sw::AutosaveManager::RestoreCompletedFunction& onCompleted )
            {
                (void)slot;
                if ( _bDeferRestore == SW_TRUE )
                {
                    _deferredBytes     = bytes;
                    _deferredCompleted = onCompleted;
                    return true;
                }
                onCompleted( apply( bytes ) );
                return true;
            } );
        }
        bool apply( const sw::vector<uint8>& bytes )
        {
            if ( _bBreakRestore == SW_TRUE )
            {
                _brokenRestoreBlock = _saveGate.block( sw::hashed_string( "restore.broken" ) );
                return false;
            }
            _brokenRestoreBlock.reset();
            _loadedState.assign( bytes.begin(), bytes.end() );
            return true;
        }
        /** @brief 들고 있던 되살리기를 적용하고(씬이 섰다) 완료를 알립니다. @p bSceneOpened 가 false 면 씬이 열리지 않은 실패다. */
        void finishDeferredRestore( bool bSceneOpened )
        {
            const sw::AutosaveManager::RestoreCompletedFunction onCompleted = _deferredCompleted;
            _deferredCompleted                                              = sw::AutosaveManager::RestoreCompletedFunction{};
            onCompleted( bSceneOpened && apply( _deferredBytes ) );
        }
    };

    /** @brief 메모리 데이터 위의 게임 로컬 저장소입니다. 같은 데이터에 새로 만들면 "다시 켠 게임" 이다. */
    sw::unique_ptr<sw::GameLocalStore> makeStore( sw::MemoryLocalDatabase& database )
    {
        return sw::make_unique<sw::GameLocalStore>( sw::make_unique<sw::MemoryLocalStore>( &database, sw::LocalSealContext{} ) );
    }

    sw::AutosaveSettings makeSettings()
    {
        sw::AutosaveSettings settings;
        settings._interval  = 0.0f;
        settings._slotCount = 3;
        return settings;
    }

    /** @brief 관리자 프레임 하나와 저장소 완료 넘기기 하나 - 게임 인스턴스의 프레임 순서(관리자는 게임 프레임, 저장소는 그 앞)를 흉내 낸다. */
    void step( sw::AutosaveManager& manager, sw::GameLocalStore& store, float32 deltaTime )
    {
        manager.update( deltaTime );
        store.update();
    }
} // namespace

/**
 * @brief [AutosaveTest] 새 저장은 가장 새 슬롯의 다음에 쓰고(가장 새 것을 건드리지 않는다), 다시 시작한 관리자는 저장소의 슬롯 기록에서 순번과 슬롯을 잇는다
 */
SW_TEST_CASE( AutosaveTest, RotatesSlotsAndContinuesAcrossRuns )
{
    const sw::AutosaveSettings settings = makeSettings();
    sw::MemoryLocalDatabase    database;
    FakeGame                   game;
    {
        sw::unique_ptr<sw::GameLocalStore> store = makeStore( database );
        sw::AutosaveManager                manager;
        SW_REQUIRE_TRUE( manager.initialize( settings, *store, game._saveGate, game.makeSave(), game.makeLoad() ) );
        SW_EXPECT_TRUE( manager.isScanning() );
        store->update();
        SW_REQUIRE_FALSE( manager.isScanning() );
        for ( int32 saveIndex = 0; saveIndex < 4; ++saveIndex )
        {
            SW_REQUIRE_TRUE( manager.saveNow( sw::AutosaveTrigger::Manual ) );
            SW_EXPECT_FALSE( manager.saveNow( sw::AutosaveTrigger::Manual ) ); // 앞 저장이 끝나기 전에는 맡지 않는다
            store->update();
        }
        SW_REQUIRE_EQUAL( 4u, manager.getSaveCount() );
        SW_REQUIRE_TRUE( manager.findLatestSlot() != nullptr );
        SW_EXPECT_EQUAL( settings.makeSlotName( 0 ), manager.findLatestSlot()->_slotName ); // 0, 1, 2 다음은 다시 0
        SW_EXPECT_EQUAL( uint64( 4 ), manager.findLatestSlot()->_sequence );
    }

    sw::unique_ptr<sw::GameLocalStore> store = makeStore( database );
    sw::AutosaveManager                next;
    SW_REQUIRE_TRUE( next.initialize( settings, *store, game._saveGate, game.makeSave(), game.makeLoad() ) );
    store->update();
    SW_REQUIRE_TRUE( next.findLatestSlot() != nullptr );
    SW_EXPECT_EQUAL( uint64( 4 ), next.findLatestSlot()->_sequence );
    SW_REQUIRE_TRUE( next.saveNow( sw::AutosaveTrigger::Manual ) );
    store->update();
    SW_EXPECT_EQUAL( settings.makeSlotName( 1 ), next.findLatestSlot()->_slotName );
    SW_EXPECT_EQUAL( uint64( 5 ), next.findLatestSlot()->_sequence );

    SW_REQUIRE_TRUE( next.restoreLatest() );
    store->update();
    SW_EXPECT_EQUAL( game._listSavedState.back(), game._loadedState ); // 슬롯 바이트가 그대로 돌아온다
}

/**
 * @brief [AutosaveTest] 한 프레임의 요청은 하나로 합쳐 더 중요한 까닭으로 남고, 최소 간격 안의 요청은 버리지 않고 기다렸다 저장한다. 막기 문이 막힌 동안은 기다리고 종료도 쓰지 않는다
 */
SW_TEST_CASE( AutosaveTest, MergesWaitsAndBlocks )
{
    sw::AutosaveSettings settings = makeSettings();
    settings._minimumGap          = 30.0f;
    sw::MemoryLocalDatabase            database;
    sw::unique_ptr<sw::GameLocalStore> store = makeStore( database );
    FakeGame                           game;
    sw::AutosaveManager                manager;
    SW_REQUIRE_TRUE( manager.initialize( settings, *store, game._saveGate, game.makeSave(), game.makeLoad() ) );
    store->update();

    manager.notifyAreaChanged( sw::hashed_string( "forest" ) );
    manager.reachCheckpoint( sw::hashed_string( "gate" ) );
    step( manager, *store, 0.1f );
    SW_REQUIRE_EQUAL( 1u, manager.getSaveCount() );
    SW_EXPECT_TRUE( manager.findLatestSlot()->_trigger == sw::AutosaveTrigger::Checkpoint );
    SW_EXPECT_EQUAL( sw::hashed_string( "gate" ), manager.findLatestSlot()->_label );

    manager.notifyAreaChanged( sw::hashed_string( "cave" ) );
    step( manager, *store, 10.0f );
    SW_EXPECT_EQUAL( 1u, manager.getSaveCount() ); // 최소 간격 안 — 기다린다
    SW_EXPECT_TRUE( manager.hasPendingRequest() );
    step( manager, *store, 25.0f );
    SW_EXPECT_EQUAL( 2u, manager.getSaveCount() );

    sw::Subscription cinematicBlock = game._saveGate.block( sw::hashed_string( "cinematic" ) );
    manager.notifyBeforeBoss( sw::hashed_string( "dragon" ) );
    step( manager, *store, 60.0f );
    SW_EXPECT_EQUAL( 2u, manager.getSaveCount() );
    SW_EXPECT_FALSE( manager.notifyQuit() ); // 종료는 기다릴 수 없어 막힌 동안은 쓰지 않는다 — 마지막 좋은 슬롯이 남는다
    store->update();
    SW_EXPECT_EQUAL( 2u, manager.getSaveCount() );
    SW_EXPECT_TRUE( manager.hasPendingRequest() );
    cinematicBlock.reset(); // 범위형 막기 — 구독을 놓으면 풀린다
    step( manager, *store, 0.1f );
    SW_EXPECT_EQUAL( 3u, manager.getSaveCount() ); // 기다린 요청이 저장됐다
    SW_EXPECT_TRUE( manager.findLatestSlot()->_trigger == sw::AutosaveTrigger::BeforeBoss );
}

/**
 * @brief [AutosaveTest] 체크포인트 되돌리기는 가장 새 체크포인트 슬롯을 불러오고, 되돌린 체크포인트에 다시 서도 저장하지 않는다
 */
SW_TEST_CASE( AutosaveTest, RestoresTheLatestCheckpoint )
{
    sw::AutosaveSettings settings = makeSettings();
    settings._minimumGap          = 0.0f;
    sw::MemoryLocalDatabase            database;
    sw::unique_ptr<sw::GameLocalStore> store = makeStore( database );
    FakeGame                           game;
    sw::AutosaveManager                manager;
    SW_REQUIRE_TRUE( manager.initialize( settings, *store, game._saveGate, game.makeSave(), game.makeLoad() ) );
    store->update();

    manager.reachCheckpoint( sw::hashed_string( "gate" ) );
    step( manager, *store, 1.0f );
    const sw::string checkpointState = game._listSavedState.back();
    manager.notifyAreaChanged( sw::hashed_string( "cave" ) );
    step( manager, *store, 1.0f );
    SW_REQUIRE_EQUAL( 2u, manager.getSaveCount() );

    SW_REQUIRE_TRUE( manager.restoreCheckpoint() );
    store->update();
    SW_EXPECT_EQUAL( checkpointState, game._loadedState );
    manager.reachCheckpoint( sw::hashed_string( "gate" ) );
    step( manager, *store, 1.0f );
    SW_EXPECT_EQUAL( 2u, manager.getSaveCount() );

    SW_REQUIRE_TRUE( manager.restoreLatest() );
    store->update();
    SW_EXPECT_EQUAL( game._listSavedState.back(), game._loadedState );
}

/**
 * @brief [AutosaveTest] 상태 슬롯 쓰기가 실패하면 저장 수는 그대로이고, 그 칸의 기록을 지워 다시 켠 관리자가 옛 상태를 새 순번으로 보지 않는다
 */
SW_TEST_CASE( AutosaveTest, FailedWriteLeavesNoStaleSlotInfo )
{
    SW_EXPECT_LOG( "Autosave.WriteFailed" );

    const sw::AutosaveSettings settings = makeSettings();
    sw::MemoryLocalDatabase    database;
    FakeGame                   game;
    {
        sw::unique_ptr<sw::GameLocalStore> store = makeStore( database );
        sw::AutosaveManager                manager;
        SW_REQUIRE_TRUE( manager.initialize( settings, *store, game._saveGate, game.makeSave(), game.makeLoad() ) );
        store->update();
        database.failNextWrite(); // 상태 슬롯 쓰기가 도중에 꺼진다
        SW_REQUIRE_TRUE( manager.saveNow( sw::AutosaveTrigger::Manual ) );
        store->update();
        store->update(); // 지우기 완료
        SW_EXPECT_EQUAL( 0u, manager.getSaveCount() );
        SW_EXPECT_TRUE( manager.findLatestSlot() == nullptr );
    }
    sw::unique_ptr<sw::GameLocalStore> store = makeStore( database );
    sw::AutosaveManager                next;
    SW_REQUIRE_TRUE( next.initialize( settings, *store, game._saveGate, game.makeSave(), game.makeLoad() ) );
    store->update();
    SW_EXPECT_TRUE( next.findLatestSlot() == nullptr );
}

/**
 * @brief [AutosaveTest] 씬을 비운 뒤 진 복원은 세이브 막기를 들고, 그동안 간격 · 직접 · 종료 저장이 슬롯을 덮지 않는다. 다음 성공한 복원이 막기를 풀면 기다린 요청이 저장된다
 * @details 감사 P0-6 — 예전에는 실패를 경고만 하고 간격 저장이 깨진 씬을 순환 슬롯 모두에 썼다.
 */
SW_TEST_CASE( AutosaveTest, FailedRestoreBlocksSaving )
{
    sw::AutosaveSettings settings = makeSettings();
    settings._interval            = 10.0f;
    settings._minimumGap          = 0.0f;
    sw::MemoryLocalDatabase            database;
    sw::unique_ptr<sw::GameLocalStore> store = makeStore( database );
    FakeGame                           game;
    sw::AutosaveManager                manager;
    SW_REQUIRE_TRUE( manager.initialize( settings, *store, game._saveGate, game.makeSave(), game.makeLoad() ) );
    store->update();
    SW_REQUIRE_TRUE( manager.saveNow( sw::AutosaveTrigger::Manual ) );
    store->update();
    SW_REQUIRE_EQUAL( 1u, manager.getSaveCount() );

    game._bBreakRestore = SW_TRUE;
    SW_EXPECT_LOG( "Autosave.AutosaveSlotReadBut" );
    SW_REQUIRE_TRUE( manager.restoreLatest() );
    store->update();
    SW_REQUIRE_TRUE( manager.isBlocked() );

    step( manager, *store, 15.0f ); // 간격이 지났다 — 요청은 기다린다
    SW_EXPECT_EQUAL( 1u, manager.getSaveCount() );
    SW_EXPECT_TRUE( manager.hasPendingRequest() );
    SW_EXPECT_FALSE( manager.saveNow( sw::AutosaveTrigger::Manual ) );
    SW_EXPECT_FALSE( manager.notifyQuit() );
    store->update();
    SW_EXPECT_EQUAL( 1u, manager.getSaveCount() );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( game._listSavedState.size() ) ); // 상태를 찍지도 않았다

    game._bBreakRestore = SW_FALSE;
    SW_REQUIRE_TRUE( manager.restoreLatest() );
    store->update();
    SW_EXPECT_FALSE( manager.isBlocked() );
    step( manager, *store, 0.1f );
    SW_EXPECT_EQUAL( 2u, manager.getSaveCount() );
}

/**
 * @brief [AutosaveTest] 게임이 씬을 옮긴 뒤 적용하는 체크포인트 되돌리기(감사 G-7): 완료 전에는 체크포인트를 지난 것으로 두지 않고 다음 되돌리기를 받지 않는다.
 *        완료가 성공이면 그 체크포인트에 다시 서도 저장하지 않고, 씬이 열리지 않은 실패면 표시하지 않는다
 */
SW_TEST_CASE( AutosaveTest, DeferredRestoreMarksCheckpointOnCompletion )
{
    sw::AutosaveSettings settings = makeSettings();
    settings._minimumGap          = 0.0f;
    sw::MemoryLocalDatabase            database;
    sw::unique_ptr<sw::GameLocalStore> store = makeStore( database );
    FakeGame                           game;
    sw::AutosaveManager                manager;
    SW_REQUIRE_TRUE( manager.initialize( settings, *store, game._saveGate, game.makeSave(), game.makeLoad() ) );
    store->update();
    manager.reachCheckpoint( sw::hashed_string( "gate" ) );
    step( manager, *store, 1.0f );
    const sw::string checkpointState = game._listSavedState.back();
    manager.reachCheckpoint( sw::hashed_string( "bridge" ) );
    step( manager, *store, 1.0f );
    SW_REQUIRE_EQUAL( 2u, manager.getSaveCount() );

    // 씬을 옮기는 중 — 바이트는 받았지만 적용 전이다
    game._bDeferRestore = SW_TRUE;
    SW_REQUIRE_TRUE( manager.restoreCheckpoint() );
    store->update();
    SW_REQUIRE_TRUE( manager.isRestoring() );
    SW_EXPECT_EQUAL( sw::hashed_string( "bridge" ), manager.getLastCheckpointID() );
    SW_EXPECT_FALSE( manager.restoreLatest() ); // 앞 되돌리기가 끝나지 않았다

    // 씬이 열리지 않았다 — 실패, 체크포인트 표시 없음
    SW_EXPECT_LOG( "Autosave.AutosaveSlotReadBut" );
    game.finishDeferredRestore( false );
    SW_EXPECT_FALSE( manager.isRestoring() );
    SW_EXPECT_EQUAL( sw::hashed_string( "bridge" ), manager.getLastCheckpointID() );
    SW_EXPECT_TRUE( game._loadedState.empty() );

    // 다시 — 이번에는 씬이 섰다: 적용 뒤 체크포인트를 지난 것으로 둔다
    SW_REQUIRE_TRUE( manager.restoreCheckpoint() );
    store->update();
    game.finishDeferredRestore( true );
    SW_EXPECT_FALSE( manager.isRestoring() );
    SW_EXPECT_EQUAL( checkpointState, game._loadedState );
    SW_EXPECT_EQUAL( sw::hashed_string( "gate" ), manager.getLastCheckpointID() );
    manager.reachCheckpoint( sw::hashed_string( "gate" ) );
    step( manager, *store, 1.0f );
    SW_EXPECT_EQUAL( 2u, manager.getSaveCount() );
}
