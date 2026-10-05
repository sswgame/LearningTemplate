#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Framework/Autosave.h"
#include "GameFramework/Framework/AutosaveTriggerComponent.h"
#include "GameFramework/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

// 자동 저장 정책 — 돌림 칸 · 가장 새 칸을 건드리지 않음 · 요청 합치기와 우선순위 · 최소 간격 · 막기 · 간격 · 종료 · 체크포인트 되돌리기 · 다른 실행 잇기 · 트리거 볼륨.

using namespace sw;

namespace
{
    /** @brief 저장은 순번 글을 파일에 쓰고, 불러오기는 그 글을 읽어 적습니다. 실패를 흉내 낼 수 있습니다. */
    struct AutosaveTestHarness
    {
        AutosaveManager _manager;
        string          _lastLoaded;
        int32           _saveCalls{ 0 };
        bool            _bFailSaves{ false };

        void initialize( const AutosaveSettings& settings )
        {
            _manager.initialize( settings, SW_DELEGATE_LAMBDA( AutosaveManager::SaveDelegate, [this]( string_view path )
            {
                ++_saveCalls;
                if ( _bFailSaves )
                    return false;
                return FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeTextFile( path, to_string( _saveCalls ) );
            } ),
                                 SW_DELEGATE_LAMBDA( AutosaveManager::LoadDelegate, [this]( string_view path )
            {
                string text;
                if ( FileUtil::readTextFile( path, text ) == false )
                    return false;
                _lastLoaded = text;
                return true;
            } ) );
        }
    };

    AutosaveSettings makeAutosaveTestSettings( const string& directory )
    {
        AutosaveSettings settings;
        settings._directory  = directory;
        settings._interval   = 0.0f;
        settings._minimumGap = 0.0f;
        settings._slotCount  = 3;
        return settings;
    }
} // namespace

/**
 * @brief [AutosaveTest] 정책 XML 을 읽는다 — 빠진 속성은 기본값, 범위 밖 칸 수는 경고하고 3
 */
SW_TEST_CASE( AutosaveTest, SettingsReadFromXml )
{
    AutosaveSettings settings;
    SW_ASSERT_TRUE( settings.loadFromXmlText( R"(<Autosave interval="120" minimumGap="10" slots="5" directory="saves/auto" prefix="slot_" quit="false"/>)" ) );
    SW_EXPECT_NEAR_EQUAL( 120.0f, settings._interval, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, settings._minimumGap, 1.0e-4f );
    SW_EXPECT_EQUAL( 5, settings._slotCount );
    SW_EXPECT_TRUE( settings._bOnQuit == SW_FALSE );
    SW_EXPECT_TRUE( settings._bOnCheckpoint == SW_TRUE );
    SW_EXPECT_EQUAL( string( "saves/auto/slot_4.sav" ), settings.makeSlotPath( 4 ) );

    AutosaveSettings             bad;
    test::ScopedDefensiveTestLog expected( "slot count outside the range" );
    SW_ASSERT_TRUE( bad.loadFromXmlText( R"(<Autosave slots="0"/>)" ) );
    SW_EXPECT_EQUAL( 3, bad._slotCount );
}

/**
 * @brief [AutosaveTest] 새 저장은 가장 새 칸의 다음 칸에 돌려 쓰고, 실패한 저장은 칸 기록을 바꾸지 않으며, 다른 실행이 칸 기록을 읽어 순번 · 칸을 잇는다
 * @details 가장 새 칸을 건드리지 않으므로 저장 도중 꺼져도 마지막 좋은 저장이 남는다(파일 쓰기 자체는 `FileUtil::writeFile` 이 원자적).
 */
