#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Foundation/Framework/Save/Autosave.h"

// AutosaveTest — 자동 저장 정책(순환 슬롯 · 요청 합치기 · 최소 간격 · 막기 · 종료 · 체크포인트 되돌리기). 저장 델리게이트는 케이스 폴더에 작은 파일을 쓴다(nogpu).

namespace
{
    /** @brief 저장 · 불러오기를 흉내 냅니다. 저장은 경로에 파일을 쓰고, 불러오기는 불린 경로를 적어 둡니다. */
    struct FakeGame
    {
        sw::vector<sw::string> _listSavedPath{};
        sw::string             _loadedPath{};

        sw::AutosaveManager::SaveDelegate makeSave()
        {
            return sw::AutosaveManager::SaveDelegate::create( [this]( sw::string_view path )
            {
                _listSavedPath.push_back( sw::string( path ) );
                return sw::FileUtil::writeTextFile( path, "state" );
            } );
        }
        sw::AutosaveManager::LoadDelegate makeLoad()
        {
            return sw::AutosaveManager::LoadDelegate::create( [this]( sw::string_view path )
            {
                _loadedPath = sw::string( path );
                return true;
            } );
        }
    };

    sw::AutosaveSettings makeSettings( sw::string_view folderName )
    {
        sw::AutosaveSettings settings;
        settings._directory = sw::test::makeScratchPath( folderName );
        settings._interval  = 0.0f;
        settings._slotCount = 3;
        SW_EXPECT_TRUE( sw::FileUtil::ensureDirectoryExists( settings._directory ) ); // REQUIRE 는 void 함수에서만 돌아간다
        return settings;
    }
} // namespace

/**
 * @brief [AutosaveTest] 새 저장은 가장 새 슬롯의 다음에 쓰고(가장 새 것을 건드리지 않는다), 다시 시작한 관리자는 폴더의 기록에서 순번과 슬롯을 잇는다
 */
SW_TEST_CASE( AutosaveTest, RotatesSlotsAndContinuesAcrossRuns )
{
    const sw::AutosaveSettings settings = makeSettings( "rotate" );
    FakeGame                   game;
    sw::AutosaveManager        manager;
    SW_REQUIRE_TRUE( manager.initialize( settings, game.makeSave(), game.makeLoad() ) );
    for ( int32 saveIndex = 0; saveIndex < 4; ++saveIndex )
    {
        SW_REQUIRE_TRUE( manager.saveNow( sw::AutosaveTrigger::Manual ) );
    }
    SW_REQUIRE_EQUAL( size_t( 4 ), game._listSavedPath.size() );
    SW_EXPECT_EQUAL( settings.makeSlotPath( 0 ), game._listSavedPath[3] ); // 0, 1, 2 다음은 다시 0
    SW_REQUIRE_TRUE( manager.findLatestSlot() != nullptr );
    SW_EXPECT_EQUAL( uint64( 4 ), manager.findLatestSlot()->_sequence );

    sw::AutosaveManager next;
    SW_REQUIRE_TRUE( next.initialize( settings, game.makeSave(), game.makeLoad() ) );
    SW_REQUIRE_TRUE( next.saveNow( sw::AutosaveTrigger::Manual ) );
    SW_EXPECT_EQUAL( settings.makeSlotPath( 1 ), game._listSavedPath.back() );
    SW_EXPECT_EQUAL( uint64( 5 ), next.findLatestSlot()->_sequence );
}

/**
 * @brief [AutosaveTest] 한 프레임의 요청은 하나로 합쳐 더 중요한 까닭으로 남고, 최소 간격 안의 요청은 버리지 않고 기다렸다 저장한다. 막기 중에는 기다리되 종료는 막기를 넘는다
 */
SW_TEST_CASE( AutosaveTest, MergesWaitsAndBlocks )
{
    sw::AutosaveSettings settings = makeSettings( "merge" );
    settings._minimumGap          = 30.0f;
    FakeGame            game;
    sw::AutosaveManager manager;
    SW_REQUIRE_TRUE( manager.initialize( settings, game.makeSave(), game.makeLoad() ) );

    manager.notifyAreaChanged( sw::hashed_string( "forest" ) );
    manager.reachCheckpoint( sw::hashed_string( "gate" ) );
    manager.update( 0.1f );
    SW_REQUIRE_EQUAL( 1u, manager.getSaveCount() );
    SW_EXPECT_TRUE( manager.findLatestSlot()->_trigger == sw::AutosaveTrigger::Checkpoint );
    SW_EXPECT_EQUAL( sw::hashed_string( "gate" ), manager.findLatestSlot()->_label );

    manager.notifyAreaChanged( sw::hashed_string( "cave" ) );
    manager.update( 10.0f );
    SW_EXPECT_EQUAL( 1u, manager.getSaveCount() ); // 최소 간격 안 — 기다린다
    SW_EXPECT_TRUE( manager.hasPendingRequest() );
    manager.update( 25.0f );
    SW_EXPECT_EQUAL( 2u, manager.getSaveCount() );

    manager.setBlocked( sw::hashed_string( "combat" ) );
    manager.notifyBeforeBoss( sw::hashed_string( "dragon" ) );
    manager.update( 60.0f );
    SW_EXPECT_EQUAL( 2u, manager.getSaveCount() );
    SW_EXPECT_TRUE( manager.notifyQuit() ); // 종료는 막기를 넘는다
    SW_EXPECT_EQUAL( 3u, manager.getSaveCount() );
    manager.clearBlocked( sw::hashed_string( "combat" ) );
    manager.update( 0.1f );
    SW_EXPECT_EQUAL( 3u, manager.getSaveCount() ); // 종료 저장이 쌓인 요청을 가져갔다
}

/**
 * @brief [AutosaveTest] 체크포인트 되돌리기는 가장 새 체크포인트 슬롯을 불러오고, 되돌린 체크포인트에 다시 서도 저장하지 않는다
 */
SW_TEST_CASE( AutosaveTest, RestoresTheLatestCheckpoint )
{
    sw::AutosaveSettings settings = makeSettings( "checkpoint" );
    settings._minimumGap          = 0.0f;
    FakeGame            game;
    sw::AutosaveManager manager;
    SW_REQUIRE_TRUE( manager.initialize( settings, game.makeSave(), game.makeLoad() ) );

    manager.reachCheckpoint( sw::hashed_string( "gate" ) );
    manager.update( 1.0f );
    const sw::string checkpointPath = game._listSavedPath.back();
    manager.notifyAreaChanged( sw::hashed_string( "cave" ) );
    manager.update( 1.0f );
    SW_REQUIRE_EQUAL( 2u, manager.getSaveCount() );

    SW_REQUIRE_TRUE( manager.restoreCheckpoint() );
    SW_EXPECT_EQUAL( checkpointPath, game._loadedPath );
    manager.reachCheckpoint( sw::hashed_string( "gate" ) );
    manager.update( 1.0f );
    SW_EXPECT_EQUAL( 2u, manager.getSaveCount() );

    SW_REQUIRE_TRUE( manager.restoreLatest() );
    SW_EXPECT_EQUAL( game._listSavedPath.back(), game._loadedPath );
}
