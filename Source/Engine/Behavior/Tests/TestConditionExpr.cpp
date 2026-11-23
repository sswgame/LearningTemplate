#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Behavior/ConditionExpr.h"
#include "Engine/Behavior/ConditionRegistry.h"
#include "Engine/Behavior/RegisteredParams.h"

#include <cstddef>

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

/**
 * @brief [ConditionExprTest] 컴파일한 뒤 등록 줄이 빠지면(모듈을 내렸다) 그 줄을 부르는 동작 · 식은 부르지 않고 실패한다 — 원시 포인터로 내린 이미지의 함수를 부를 수 있었다(감사 S-14).
 *        빠지지 않은 줄을 부르는 것은 다른 줄이 빠져도 그대로 돈다(세대가 바뀌면 줄이 남았는지만 본다).
 */
SW_TEST_CASE( ConditionExprTest, CallsToAnUnregisteredEntryFailClosed )
{
    SW_EXPECT_LOG( "GameAction.Stale" );
    SW_EXPECT_LOG( "ConditionExpr.Stale" );
    TestValues    values;
    ActionContext context;
    context._world.set<TestValuesSlot>( &values );

    // 모듈 하나가 올린 줄을 흉내 낸다 — 등록자가 사라지면 줄이 빠진다.
    static const ActionEntry       s_moduleAction{ "test.moduleCount", &InstancedStructOps::of<VocabularyNoParams>(), &ActionThunk<VocabularyNoParams, &XTestUtil::runCount>::call };
    static const ConditionEntry    s_moduleCondition{ "testModuleFixed", &InstancedStructOps::of<VocabularyNoParams>(), &ConditionThunk<VocabularyNoParams, &XTestUtil::readFixed>::call };
    unique_ptr<ActionRegistrar>    pActionRegistrar    = make_unique<ActionRegistrar>( &s_moduleAction );
    unique_ptr<ConditionRegistrar> pConditionRegistrar = make_unique<ConditionRegistrar>( &s_moduleCondition );

    GameActionCall moduleCall;
    GameActionCall engineCall;
    ConditionExpr  moduleExpr;
    ConditionExpr  engineExpr;
    SW_REQUIRE_TRUE( GameActionCall::compileText( "test.moduleCount", "test", moduleCall ) );
    SW_REQUIRE_TRUE( GameActionCall::compileText( "test.count", "test", engineCall ) );
    SW_REQUIRE_TRUE( ConditionExpr::compile( "testModuleFixed == 7", "test", moduleExpr ) );
    SW_REQUIRE_TRUE( ConditionExpr::compile( "testFixed == 7", "test", engineExpr ) );
    SW_EXPECT_TRUE( moduleCall.invoke( context ) == ActionResult::Succeeded );
    SW_EXPECT_TRUE( moduleExpr.evaluate( context._world ) );

    pActionRegistrar.reset();
    pConditionRegistrar.reset();
    values._actionCount = 0;
    SW_EXPECT_TRUE( moduleCall.invoke( context ) == ActionResult::Failed );
    SW_EXPECT_EQUAL( 0, values._actionCount );
    SW_EXPECT_FALSE( moduleExpr.evaluate( context._world ) );
    SW_EXPECT_TRUE( engineCall.invoke( context ) == ActionResult::Succeeded );
    SW_EXPECT_EQUAL( 1, values._actionCount );
    SW_EXPECT_TRUE( engineExpr.evaluate( context._world ) );
}

namespace
{
    /** @brief 제어 블록을 주어진 버퍼에 두는 할당기입니다 — 일부러 남기는(놓지 않는) 공유 포인터가 힙 누수로 남지 않게 한다. */
    template <typename T>
    struct BufferAllocator
    {
        using value_type = T;

        uint8* _pBuffer;
        size_t _size;

        BufferAllocator( uint8* pBuffer, size_t size )
            : _pBuffer{ pBuffer }
            , _size{ size }
        {
        }
        template <typename U>
        BufferAllocator( const BufferAllocator<U>& other ) // NOLINT(google-explicit-constructor): 할당기 되묶기 규칙이다
            : _pBuffer{ other._pBuffer }
            , _size{ other._size }
        {
        }
        T* allocate( size_t count )
        {
            SW_ASSERT( count * sizeof( T ) <= _size );
            return reinterpret_cast<T*>( _pBuffer );
        }
        void deallocate( T* /*pItem*/, size_t /*count*/ ) {}
        template <typename U>
        bool operator==( const BufferAllocator<U>& other ) const
        {
            return _pBuffer == other._pBuffer;
        }
        template <typename U>
        bool operator!=( const BufferAllocator<U>& other ) const
        {
            return _pBuffer != other._pBuffer;
        }
    };

    /** @brief 놓일 때 수를 셉니다(값의 해제 코드가 불렸다). */
    struct CountingDeleter
    {
        int32* _pCount;
        void   operator()( const void* /*pValue*/ ) const { ++*_pCount; }
    };
} // namespace

