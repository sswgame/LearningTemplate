// 문서 편집 구간의 기준 글 — 바로 기록한 편집과 끝난 구간은 기준을 끝 글로 옮기고, 쥔 동안은 기준을 두며, resetBaseline 은 쥔 구간을 버린다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Workspace/Undo/DocumentEditSpan.h"

using sw::string;
using sw::editor::DocumentEdit;
using sw::editor::DocumentEditSpan;

/**
 * @brief [DocumentEditSpanTest] 기준 글은 기록한 편집의 끝 글을 따라가고, 쥔 구간 동안은 구간 전 글에 머문다
 */
SW_TEST_CASE( DocumentEditSpanTest, BaselineFollowsRecordedEdits )
{
    DocumentEditSpan span;
    span.resetBaseline( "a" );
    SW_EXPECT_EQUAL( string( "a" ), span.getBaseline() );

    const sw::optional<DocumentEdit> direct = span.notifyEdited( "Type", false, []()
    { return string( "b" ); } );
    SW_REQUIRE_TRUE( direct.has_value() );
    SW_EXPECT_EQUAL( string( "a" ), *direct->_pBefore );
    SW_EXPECT_EQUAL( string( "b" ), span.getBaseline() );

    const sw::optional<DocumentEdit> held = span.notifyEdited( "Drag", true, []()
    { return string( "c" ); } );
    SW_EXPECT_FALSE( held.has_value() );
    SW_EXPECT_TRUE( span.isHolding() );
    SW_EXPECT_EQUAL( string( "b" ), span.getBaseline() ); // 쥔 동안은 구간 전 글이다

    const sw::optional<DocumentEdit> finished = span.finish( "d" );
    SW_REQUIRE_TRUE( finished.has_value() );
    SW_EXPECT_EQUAL( string( "b" ), *finished->_pBefore );
    SW_EXPECT_EQUAL( string( "d" ), span.getBaseline() );
    // 연속한 단계는 글을 한 벌만 든다 — 앞 단계의 끝 글 · 다음 단계의 전 글 · 기준선이 같은 버퍼다.
    SW_EXPECT_TRUE( direct->_pAfter == finished->_pBefore );
    SW_EXPECT_TRUE( &span.getBaseline() == finished->_pAfter.get() );
}

/**
 * @brief [DocumentEditSpanTest] 기준을 다시 맞추면 쥔 구간을 버린다 — 로드 · 저장 · 되돌리기 뒤에 낡은 "전" 으로 단계를 내지 않는다
 */
SW_TEST_CASE( DocumentEditSpanTest, ResetBaselineDropsTheHeldSpan )
{
    DocumentEditSpan span;
    span.resetBaseline( "a" );
    const sw::optional<DocumentEdit> held = span.notifyEdited( "Drag", true, []()
    { return string( "b" ); } );
    SW_EXPECT_FALSE( held.has_value() );
    SW_REQUIRE_TRUE( span.isHolding() );

    span.resetBaseline( "loaded" );
    SW_EXPECT_FALSE( span.isHolding() );
    SW_EXPECT_EQUAL( string( "loaded" ), span.getBaseline() );
    SW_EXPECT_FALSE( span.finish( "c" ).has_value() );
    SW_EXPECT_EQUAL( string( "loaded" ), span.getBaseline() );
}