SW_TEST_CASE( AutosaveTest, SlotsRotateAndTheLatestSurvivesAFailedSave )
{
    const string        directory = test::makeTempDirectory( "autosave_rotation" );
    AutosaveTestHarness harness;
    harness.initialize( makeAutosaveTestSettings( directory ) );

    for ( int32 saveIndex = 0; saveIndex < 4; ++saveIndex )
        SW_ASSERT_TRUE( harness._manager.saveNow( AutosaveTrigger::Manual, hashed_string( "Save" ) ) );
    const AutosaveSlotInfo* pLatest = harness._manager.findLatestSlot();
    SW_ASSERT_NOT_NULL( pLatest );
    SW_EXPECT_EQUAL( 0, pLatest->_slot ); // 0 · 1 · 2 · 0
    SW_EXPECT_EQUAL( uint64( 4 ), pLatest->_sequence );
    vector<AutosaveSlotInfo> listSlot;
    harness._manager.collectSlots( listSlot );
    SW_ASSERT_EQUAL( size_t( 3 ), listSlot.size() );
    SW_EXPECT_EQUAL( uint64( 4 ), listSlot[0]._sequence );
    SW_EXPECT_EQUAL( uint64( 2 ), listSlot[2]._sequence );

    // 실패한 저장 — 가장 새 칸 · 기록이 그대로다.
    harness._bFailSaves = true;
    {
        test::ScopedDefensiveTestLog expected( "a failed autosave" );
        harness._manager.requestSave( AutosaveTrigger::Manual );
        harness._manager.update( 0.1f );
    }
    pLatest = harness._manager.findLatestSlot();
    SW_EXPECT_EQUAL( uint64( 4 ), pLatest->_sequence );
    SW_EXPECT_TRUE( harness._manager.restoreLatest() );
    SW_EXPECT_EQUAL( string( "4" ), harness._lastLoaded );

    // 다른 실행 — 칸 기록을 읽어 순번 5 를 칸 1 에 쓴다.
    AutosaveTestHarness next;
    next.initialize( makeAutosaveTestSettings( directory ) );
    SW_ASSERT_NOT_NULL( next._manager.findLatestSlot() );
    SW_EXPECT_EQUAL( uint64( 4 ), next._manager.findLatestSlot()->_sequence );
    SW_ASSERT_TRUE( next._manager.saveNow( AutosaveTrigger::Manual ) );
    SW_EXPECT_EQUAL( 1, next._manager.findLatestSlot()->_slot );
    SW_EXPECT_EQUAL( uint64( 5 ), next._manager.findLatestSlot()->_sequence );
}

/**
 * @brief [AutosaveTest] 요청은 다음 update 에 하나로 저장되고 더 중요한 까닭이 남는다 · 최소 간격 · 막기는 기다리게 한다 · 꺼진 까닭은 무시 · 간격 저장 · 종료는 막기를 무시한다
 */
SW_TEST_CASE( AutosaveTest, RequestsMergeWaitForGapsAndBlocksAndQuitSavesAnyway )
{
    const string     directory = test::makeTempDirectory( "autosave_requests" );
    AutosaveSettings settings  = makeAutosaveTestSettings( directory );
    settings._minimumGap       = 5.0f;
    settings._interval         = 20.0f;
    settings._bOnAreaChange    = SW_FALSE;
    AutosaveTestHarness harness;
    harness.initialize( settings );
    AutosaveManager& manager = harness._manager;

    manager.notifyAreaChanged( hashed_string( "Forest" ) ); // 정책이 껐다
    SW_EXPECT_FALSE( manager.hasPendingRequest() );

    manager.notifyBeforeBoss( hashed_string( "Dragon" ) );
    manager.reachCheckpoint( hashed_string( "Gate" ) );
    manager.requestSave( AutosaveTrigger::Interval ); // 더 낮은 까닭은 덮지 않는다
    SW_EXPECT_TRUE( manager.getPendingTrigger() == AutosaveTrigger::Checkpoint );
    manager.update( 0.1f );
    SW_EXPECT_EQUAL( 1, harness._saveCalls );
    SW_EXPECT_TRUE( manager.findLatestSlot()->_trigger == AutosaveTrigger::Checkpoint );
    SW_EXPECT_TRUE( manager.findLatestSlot()->_label == hashed_string( "Gate" ) );

    // 같은 체크포인트에 다시 닿으면 저장하지 않는다.
    manager.reachCheckpoint( hashed_string( "Gate" ) );
    SW_EXPECT_FALSE( manager.hasPendingRequest() );

    // 최소 간격 안에서는 기다린다(버리지 않는다).
    manager.notifyBeforeBoss( hashed_string( "Dragon" ) );
    manager.update( 1.0f );
    SW_EXPECT_EQUAL( 1, harness._saveCalls );
    SW_EXPECT_TRUE( manager.hasPendingRequest() );

    // 막기 — 간격이 지나도 풀릴 때까지 기다린다.
    manager.setBlocked( hashed_string( "Combat" ) );
    manager.update( 10.0f );
    SW_EXPECT_EQUAL( 1, harness._saveCalls );
    SW_EXPECT_FALSE( manager.saveNow( AutosaveTrigger::Manual ) );
    manager.clearBlocked( hashed_string( "Combat" ) );
    manager.update( 0.1f );
    SW_EXPECT_EQUAL( 2, harness._saveCalls );
    SW_EXPECT_TRUE( manager.findLatestSlot()->_trigger == AutosaveTrigger::BeforeBoss );

    // 간격 저장 — 20 초마다.
    manager.update( 21.0f );
    manager.update( 0.1f );
    SW_EXPECT_EQUAL( 3, harness._saveCalls );
    SW_EXPECT_TRUE( manager.findLatestSlot()->_trigger == AutosaveTrigger::Interval );

    // 종료는 막기 · 최소 간격과 상관없이 지금 저장한다.
    manager.setBlocked( hashed_string( "Cutscene" ) );
    SW_EXPECT_TRUE( manager.notifyQuit() );
    SW_EXPECT_EQUAL( 4, harness._saveCalls );
    SW_EXPECT_TRUE( manager.findLatestSlot()->_trigger == AutosaveTrigger::Quit );
}

