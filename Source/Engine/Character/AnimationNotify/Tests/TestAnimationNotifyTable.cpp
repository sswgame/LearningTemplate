// AnimationNotifyTableTest — 알림 테이블(`*.notifies.xml`)의 `<FootContact>` 줄 읽기(칸은 AnimationFootContactDef 의 PROPERTY).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Character/AnimationNotify/AnimationNotifyTable.h"

using namespace sw;

/**
 * @brief [AnimationNotifyTableTest] 발 닿음 — 밑줄 이름 칸(`contact_height` 등)을 읽고, 읽은 값을 그대로 둔다
 */
SW_TEST_CASE( AnimationNotifyTableTest, FootContactReadsUnderscoreFields )
{
    const AnimationNotifyHandlerRegistry& registry = AnimationNotifyHandlerRegistry::getDefault();
    AnimationNotifyTable                  table;
    SW_REQUIRE_TRUE( table.loadFromXMLText( R"(<AnimationNotifies>
        <Notify name="FootL" handler="Footstep" socket="foot_l"/>
        <FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.12" max_vertical_speed="0.6"/>
    </AnimationNotifies>)",
                                            "good", registry ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( table.getFootContacts().size() ) );
    const AnimationFootContactDef& def = table.getFootContacts()[0];
    SW_EXPECT_TRUE( def._bone == hashed_string( "foot.l" ) );
    SW_EXPECT_NEAR( 0.05f, def._contactHeight, 1e-6f );
    SW_EXPECT_NEAR( 0.12f, def._releaseHeight, 1e-6f );
    SW_EXPECT_NEAR( 0.6f, def._maxVerticalSpeed, 1e-6f );
}

namespace
{
    /** @brief 표에 `<FootContact>` 한 줄을 붙여 읽습니다(알림 `FootL` 은 늘 있다). */
    bool loadFootContactLine( const utf8* pLine )
    {
        AnimationNotifyTable table;
        const string         xml = string( R"(<AnimationNotifies><Notify name="FootL" handler="Footstep" socket="foot_l"/>)" ) + pLine + "</AnimationNotifies>";
        return table.loadFromXMLText( xml, "bad", AnimationNotifyHandlerRegistry::getDefault() );
    }
} // namespace

/** @brief [AnimationNotifyTableTest] 발 닿음 — 필수 칸이 빠지면 읽기 오류다. 적지 않은 칸의 초기값(0)은 범위 오류를 덧붙이지 않는다 */
SW_TEST_CASE( AnimationNotifyTableTest, FootContactRejectsMissingField )
{
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    SW_EXPECT_FALSE( loadFootContactLine( R"(<FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.12"/>)" ) );
}

/** @brief [AnimationNotifyTableTest] 발 닿음 — 세로 속도 0 은 PROPERTY( Min ) 밖이다 */
SW_TEST_CASE( AnimationNotifyTableTest, FootContactRejectsZeroSpeed )
{
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_FALSE( loadFootContactLine( R"(<FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.12" max_vertical_speed="0"/>)" ) );
}

/** @brief [AnimationNotifyTableTest] 발 닿음 — 놓는 높이가 닿는 높이 위여야 한다 */
SW_TEST_CASE( AnimationNotifyTableTest, FootContactRejectsHeightOrder )
{
    SW_EXPECT_LOG( "Character.FootContactHeightOrder" );
    SW_EXPECT_FALSE( loadFootContactLine( R"(<FootContact notify="FootL" bone="foot.l" contact_height="0.2" release_height="0.12" max_vertical_speed="0.6"/>)" ) );
}
