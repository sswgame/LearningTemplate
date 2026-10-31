#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Foundation/Framework/Save/Autosave.h"
#include "GameFramework/Base/Foundation/Framework/Save/GameLocalStore.h"
#include "GameFramework/Base/Online/Local/MemoryLocalStore.h"

// AutosaveTest — 자동 저장 정책(순환 슬롯 · 요청 합치기 · 최소 간격 · 막기 · 종료 · 체크포인트 되돌리기). 슬롯은 메모리 로컬 저장소에 쓴다(nogpu, 디스크 없음).

namespace
{
    /** @brief 상태 만들기 · 되살리기를 흉내 냅니다. 저장마다 번호를 붙인 바이트를 만들고, 되살리기는 받은 바이트를 적어 둡니다. */
    struct FakeGame
    {
        sw::vector<sw::string> _listSavedState{};
        sw::string             _loadedState{};

        sw::AutosaveManager::SaveDelegate makeSave()
        {
            return sw::AutosaveManager::SaveDelegate::create( [this]( sw::vector<uint8>& outBytes )
            {
                const sw::string state = sw::formatToString( "state%#", _listSavedState.size() );
                _listSavedState.push_back( state );
                outBytes.assign( state.begin(), state.end() );
                return true;
            } );
        }
        sw::AutosaveManager::LoadDelegate makeLoad()
        {
            return sw::AutosaveManager::LoadDelegate::create( [this]( const sw::vector<uint8>& bytes )
            {
                _loadedState.assign( bytes.begin(), bytes.end() );
                return true;
            } );
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
        SW_REQUIRE_TRUE( manager.initialize( settings, *store, game.makeSave(), game.makeLoad() ) );
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
    SW_REQUIRE_TRUE( next.initialize( settings, *store, game.makeSave(), game.makeLoad() ) );
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
 * @brief [AutosaveTest] 한 프레임의 요청은 하나로 합쳐 더 중요한 까닭으로 남고, 최소 간격 안의 요청은 버리지 않고 기다렸다 저장한다. 막기 중에는 기다리되 종료는 막기를 넘는다
 */
SW_TEST_CASE( AutosaveTest, MergesWaitsAndBlocks )
{
    sw::AutosaveSettings settings = makeSettings();
    settings._minimumGap          = 30.0f;
    sw::MemoryLocalDatabase            database;
    sw::unique_ptr<sw::GameLocalStore> store = makeStore( database );
    FakeGame                           game;
    sw::AutosaveManager                manager;
    SW_REQUIRE_TRUE( manager.initialize( settings, *store, game.makeSave(), game.makeLoad() ) );
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

    manager.setBlocked( sw::hashed_string( "combat" ) );
    manager.notifyBeforeBoss( sw::hashed_string( "dragon" ) );
    step( manager, *store, 60.0f );
    SW_EXPECT_EQUAL( 2u, manager.getSaveCount() );
    SW_EXPECT_TRUE( manager.notifyQuit() ); // 종료는 막기를 넘는다
    store->update();
    SW_EXPECT_EQUAL( 3u, manager.getSaveCount() );
    manager.clearBlocked( sw::hashed_string( "combat" ) );
    step( manager, *store, 0.1f );
    SW_EXPECT_EQUAL( 3u, manager.getSaveCount() ); // 종료 저장이 쌓인 요청을 가져갔다
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
    SW_REQUIRE_TRUE( manager.initialize( settings, *store, game.makeSave(), game.makeLoad() ) );
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
    const sw::AutosaveSettings settings = makeSettings();
    sw::MemoryLocalDatabase    database;
    FakeGame                   game;
    {
        sw::unique_ptr<sw::GameLocalStore> store = makeStore( database );
        sw::AutosaveManager                manager;
        SW_REQUIRE_TRUE( manager.initialize( settings, *store, game.makeSave(), game.makeLoad() ) );
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
    SW_REQUIRE_TRUE( next.initialize( settings, *store, game.makeSave(), game.makeLoad() ) );
    store->update();
    SW_EXPECT_TRUE( next.findLatestSlot() == nullptr );
}
