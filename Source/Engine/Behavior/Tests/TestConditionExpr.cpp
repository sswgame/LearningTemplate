#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Behavior/ConditionExpr.h"

using namespace sw;

// ConditionExprTest — 조건식 문법 · 컴파일 오류 · 평가와 동작 호출의 글 왕복(nogpu). 세계는 시험용 칸 하나로 준다.

namespace
{
    /** @brief 시험용 칸입니다. 이름 "x" 와 "y" 의 값을 듭니다. */
    struct TestValues
    {
        float64 _x{ 0.0 };
        float64 _y{ 0.0 };
        int32   _actionCount{ 0 };
    };

    struct TestValuesSlot
    {
        using Type                      = TestValues;
        static constexpr uint32 kSlotID = 0x54455354u;
    };

    struct XTestUtil
    {
        static float64 readName( const GameplayContext& context, const ConditionNameArg& args )
        {
            const TestValues* pValues = context.find<TestValuesSlot>();
            if ( pValues == nullptr )
                return 0.0;
            return args._name == hashed_string( "x" ) ? pValues->_x : ( args._name == hashed_string( "y" ) ? pValues->_y : 0.0 );
        }

        static bool readAny( const GameplayContext& /*context*/, const ConditionNameListArgs& args )
        {
            return args._listName.empty() == false;
        }

        static float64 readFixed( const GameplayContext& /*context*/, const VocabularyNoParams& /*args*/ )
        {
            return 7.0;
        }

        static constexpr uint32 kSeedModulo = 1000000u;

        static float64 readSeed( const GameplayContext& context, const VocabularyNoParams& /*args*/ )
        {
            return static_cast<float64>( context.getSeed() % kSeedModulo ); // 식의 숫자 상한 안에서 비교한다(씨앗은 32 비트 전체를 쓴다)
        }

        static ActionResult runCount( ActionContext& context, const VocabularyNoParams& /*params*/ )
        {
            TestValues* pValues = context._world.find<TestValuesSlot>();
            if ( pValues == nullptr )
                return ActionResult::Failed;
            ++pValues->_actionCount;
            return ActionResult::Succeeded;
        }

        static bool run( string_view text, const GameplayContext& context )
        {
            ConditionExpr expr;
            const bool    bCompiled = ConditionExpr::compile( text, "test", expr, "testName" );
            return bCompiled && expr.evaluate( context );
        }
    };

    SW_CONDITION( testName, ConditionNameArg, &XTestUtil::readName );
    SW_CONDITION( testAny, ConditionNameListArgs, &XTestUtil::readAny );
    SW_CONDITION( testFixed, VocabularyNoParams, &XTestUtil::readFixed );
    SW_CONDITION( testSeed, VocabularyNoParams, &XTestUtil::readSeed );
    SW_GAME_ACTION( TestCount, "test.count", VocabularyNoParams, &XTestUtil::runCount );
} // namespace

/** @brief [ConditionExprTest] 우선순위 · 괄호 · 부정 · 비교 · 음수 · 소수. 빈 식은 참이다 */
SW_TEST_CASE( ConditionExprTest, GrammarAndEvaluation )
{
    TestValues values;
    values._x = 3.0;
    GameplayContext context;
    context.set<TestValuesSlot>( &values );

    SW_EXPECT_TRUE( XTestUtil::run( "", context ) );
    SW_EXPECT_TRUE( XTestUtil::run( " \t\r\n", context ) );
    SW_EXPECT_TRUE( XTestUtil::run( "x", context ) );
    SW_EXPECT_FALSE( XTestUtil::run( "y", context ) );
    SW_EXPECT_TRUE( XTestUtil::run( "!y", context ) );
    SW_EXPECT_TRUE( XTestUtil::run( "x || y && y", context ) ); // && 먼저
    SW_EXPECT_FALSE( XTestUtil::run( "( x || y ) && y", context ) );
    SW_EXPECT_TRUE( XTestUtil::run( "x >= 3 && x < 3.5", context ) );
    SW_EXPECT_TRUE( XTestUtil::run( "-1 < 0.5", context ) );
    SW_EXPECT_TRUE( XTestUtil::run( "testName(x) == x", context ) );
    SW_EXPECT_TRUE( XTestUtil::run( "testAny(a, b)", context ) );
    SW_EXPECT_FALSE( XTestUtil::run( "testAny()", context ) );
    SW_EXPECT_TRUE( XTestUtil::run( "testFixed == 7", context ) ); // 인자 없는 등록 이름은 괄호를 생략한다
    SW_EXPECT_TRUE( XTestUtil::run( "!x == 0", context ) );        // `!` 뒤는 비교 하나다
}

