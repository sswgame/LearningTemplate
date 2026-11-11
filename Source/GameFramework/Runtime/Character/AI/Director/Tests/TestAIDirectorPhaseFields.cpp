#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

#include "GameFramework/Runtime/Character/AI/Director/AIDirectorProfile.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"

using namespace sw;

// 이름 목록 칸(`vector<hashed_string>`)은 속성 하나에 낱말을 잇는다 — 데이터가 쓰는 구분자(쉼표 · 세미콜론 · 세로줄 · 공백)를 모두 받는다.
SW_TEST_CASE( XMLFieldsNameListTest, ReadsEveryDataSeparatorIntoTheList )
{
    XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( "<Phase id=\"BuildUp\" spawnTags=\"Skeleton, Boss;Elite|Scout Runner\"/>" ) );
    AIDirectorPhaseDef phase;
    SW_REQUIRE_TRUE( XMLFields::readProperties( document.getRoot(), phase, "test" ) );
    SW_REQUIRE_EQUAL( 5u, static_cast<uint32>( phase._listSpawnTag.size() ) );
    SW_EXPECT_TRUE( phase._listSpawnTag[0] == hashed_string( "Skeleton" ) );
    SW_EXPECT_TRUE( phase._listSpawnTag[4] == hashed_string( "Runner" ) );
}

// 한 구조체에 이름 목록 칸이 둘이면 속성마다 제 칸으로 간다 — 표식이 다음 칸에 붙으면 참가자가 아키타입으로 읽힌다.
SW_TEST_CASE( XMLFieldsNameListTest, EachAttributeGoesToItsOwnListField )
{
    XMLDocument eventDocument;
    SW_REQUIRE_TRUE( eventDocument.parse( "<Event id=\"e\" npcs=\"a,b\" archetypes=\"c\"/>" ) );
    ScheduleEventDef event;
    SW_REQUIRE_TRUE( XMLFields::readProperties( eventDocument.getRoot(), event, "test" ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( event._listNPC.size() ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( event._listArchetype.size() ) );
    SW_EXPECT_TRUE( event._listArchetype[0] == hashed_string( "c" ) );

    XMLDocument encounterDocument;
    SW_REQUIRE_TRUE( encounterDocument.parse( "<Encounter id=\"e\" areas=\"a,b\" pacing=\"c\"/>" ) );
    AIDirectorEncounterDef encounter;
    SW_REQUIRE_TRUE( XMLFields::readProperties( encounterDocument.getRoot(), encounter, "test" ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( encounter._listArea.size() ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( encounter._listPacing.size() ) );
    SW_EXPECT_TRUE( encounter._listPacing[0] == hashed_string( "c" ) );
}
