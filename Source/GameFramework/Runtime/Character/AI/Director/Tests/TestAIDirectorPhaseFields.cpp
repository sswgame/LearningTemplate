#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

#include "GameFramework/Runtime/Character/AI/Director/AIDirectorProfile.h"

using namespace sw;

// 이름 목록 칸(`vector<hashed_string>`)은 속성 하나에 낱말을 잇는다 — 데이터가 쓰는 구분자(쉼표 · 세미콜론 · 세로줄 · 공백)를 모두 받는다.
SW_TEST_CASE( XMLFieldsNameListTest, ReadsEveryDataSeparatorIntoTheList )
{
    XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( "<Phase id=\"BuildUp\" spawnTags=\"Skeleton, Boss;Elite|Scout Runner\"/>" ) );
    AIDirectorPhaseDef phase;
    SW_REQUIRE_TRUE( XMLFields::readFields( document.getRoot(), phase, "test" ) );
    SW_REQUIRE_EQUAL( 5u, static_cast<uint32>( phase._listSpawnTag.size() ) );
    SW_EXPECT_TRUE( phase._listSpawnTag[0] == hashed_string( "Skeleton" ) );
    SW_EXPECT_TRUE( phase._listSpawnTag[4] == hashed_string( "Runner" ) );
}