/** @brief [ConditionExprTest] 산술(`+ - * /`)은 비교보다 먼저 묶이고 곱이 합보다 먼저다. 수치 칸이 같은 문법으로 값을 낸다. 0 으로 나누면 값이 없음이다 */
SW_TEST_CASE( ConditionExprTest, ArithmeticForNumericProperties )
{
    TestValues values;
    values._x = 0.25;
    values._y = 4.0;
    GameplayContext context;
    context.set<TestValuesSlot>( &values );

    const auto number = [&context]( string_view text ) -> float64
    {
        ConditionExpr expr;
        SW_EXPECT_TRUE( ConditionExpr::compile( text, "test", expr, "testName" ) );
        return expr.evaluateNumber( context );
    };
    SW_EXPECT_NEAR( 14.0, number( "2 + 3 * 4" ), 1.0e-9 );
    SW_EXPECT_NEAR( 20.0, number( "(2 + 3) * 4" ), 1.0e-9 );
    SW_EXPECT_NEAR( 15.0, number( "20 * (1 - x)" ), 1.0e-9 ); // 지역 피해 · 저항의 모양
    SW_EXPECT_NEAR( 13.0, number( "5 + y * 2" ), 1.0e-9 );
    SW_EXPECT_NEAR( 2.0, number( "y / 2" ), 1.0e-9 );
    SW_EXPECT_NEAR( 6.0, number( "3 - -3" ), 1.0e-9 );
    SW_EXPECT_NEAR( 14.0, number( "testFixed * 2" ), 1.0e-9 );
    SW_EXPECT_TRUE( XTestUtil::run( "y * 2 > 7 && x + y == 4.25", context ) );
    SW_EXPECT_TRUE( XTestUtil::run( "!(y / 0 > 0) && !(y / 0 <= 0)", context ) ); // 값이 없음은 어떤 비교에도 거짓이다

    SW_EXPECT_LOG( "ConditionExpr.Invalid" );
    for ( const utf8* pBad : { "1 +", "* 2", "x * / y", "(1 + 2", "1 + )" } )
    {
        ConditionExpr expr;
        SW_EXPECT_FALSE( ConditionExpr::compile( pBad, "test", expr, "testName" ) );
    }
}

/** @brief [ConditionExprTest] 틀린 글과 모르는 이름은 읽기 오류이고 식은 비어 있다 */
SW_TEST_CASE( ConditionExprTest, InvalidTextFailsToCompile )
{
    SW_EXPECT_LOG( "ConditionExpr.Invalid" );
    for ( const utf8* pBad : { "(x", "x)", "x &&", "|| x", "x >", "> 1", "x y", "()", "-", "-x", "3a", "99999999999", "x = 1", "x & x", "!", "noSuch(1)", "testName()", "testName(a, b)" } )
    {
        ConditionExpr expr;
        SW_EXPECT_FALSE( ConditionExpr::compile( pBad, "test", expr, "testName" ) );
        SW_EXPECT_TRUE( expr.isEmpty() );
    }

    ConditionExpr        expr;
    const expected<void> bare = ConditionExpr::tryCompile( "name", expr, nullptr ); // 그냥 이름을 넘길 조건이 없다
    SW_EXPECT_FALSE( bare.has_value() );
    SW_EXPECT_FALSE( bare.has_value() == false && bare.error().detail().empty() );

    string deep( static_cast<size_t>( ConditionExpr::kMaxDepth ) + 1, '!' );
    deep += "x";
    SW_EXPECT_FALSE( ConditionExpr::tryCompile( deep, expr, "testName" ).has_value() );
    string notTooDeep( static_cast<size_t>( ConditionExpr::kMaxDepth ) - 2, '!' );
    notTooDeep += "x";
    SW_EXPECT_TRUE( ConditionExpr::tryCompile( notTooDeep, expr, "testName" ).has_value() );
}

