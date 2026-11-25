// ShooterPacingProfileTest — 사격장이 감독 프로필의 `effect` 낱말을 검사한다: 아는 낱말(조우의 빈 글 · Elite, 보상의 Ammo · Repair)만 든 프로필은 받고,
// 모르는 낱말이나 풀 종류에 맞지 않는 낱말이 든 프로필은 오류를 남기고 거절한다(그 프로필로는 감독이 서지 않는다). 파일 · 씬 없음.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Character/AI/Director/AIDirectorProfile.h"
#include "GameFramework/Runtime/Character/AI/SpawnDirector.h"

#include "Games/Playground/Shooter3D/ShooterGameModeComponent.h"

using namespace sw;

namespace
{
    /** @brief 조우 풀 하나 · 보상 풀 하나를 든 프로필 글입니다. 두 `%#` 는 조우 항목 · 보상 항목의 속성 글이다. */
    constexpr const utf8* kProfileFormat = R"(<AIDirector startPhase="BuildUp">
  <Phase id="BuildUp" spawnScale="1"><Exit to="Peak" minTime="5"/></Phase>
  <Phase id="Peak" spawnScale="1"><Exit to="BuildUp" minTime="5"/></Phase>
  <Pool id="surge" trigger="PhaseEnter" phase="Peak">
    <Encounter id="horde" count="3"/>
    <Encounter id="champions" %# count="2"/>
  </Pool>
  <Pool id="resupply" kind="Reward" trigger="PhaseEnter" phase="BuildUp">
    <Encounter id="crate" %#/>
  </Pool>
</AIDirector>)";

    bool hasKnownEffects( const utf8* pEncounterAttributes, const utf8* pRewardAttributes )
    {
        StringBuilder<constant::buffer::kMax2048> text;
        text.appendFormat( kProfileFormat, pEncounterAttributes, pRewardAttributes );
        AIDirectorProfile profile;
        const bool        bLoaded = profile.loadFromXMLText( text.view(), "ShooterPacingProfileTest" );
        SW_EXPECT_TRUE( bLoaded ); // 읽지 못한 프로필의 거절을 낱말 검사의 거절로 세지 않는다
        return bLoaded && ShooterGameModeComponent::hasKnownEffects( profile, "ShooterPacingProfileTest" );
    }
} // namespace

/** @brief [ShooterPacingProfileTest] 항목 id 는 데이터가 짓는다 — 낱말만 맞으면 처음 보는 id 의 프로필도 받는다 */
SW_TEST_CASE( ShooterPacingProfileTest, KnownEffectsAreAcceptedWhateverTheIDs )
{
    SW_EXPECT_TRUE( hasKnownEffects( R"(effect="Elite")", R"(effect="Ammo")" ) );
    SW_EXPECT_TRUE( hasKnownEffects( "", R"(effect="Repair" scale="2")" ) );
}

/** @brief [ShooterPacingProfileTest] 모르는 낱말 · 풀 종류에 맞지 않는 낱말 · 낱말 없는 보상은 거절한다 */
SW_TEST_CASE( ShooterPacingProfileTest, UnknownEffectsAreRefused )
{
    SW_EXPECT_LOG( "ShooterGameMode.PacingProfileUnknownEffect" );
    SW_EXPECT_FALSE( hasKnownEffects( R"(effect="Boss")", R"(effect="Ammo")" ) );   // 모르는 조우 낱말
    SW_EXPECT_FALSE( hasKnownEffects( R"(effect="Ammo")", R"(effect="Ammo")" ) );   // 보상 낱말을 조우에
    SW_EXPECT_FALSE( hasKnownEffects( R"(effect="Elite")", R"(effect="Elite")" ) ); // 조우 낱말을 보상에
    SW_EXPECT_FALSE( hasKnownEffects( "", "" ) );                                   // 보상은 무엇을 줄지 적어야 한다
}

/** @brief [ShooterPacingProfileTest] 스폰 표의 항목과 적 종류는 id 로 맞는다 — 표에 없는 id 의 종류 · 종류 없는 항목은 거절하고, 여러 종류가 한 항목을 나눠 써도 된다 */
SW_TEST_CASE( ShooterPacingProfileTest, EnemyKindsMatchTheSpawnTableByID )
{
    SW_EXPECT_LOG( "ShooterGameMode.SpawnEntryUnknown" );
    SpawnTable table;
    SW_REQUIRE_TRUE( table.loadFromXMLText( R"(<SpawnTable budgetPerMinute="1" maxBudget="1" startBudget="1">
  <Entry id="skeleton" cost="1" weight="1" max="6"/>
  <Entry id="crab" cost="1" weight="1" max="6"/>
</SpawnTable>)",
                                            "ShooterPacingProfileTest" ) );
    vector<ShooterEnemyKind> listKind( 3 );
    listKind[0]._spawnEntryID = hashed_string( "skeleton" );
    listKind[1]._spawnEntryID = hashed_string( "skeleton" );
    listKind[2]._spawnEntryID = hashed_string( "crab" );
    SW_EXPECT_TRUE( ShooterGameModeComponent::matchesSpawnTable( table, listKind, "test" ) );
    listKind[2]._spawnEntryID = hashed_string( "skeleton" ); // crab 을 맡은 종류가 없다
    SW_EXPECT_FALSE( ShooterGameModeComponent::matchesSpawnTable( table, listKind, "test" ) );
    listKind[2]._spawnEntryID = hashed_string( "crab" );
    listKind[0]._spawnEntryID = hashed_string{}; // 적지 않았다
    SW_EXPECT_FALSE( ShooterGameModeComponent::matchesSpawnTable( table, listKind, "test" ) );
}
