#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Gameplay/ActionRegistry.h"
#include "Engine/Gameplay/ConditionExpr.h"

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

    string        error;
    ConditionExpr expr;
    SW_EXPECT_FALSE( ConditionExpr::tryCompile( "name", expr, error, nullptr ) ); // 그냥 이름을 넘길 조건이 없다
    SW_EXPECT_FALSE( error.empty() );

    string deep( static_cast<size_t>( ConditionExpr::kMaxDepth ) + 1, '!' );
    deep += "x";
    SW_EXPECT_FALSE( ConditionExpr::tryCompile( deep, expr, error, "testName" ) );
    string notTooDeep( static_cast<size_t>( ConditionExpr::kMaxDepth ) - 2, '!' );
    notTooDeep += "x";
    SW_EXPECT_TRUE( ConditionExpr::tryCompile( notTooDeep, expr, error, "testName" ) );
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
    InstancedStruct    fields;
    string             error;
    SW_REQUIRE_TRUE( GameActionCall::tryReadFields( "test.count()", pEntry, fields, error ) );
    SW_EXPECT_EQUAL( string( "test.count" ), GameActionCall::formatText( *pEntry, fields ) );
}