/** @brief [ConditionExprTest] 동작은 이름으로 찾아 칸을 읽어 부른다. 모르는 이름은 읽기 오류이고, 칸 없는 동작은 글로 돌아온다 */
SW_TEST_CASE( ConditionExprTest, ActionCallsByName )
{
    TestValues    values;
    ActionContext context;
    context._world.set<TestValuesSlot>( &values );

    GameActionCall call;
    SW_REQUIRE_TRUE( GameActionCall::compileText( "test.count", "test", call ) );
    SW_EXPECT_TRUE( call.invoke( context ) == ActionResult::Succeeded );
    SW_EXPECT_EQUAL( 1, values._actionCount );

    SW_EXPECT_LOG( "GameAction.Invalid" );
    GameActionCall unknown;
    SW_EXPECT_FALSE( GameActionCall::compileText( "no.such(1)", "test", unknown ) );
    SW_EXPECT_FALSE( unknown.isValid() );
    SW_EXPECT_TRUE( unknown.invoke( context ) == ActionResult::Failed );

    const ActionEntry* pEntry = nullptr;
    InstancedStruct    properties;
    SW_REQUIRE_TRUE( GameActionCall::tryReadProperties( "test.count()", pEntry, properties ).has_value() );
    SW_EXPECT_EQUAL( string( "test.count" ), GameActionCall::formatText( *pEntry, properties ) );
}

/** @brief [ConditionExprTest] 식 안의 호출마다 굴림 씨앗이 다르다(첫 호출은 문맥 그대로). 같은 문맥이면 몇 번 평가해도 같은 답이다 */
SW_TEST_CASE( ConditionExprTest, CallsInOneExpressionRollApart )
{
    GameplayContext context;
    context.setSeed( 12345u );
    // 호출이 하나면 씨앗은 그대로다 — 기존 데이터(스케줄 저장 · 재생)의 답이 바뀌지 않는다.
    SW_EXPECT_TRUE( XTestUtil::run( "testSeed == 12345", context ) );
    // 같은 이름을 두 번 불러도 두 자리는 다른 굴림이다. 예전에는 같은 씨앗이라 항상 같았다.
    SW_EXPECT_FALSE( XTestUtil::run( "testSeed == testSeed", context ) );
    // 둘째 자리(앞에 다른 호출이 있다)는 섞인 씨앗이다.
    SW_EXPECT_FALSE( XTestUtil::run( "testFixed == 7 && testSeed == 12345", context ) );
    // 결정적이다: 둘째 자리의 씨앗은 자리 번호 1 을 섞은 값이고, 몇 번 평가해도 같다.
    GameplayContext expected = context;
    expected.mixCallSeed( 1u );
    SW_EXPECT_NOT_EQUAL( 12345u, expected.getSeed() );
    StringBuilder<constant::buffer::kMax256> textBuilder;
    textBuilder.appendFormat( "testFixed == 7 && testSeed == %#", expected.getSeed() % XTestUtil::kSeedModulo );
    const string text{ textBuilder.view() };
    SW_EXPECT_TRUE( XTestUtil::run( text, context ) );
    SW_EXPECT_TRUE( XTestUtil::run( text, context ) );
}

/** @brief [ConditionExprTest] 동작 추적: 꺼져 있으면 남지 않고, 켜면 이름 · 결과 · 시작 표시가 고정 용량 고리에 남는다(넘으면 오래된 것이 밀린다) */
SW_TEST_CASE( ConditionExprTest, ActionTraceRecordsCalls )
{
    TestValues    values;
    ActionContext context;
    context._world.set<TestValuesSlot>( &values );
    GameActionCall call;
    SW_REQUIRE_TRUE( GameActionCall::compileText( "test.count", "test", call ) );

    ActionTrace::setEnabled( false );
    ActionTrace::clear();
    (void)call.invoke( context );
    vector<ActionTraceEntry> listEntry;
    ActionTrace::collect( listEntry );
    SW_EXPECT_TRUE( listEntry.empty() );

    ActionTrace::setEnabled( true );
    for ( uint32 index = 0; index < ActionTrace::kCapacity + 3; ++index )
    {
        (void)call.invoke( context );
    }
    ActionTrace::setEnabled( false );
    ActionTrace::collect( listEntry );
    SW_REQUIRE_EQUAL( size_t( ActionTrace::kCapacity ), listEntry.size() );
    SW_EXPECT_EQUAL( string( "test.count" ), string( listEntry.back()._arrName ) );
    SW_EXPECT_TRUE( listEntry.back()._result == ActionResult::Succeeded );
    SW_EXPECT_EQUAL( uint64( ActionTrace::kCapacity + 3 ), listEntry.back()._sequence );
    SW_EXPECT_EQUAL( uint64( 4 ), listEntry.front()._sequence );
    ActionTrace::clear();
}
