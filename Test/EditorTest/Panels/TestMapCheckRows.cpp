#include "pch.h"

#include "Editor/Panels/MapCheckRows.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    ValidationIssue makeIssue( const utf8* pLabel, const utf8* pMessage, ValidationSeverity severity )
    {
        ValidationIssue issue{};
        issue._sourceLabel = pLabel;
        issue._message     = pMessage;
        issue._typeName    = hashed_string( "sw::PointLightComponent" );
        issue._severity    = severity;
        return issue;
    }
} // namespace

/**
 * @brief [MapCheckRowsTest] 오류가 먼저, 그 안에서 오브젝트 이름 순이고, 수는 거르기 전 무게별이다
 */
SW_TEST_CASE( MapCheckRowsTest, ErrorsComeFirstAndCountsIgnoreTheFilter )
{
    const vector<ValidationIssue> listIssue = {
        makeIssue( "Lamp", "radius is 0", ValidationSeverity::Warning ),
        makeIssue( "Zeta", "radius is 0", ValidationSeverity::Error ),
        makeIssue( "Alpha", "radius is 0", ValidationSeverity::Error ),
    };
    MapCheckFilter          filter{};
    vector<ValidationIssue> listRow;
    MapCheckCounts          counts{};
    MapCheckRows::populate( listIssue, filter, listRow, counts );

    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), listRow.size() );
    SW_EXPECT_STREQ( "Alpha", listRow[0]._sourceLabel.c_str() );
    SW_EXPECT_STREQ( "Zeta", listRow[1]._sourceLabel.c_str() );
    SW_EXPECT_STREQ( "Lamp", listRow[2]._sourceLabel.c_str() );
    SW_EXPECT_EQUAL( 2u, counts._errorCount );
    SW_EXPECT_EQUAL( 1u, counts._warningCount );

    filter._bShowErrors = false;
    MapCheckRows::populate( listIssue, filter, listRow, counts );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listRow.size() );
    SW_EXPECT_STREQ( "Lamp", listRow[0]._sourceLabel.c_str() );
    SW_EXPECT_EQUAL( 2u, counts._errorCount );
}

/**
 * @brief [MapCheckRowsTest] 검색은 오브젝트 · 타입 · 프로퍼티 · 메시지 어디에 맞아도 남기고, 대소문자를 가리지 않는다
 */
SW_TEST_CASE( MapCheckRowsTest, SearchMatchesAnyColumn )
{
    vector<ValidationIssue> listIssue = {
        makeIssue( "Lamp", "radius is 0", ValidationSeverity::Error ),
        makeIssue( "Crate", "mass is negative", ValidationSeverity::Error ),
    };
    listIssue[1]._typeName = hashed_string( "sw::RigidBodyComponent" );

    MapCheckFilter filter{};
    filter._search = "RIGIDBODY";
    vector<ValidationIssue> listRow;
    MapCheckCounts          counts{};
    MapCheckRows::populate( listIssue, filter, listRow, counts );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listRow.size() );
    SW_EXPECT_STREQ( "Crate", listRow[0]._sourceLabel.c_str() );

    filter._search = "radius";
    MapCheckRows::populate( listIssue, filter, listRow, counts );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listRow.size() );
    SW_EXPECT_STREQ( "Lamp", listRow[0]._sourceLabel.c_str() );
}