/**
 * @brief [ConditionExprTest] 등록 줄이 빠진 뒤 놓이는 칸 값은 해제 코드를 부르지 않는다 — 그 코드는 내린 이미지에 있다(감사 S-14). 줄이 살아 있으면 평소처럼 놓는다.
 */
SW_TEST_CASE( ConditionExprTest, ParamsOfAnUnregisteredEntryAreNotReleased )
{
    static const ActionEntry          s_heldAction{ "test.heldParams", &InstancedStructOps::of<VocabularyNoParams>(), &ActionThunk<VocabularyNoParams, &XTestUtil::runCount>::call };
    constexpr size_t                  kBlockSize = 256;
    alignas( std::max_align_t ) uint8 arrAliveBlock[kBlockSize];
    alignas( std::max_align_t ) uint8 arrStaleBlock[kBlockSize];
    int32                             value       = 0;
    int32                             deleteCount = 0;
    {
        unique_ptr<ActionRegistrar>  pRegistrar = make_unique<ActionRegistrar>( &s_heldAction );
        const uint32                 generation = ActionRegistry::getRemovalGeneration();
        const shared_ptr<const void> pAlive( &value, CountingDeleter{ &deleteCount }, BufferAllocator<uint8>( arrAliveBlock, kBlockSize ) );
        const shared_ptr<const void> pStale( &value, CountingDeleter{ &deleteCount }, BufferAllocator<uint8>( arrStaleBlock, kBlockSize ) );
        RegisteredParams             alive{ pAlive, &s_heldAction, ActionEntry::getEntries(), generation };
        RegisteredParams             stale{ pStale, &s_heldAction, ActionEntry::getEntries(), generation };
        RegisteredParams             aliveCopy = alive;
        SW_EXPECT_TRUE( alive.get() == &value );
        SW_EXPECT_EQUAL( 3, static_cast<int32>( pAlive.use_count() ) ); // 원본 · 보관자 · 사본
        alive     = RegisteredParams{};
        aliveCopy = RegisteredParams{};
        SW_EXPECT_EQUAL( 1, static_cast<int32>( pAlive.use_count() ) ); // 줄이 살아 있으면 놓는다

        pRegistrar.reset(); // 모듈을 내렸다
        SW_EXPECT_FALSE( stale.isEntryRegistered() );
        SW_EXPECT_EQUAL( 2, static_cast<int32>( pStale.use_count() ) );
    } // 원본 둘(살아 있는 줄의 것)은 놓이고, 빠진 줄의 보관자는 놓지 않는다
    SW_EXPECT_EQUAL( 1, deleteCount ); // pAlive 하나만 해제됐다 — pStale 은 보관자가 일부러 남겼다
}

/**
 * @brief [ConditionExprTest] 이름이 겹친 등록은 먼저 것을 두고 거절하며, 거절된 줄을 빼도 먼저 것은 남는다. 동작은 대소문자를 보지 않고 조건은 본다.
 */
SW_TEST_CASE( ConditionExprTest, DuplicateEntryNameKeepsTheFirst )
{
    SW_EXPECT_LOG( "Registration.DuplicateName" );
    static const ActionEntry s_firstAction{ "test.duplicate", &InstancedStructOps::of<VocabularyNoParams>(), &ActionThunk<VocabularyNoParams, &XTestUtil::runCount>::call };
    static const ActionEntry s_secondAction{ "TEST.Duplicate", &InstancedStructOps::of<VocabularyNoParams>(), &ActionThunk<VocabularyNoParams, &XTestUtil::runCount>::call };
    const ActionRegistrar    firstAction{ &s_firstAction };
    SW_EXPECT_FALSE( ActionRegistry::registerEntry( &s_secondAction ) ); // 등록자로 올리면 정적 등록 거절로 세어 시작 검사를 흔든다 — 손으로 올린다
    SW_EXPECT_TRUE( ActionRegistry::find( "Test.Duplicate" ) == &s_firstAction );
    SW_EXPECT_FALSE( ActionRegistry::isRegistered( &s_secondAction ) );
    ActionRegistry::unregisterEntry( &s_secondAction );
    SW_EXPECT_TRUE( ActionRegistry::isRegistered( &s_firstAction ) );

    static const ConditionEntry s_lowerCondition{ "testDuplicate", &InstancedStructOps::of<VocabularyNoParams>(), &ConditionThunk<VocabularyNoParams, &XTestUtil::readFixed>::call };
    static const ConditionEntry s_upperCondition{ "TestDuplicate", &InstancedStructOps::of<VocabularyNoParams>(), &ConditionThunk<VocabularyNoParams, &XTestUtil::readFixed>::call };
    const ConditionRegistrar    lowerCondition{ &s_lowerCondition };
    const ConditionRegistrar    upperCondition{ &s_upperCondition };
    SW_EXPECT_TRUE( ConditionRegistry::find( "testDuplicate" ) == &s_lowerCondition );
    SW_EXPECT_TRUE( ConditionRegistry::find( "TestDuplicate" ) == &s_upperCondition );
}
