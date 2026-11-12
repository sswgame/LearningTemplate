// GrowthLogTest — 성장 기록: 고정 용량 링(넘치면 가장 오래된 줄을 덮고 누적 수는 이어 센다), 종류별 수, 디버그 줄(최근 것부터 · 출처)(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Object/GameObject/Tests/SceneWalkTestAccess.h"

#include "GameFramework/Runtime/Gameplay/Progression/GrowthLog.h"

using namespace sw;

/** @brief [GrowthLogTest] 줄은 출처와 함께 최근 것부터 나오고, 출처가 없는 줄은 그렇다고 적힌다 — 성장 디버거가 "왜 올랐는지" 를 보인다 */
SW_TEST_CASE( GrowthLogTest, LinesShowNewestFirstWithTheirSource )
{
    GrowthLog log;
    log.record( GrowthEventKind::Experience, "Geralt", "xp", 40, "contract griffin" );
    log.record( GrowthEventKind::LevelUp, "Geralt", "level", 3, "contract griffin" );
    log.record( GrowthEventKind::SkillRank, "Geralt", "power", 2, "" );
    SW_EXPECT_EQUAL( 3u, log.getCount() );
    SW_EXPECT_EQUAL( 1u, log.countKind( GrowthEventKind::LevelUp ) );

    DebugHUDSectionWriter writer{ sw::SceneWalkTestAccess::getKey() };
    log.writeDebugLines( writer, 8 );
    SW_REQUIRE_EQUAL( 3u, writer.getLineCount() );
    SW_EXPECT_EQUAL( string( "Geralt" ), string( writer.getLineAt( 0 )._label ) );
    SW_EXPECT_EQUAL( string( "SkillRank power 2 (no source)" ), string( writer.getLineAt( 0 )._value ) );
    SW_EXPECT_EQUAL( string( "LevelUp level 3 from contract griffin" ), string( writer.getLineAt( 1 )._value ) );

    DebugHUDSectionWriter capped{ sw::SceneWalkTestAccess::getKey() };
    log.writeDebugLines( capped, 1 );
    SW_EXPECT_EQUAL( 1u, capped.getLineCount() );
}

/** @brief [GrowthLogTest] 용량을 넘으면 가장 오래된 줄이 사라지고 누적 수는 이어 센다 — 오래 돌아도 기록이 늘지 않는다 */
SW_TEST_CASE( GrowthLogTest, RingKeepsTheNewestEntries )
{
    GrowthLog log;
    for ( int64 index = 0; index < static_cast<int64>( GrowthLog::kCapacity ) + 3; ++index )
    {
        log.record( GrowthEventKind::Gained, "party", "gold", index, "chest" );
    }
    SW_EXPECT_EQUAL( GrowthLog::kCapacity, log.getCount() );
    SW_EXPECT_EQUAL( GrowthLog::kCapacity + 3, log.getRecordedCount() );
    SW_EXPECT_EQUAL( int64{ 3 }, log.getEntry( 0 )._value );
    SW_EXPECT_EQUAL( static_cast<int64>( GrowthLog::kCapacity ) + 2, log.getEntry( GrowthLog::kCapacity - 1 )._value );

    log.clear();
    DebugHUDSectionWriter writer{ sw::SceneWalkTestAccess::getKey() };
    log.writeDebugLines( writer, 8 );
    SW_REQUIRE_EQUAL( 1u, writer.getLineCount() );
    SW_EXPECT_EQUAL( string( "no change recorded yet" ), string( writer.getLineAt( 0 )._value ) );
}