/**
 * @brief [AutosaveTest] 체크포인트로 되돌리면 더 새 저장이 있어도 가장 새 체크포인트 칸을 읽고, 그 체크포인트를 다시 밟아도 저장하지 않는다
 */
SW_TEST_CASE( AutosaveTest, RestoreCheckpointLoadsTheNewestCheckpointSlot )
{
    const string        directory = test::makeTempDirectory( "autosave_checkpoint" );
    AutosaveTestHarness harness;
    AutosaveSettings    settings = makeAutosaveTestSettings( directory );
    settings._slotCount          = 4;
    harness.initialize( settings );
    AutosaveManager& manager = harness._manager;

    SW_EXPECT_FALSE( manager.restoreCheckpoint() ); // 아직 없다
    manager.reachCheckpoint( hashed_string( "Camp" ) );
    manager.update( 0.1f ); // 저장 1 — 체크포인트
    manager.requestSave( AutosaveTrigger::Manual );
    manager.update( 0.1f ); // 저장 2 — 직접
    SW_EXPECT_EQUAL( 2, harness._saveCalls );

    manager.reachCheckpoint( hashed_string( "Bridge" ) );
    manager.update( 0.1f ); // 저장 3 — 체크포인트
    SW_ASSERT_TRUE( manager.restoreCheckpoint() );
    SW_EXPECT_EQUAL( string( "3" ), harness._lastLoaded );
    SW_EXPECT_TRUE( manager.getLastCheckpointId() == hashed_string( "Bridge" ) );
    manager.reachCheckpoint( hashed_string( "Bridge" ) );
    SW_EXPECT_FALSE( manager.hasPendingRequest() );

    manager.requestSave( AutosaveTrigger::Manual );
    manager.update( 0.1f ); // 저장 4 — 직접
    SW_ASSERT_TRUE( manager.restoreCheckpoint() );
    SW_EXPECT_EQUAL( string( "3" ), harness._lastLoaded );
    SW_ASSERT_TRUE( manager.restoreLatest() );
    SW_EXPECT_EQUAL( string( "4" ), harness._lastLoaded );
}

/**
 * @brief [AutosaveTest] 트리거 볼륨은 태그가 맞는 활성자에게 한 번 체크포인트를 넘긴다 — 관리자가 게임 서비스로 묶이지 않았으면 아무것도 안 한다
 */
SW_TEST_CASE( AutosaveTest, TriggerVolumeReachesTheCheckpointOnceForTheTaggedActivator )
{
    GameObjectManager objects;
    GameObject*       pVolume = objects.createGameObject( hashed_string( "CheckpointVolume" ) );
    GameObject*       pPlayer = objects.createGameObject( hashed_string( "Player" ) );
    GameObject*       pCrate  = objects.createGameObject( hashed_string( "Crate" ) );
    SW_ASSERT_NOT_NULL( pVolume );
    SW_ASSERT_NOT_NULL( pPlayer );
    SW_ASSERT_NOT_NULL( pCrate );
    AutosaveTriggerComponent* pTrigger = pVolume->addComponent<AutosaveTriggerComponent>();
    SW_ASSERT_NOT_NULL( pTrigger );
    pTrigger->configure( AutosaveTrigger::Checkpoint, hashed_string( "Shrine" ), TagID::request( "Player" ) );
    pPlayer->addTag( TagID::request( "Player" ) );
    objects.mergePendingAdds();

    SW_EXPECT_FALSE( pTrigger->activate( pPlayer ) ); // 관리자가 없다

    const string        directory = test::makeTempDirectory( "autosave_trigger" );
    AutosaveTestHarness harness;
    harness.initialize( makeAutosaveTestSettings( directory ) );
    game::bindLocalService<AutosaveManager>( &harness._manager );
    SW_EXPECT_FALSE( pTrigger->activate( pCrate ) ); // 태그가 없다
    SW_EXPECT_TRUE( pTrigger->activate( pPlayer ) );
    SW_EXPECT_TRUE( harness._manager.getPendingTrigger() == AutosaveTrigger::Checkpoint );
    SW_EXPECT_TRUE( harness._manager.getLastCheckpointId() == hashed_string( "Shrine" ) );
    SW_EXPECT_FALSE( pTrigger->activate( pPlayer ) ); // 한 번
    game::unbindLocalService<AutosaveManager>();
}
