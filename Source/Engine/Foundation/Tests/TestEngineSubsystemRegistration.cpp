// 엔진 등록 목록 — 등록 → 칸 → 찾기, 필수 칸 누락 보고, 프로세스당 한 바인딩, 게임 노출 거르기, 만드는 순서(의존 · 이름) · 순환 · 모르는 타입 · 다른 단계 오류,
// 공장의 찾기가 먼저 만들 타입에 묶이는 것, 만드는 순서와 수명 의존의 대조.
// 테스트는 자기 목록(`EngineRegistrationList`)에 자기 등록을 단다 — 엔진의 전역 목록과 칸을 건드리지 않는다.
#include "pch.h"

#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/QuitRequest.h"

// 칸을 가진 타입은 외부 연결이어야 한다(칸이 형식의 정적 멤버다) — 익명 namespace 에 둘 수 없다.
namespace sw::engine_registration_test
{
    /** @brief 필수 칸 · 게임 노출로 등록하는 값입니다. */
    struct RequiredValue
    {
        int32 _value;

        SW_SERVICE_SLOT( RequiredValue, Required );
    };

    /** @brief 선택 칸 · 호스트 전용으로 등록하는 값입니다. */
    struct OptionalValue
    {
        int32 _value;

        SW_SERVICE_SLOT( OptionalValue, Optional );
    };

    /** @brief 번호 @p id 의 서브시스템 이름입니다(없는 번호는 nullptr). */
    constexpr const utf8* findOrderedName( int32 id )
    {
        return id == 0 ? "Alpha" : id == 1 ? "Bravo"
                             : id == 2     ? "Charlie"
                             : id == 3     ? "Delta"
                             : id == 4     ? "Echo"
                             : id == 5     ? "Foxtrot"
                                           : nullptr;
    }

    /** @brief 만드는 순서를 보는 서브시스템입니다(인자 없이 만든다). 이름은 형식의 상수이고, @p TDependencyID 가 0 이상이면 그 번호의 서브시스템에 수명으로 기댄다. */
    template <int32 TID, int32 TDependencyID = -1>
    class OrderedSubsystem final : public ISubsystem
    {
    public:
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::EngineMisc;
        static constexpr const utf8* kSubsystemName      = findOrderedName( TID );

        SubsystemInitResult     initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            if constexpr ( TDependencyID < 0 )
            {
                return {};
            }
            else
            {
                static constexpr const utf8* kArrDependency[] = { findOrderedName( TDependencyID ) };
                return kArrDependency;
            }
        }
    };
    using Alpha   = OrderedSubsystem<0>;
    using Bravo   = OrderedSubsystem<1>;
    using Charlie = OrderedSubsystem<2>;
    using Delta   = OrderedSubsystem<3, 0>; // Alpha 에 기댄다
    using Echo    = OrderedSubsystem<4, 3>; // Delta(그리고 간접으로 Alpha)에 기댄다
    using Foxtrot = OrderedSubsystem<5>;    // 아무것에도 기대지 않는다
} // namespace sw::engine_registration_test

namespace sw
{
    ServiceSlot engine_registration_test::RequiredValue::s_serviceSlot{ nullptr };
    ServiceSlot engine_registration_test::OptionalValue::s_serviceSlot{ nullptr };
} // namespace sw

using namespace sw;
using namespace sw::engine_registration_test;

namespace
{
    /** @brief 테스트 목록에 단 서비스 칸 등록입니다(엔진의 `SW_ENGINE_SERVICE` 와 같은 값). */
    template <class T>
    struct TestServiceRecord : engine::EngineServiceRecord
    {
        TestServiceRecord( engine::EngineRegistrationList& list, engine::EngineServiceVisibility visibility )
            : engine::EngineServiceRecord{ list,
                                           ServiceSlotOf<T>::kTypeName,
                                           getSubsystemTypeHash<T>(),
                                           ServiceSlotOf<T>::getSlot(),
                                           &engine::EngineRegistrationOps<T>::bind,
                                           ServiceSlotOf<T>::kRequirement,
                                           visibility }
        {
        }
    };

    /** @brief 테스트 목록에 단 공장 등록입니다(엔진의 `SW_ENGINE_SUBSYSTEM` 과 같은 값 — @p TAfter 를 먼저 만든다). */
    template <class T, class... TAfter>
    struct TestSubsystemRecord : engine::EngineSubsystemRecord
    {
        TestSubsystemRecord( engine::EngineRegistrationList& list, engine::EngineSubsystemStage stage )
            : engine::EngineSubsystemRecord{ list,
                                             T::kSubsystemName,
                                             getSubsystemTypeHash<T>(),
                                             stage,
                                             engine::EngineSubsystemAfter<TAfter...>::getHashes(),
                                             "(test)",
                                             &engine::EngineRegistrationOps<T>::emplace }
        {
        }
    };

    struct EngineRegistrationTestUtil
    {
        static constexpr engine::EngineSubsystemStage kStartup  = engine::EngineSubsystemStage::Startup;
        static constexpr engine::EngineSubsystemStage kServices = engine::EngineSubsystemStage::Services;

        /** @brief 오류를 `문장 (대상)` 글로 잇습니다(문장 · 대상 어느 쪽의 낱말이든 찾게). */
        static string describe( const Error& error ) { return string( error.message().data(), error.message().size() ) + " (" + string( error.detail().data(), error.detail().size() ) + ")"; }

        /** @brief 순서대로 놓인 공장의 이름을 공백으로 잇습니다. */
        static string joinNames( const vector<const engine::EngineSubsystemRecord*>& listRecord )
        {
            string joined;
            for ( const engine::EngineSubsystemRecord* const pRecord : listRecord )
            {
                if ( joined.empty() == false )
                    joined += ' ';
                joined += pRecord->_pTypeName;
            }
            return joined;
        }

        /** @brief 범위의 컬렉션에 더해진 순서(만든 순서)의 이름을 공백으로 잇습니다. */
        static string joinAddedNames( const SubsystemScope& scope )
        {
            string joined;
            for ( uint32 index = 0; index < scope.getCollection().getSubsystemCount(); ++index )
            {
                if ( joined.empty() == false )
                    joined += ' ';
                joined += scope.getCollection().getSubsystem( index ).getSubsystemName();
            }
            return joined;
        }
    };
    using Util = EngineRegistrationTestUtil;
} // namespace

/** @brief [EngineRegistrationTest] 등록한 타입의 칸이 범위에서 찾은 객체를 들고, 창구는 그 칸을 읽는다. 끊으면 비고 다시 묶을 수 있다 */
SW_TEST_CASE( EngineRegistrationTest, BindFillsSlotAndAccessReadsIt )
{
    engine::EngineRegistrationList         list;
    const TestServiceRecord<RequiredValue> requiredRecord{ list, engine::EngineServiceVisibility::GameVisible };
    const TestServiceRecord<OptionalValue> optionalRecord{ list, engine::EngineServiceVisibility::HostOnly };
    RequiredValue                          required{ 7 };
    OptionalValue                          optional{ 9 };
    SubsystemScope                         parentScope;
    SubsystemScope                         scope{ &parentScope };
    SW_REQUIRE_TRUE( parentScope.bind( &required ) ); // 부모 범위까지 찾는다
    SW_REQUIRE_TRUE( scope.bind( &optional ) );

    SW_EXPECT_TRUE( list.bindServices( scope ).isBound() );
    SW_EXPECT_TRUE( &engine::getSubsystem<RequiredValue>() == &required );
    SW_EXPECT_EQUAL( 7, engine::getSubsystem<RequiredValue>()._value );
    SW_EXPECT_TRUE( engine::findSubsystem<OptionalValue>() == &optional );
    SW_EXPECT_TRUE( engine::findSubsystem<RequiredValue>() == &required );

    list.unbindServices( scope );
    SW_EXPECT_NULL( engine::findSubsystem<RequiredValue>() );
    SW_EXPECT_NULL( engine::findSubsystem<OptionalValue>() );
}

/** @brief [EngineRegistrationTest] 필수 칸이 비면 그 타입 이름으로 알린다. 선택 칸은 비어도 알리지 않는다 */
SW_TEST_CASE( EngineRegistrationTest, MissingRequiredSlotIsReportedByName )
{
    engine::EngineRegistrationList         list;
    const TestServiceRecord<RequiredValue> requiredRecord{ list, engine::EngineServiceVisibility::GameVisible };
    const TestServiceRecord<OptionalValue> optionalRecord{ list, engine::EngineServiceVisibility::HostOnly };
    SubsystemScope                         emptyScope;
    const engine::EngineServiceBindResult  missing = list.bindServices( emptyScope );
    SW_REQUIRE_TRUE( missing._kind == engine::EngineServiceBindKind::MissingRequired );
    SW_REQUIRE_NOT_NULL( missing._pTypeName );
    SW_EXPECT_EQUAL( string( "RequiredValue" ), string( missing._pTypeName ) );
    SW_EXPECT_NULL( engine::findSubsystem<RequiredValue>() );
    list.unbindServices( emptyScope ); // 서지 못한 호스트도 끊는다 — 다음 바인딩이 거절되지 않게

    RequiredValue  required{ 1 };
    SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind( &required ) );
    SW_EXPECT_TRUE( list.bindServices( scope ).isBound() ); // 선택 칸(OptionalValue)은 비었다
    SW_EXPECT_NULL( list.findMissingRequiredService() );
    list.unbindServices( scope );
    SW_EXPECT_NOT_NULL( list.findMissingRequiredService() );
}

/** @brief [EngineRegistrationTest] 게임 모듈 범위에는 게임 노출 칸만 걸리고, 호스트 · 에디터 범위에는 모두 걸린다. 빈 칸은 걸지 않는다 */
SW_TEST_CASE( EngineRegistrationTest, ModuleScopeFiltersByVisibility )
{
    engine::EngineRegistrationList         list;
    const TestServiceRecord<RequiredValue> requiredRecord{ list, engine::EngineServiceVisibility::GameVisible };
    const TestServiceRecord<OptionalValue> optionalRecord{ list, engine::EngineServiceVisibility::HostOnly };
    RequiredValue                          required{ 1 };
    OptionalValue                          optional{ 2 };
    SubsystemScope                         scope;
    SW_REQUIRE_TRUE( scope.bind( &required ) );
    SW_REQUIRE_TRUE( scope.bind( &optional ) );
    SW_REQUIRE_TRUE( list.bindServices( scope ).isBound() );

    SubsystemScope gameScope;
    list.bindModuleServices( gameScope, true, false );
    SW_EXPECT_TRUE( gameScope.find<RequiredValue>() == &required );
    SW_EXPECT_NULL( gameScope.find<OptionalValue>() );

    SubsystemScope hostScope;
    list.bindModuleServices( hostScope, false, false );
    SW_EXPECT_TRUE( hostScope.find<RequiredValue>() == &required );
    SW_EXPECT_TRUE( hostScope.find<OptionalValue>() == &optional );

    // 이미 걸린 타입을 건너뛰는 채우기 — 테스트가 자기 값을 먼저 걸고 나머지를 채운다.
    RequiredValue  own{ 3 };
    SubsystemScope prefilledScope;
    SW_REQUIRE_TRUE( prefilledScope.bind( &own ) );
    list.bindModuleServices( prefilledScope, false, true );
    SW_EXPECT_TRUE( prefilledScope.find<RequiredValue>() == &own );
    SW_EXPECT_TRUE( prefilledScope.find<OptionalValue>() == &optional );

    list.unbindServices( scope );
    SubsystemScope unboundScope;
    list.bindModuleServices( unboundScope, false, false );
    SW_EXPECT_NULL( unboundScope.find<RequiredValue>() );
}

/** @brief [EngineRegistrationTest] 만드는 순서는 선언한 먼저 만들 타입이 정하고, 의존이 없는 것끼리는 이름 순이다(단 순서 · 링크 순서는 보지 않는다) */
SW_TEST_CASE( EngineRegistrationTest, BuildOrderFollowsDeclaredDependenciesThenName )
{
    engine::EngineRegistrationList               list;
    const TestSubsystemRecord<Echo, Delta>       echoRecord{ list, Util::kStartup };  // Echo 는 Delta 뒤
    const TestSubsystemRecord<Delta, Alpha>      deltaRecord{ list, Util::kStartup }; // Delta 는 Alpha 뒤
    const TestSubsystemRecord<Charlie>           charlieRecord{ list, Util::kStartup };
    const TestSubsystemRecord<Alpha>             alphaRecord{ list, Util::kStartup };
    const TestSubsystemRecord<Bravo>             bravoRecord{ list, Util::kServices }; // 다른 단계는 끼지 않는다
    vector<const engine::EngineSubsystemRecord*> listOrder;
    SW_REQUIRE_TRUE( list.orderSubsystems( Util::kStartup, listOrder ).has_value() );
    SW_EXPECT_EQUAL( string( "Alpha Charlie Delta Echo" ), Util::joinNames( listOrder ) );
    SW_REQUIRE_TRUE( list.orderSubsystems( Util::kServices, listOrder ).has_value() );
    SW_EXPECT_EQUAL( string( "Bravo" ), Util::joinNames( listOrder ) );

    // 공장이 그 순서로 불려 범위에 더해진다.
    SubsystemScope               scope;
    engine::EngineSubsystemBuild build{};
    list.buildSubsystems( Util::kStartup, scope, build );
    SW_EXPECT_EQUAL( string( "Alpha Charlie Delta Echo" ), Util::joinAddedNames( scope ) );
    SW_EXPECT_NOT_NULL( scope.find<Echo>() );
}

/** @brief [EngineRegistrationTest] 순환 · 등록하지 않은 타입 · 다른 단계의 타입을 먼저 만들라고 하면 순서를 정하지 않고 이유를 적는다 */
SW_TEST_CASE( EngineRegistrationTest, BuildOrderRejectsCycleUnknownAndOtherStage )
{
    {
        engine::EngineRegistrationList               list;
        const TestSubsystemRecord<Alpha, Bravo>      alphaRecord{ list, Util::kStartup };
        const TestSubsystemRecord<Bravo, Alpha>      bravoRecord{ list, Util::kStartup };
        vector<const engine::EngineSubsystemRecord*> listOrder;
        const expected<void>                         ordered = list.orderSubsystems( Util::kStartup, listOrder );
        SW_REQUIRE_FALSE( ordered.has_value() );
        const string error = Util::describe( ordered.error() );
        SW_EXPECT_TRUE_MSG( error.find( "cycle" ) != string::npos, error );
        SW_EXPECT_TRUE( listOrder.empty() );
    }
    {
        engine::EngineRegistrationList               list;
        const TestSubsystemRecord<Alpha, Charlie>    alphaRecord{ list, Util::kStartup }; // Charlie 는 등록이 없다
        vector<const engine::EngineSubsystemRecord*> listOrder;
        const expected<void>                         ordered = list.orderSubsystems( Util::kStartup, listOrder );
        SW_REQUIRE_FALSE( ordered.has_value() );
        const string error = Util::describe( ordered.error() );
        SW_EXPECT_TRUE_MSG( error.find( "no registration" ) != string::npos, error );
    }
    {
        engine::EngineRegistrationList               list;
        const TestSubsystemRecord<Alpha, Bravo>      alphaRecord{ list, Util::kStartup };
        const TestSubsystemRecord<Bravo>             bravoRecord{ list, Util::kServices }; // 범위가 다르면 공장이 찾지 못한다
        vector<const engine::EngineSubsystemRecord*> listOrder;
        const expected<void>                         ordered = list.orderSubsystems( Util::kStartup, listOrder );
        SW_REQUIRE_FALSE( ordered.has_value() );
        const string error = Util::describe( ordered.error() );
        SW_EXPECT_TRUE_MSG( error.find( "another stage" ) != string::npos, error );
    }
}

/** @brief [EngineRegistrationTest] 칸은 프로세스에 하나라 묶는 범위도 하나다 — 두 번째 바인딩은 칸을 덮지 않고 거절되고, 그 범위의 끊기는 앞 바인딩을 건드리지 않는다 */
SW_TEST_CASE( EngineRegistrationTest, SecondBindIsRejectedAndKeepsTheFirst )
{
    engine::EngineRegistrationList         list;
    const TestServiceRecord<RequiredValue> requiredRecord{ list, engine::EngineServiceVisibility::GameVisible };
    RequiredValue                          first{ 1 };
    RequiredValue                          second{ 2 };
    SubsystemScope                         firstScope;
    SubsystemScope                         secondScope;
    SW_REQUIRE_TRUE( firstScope.bind( &first ) );
    SW_REQUIRE_TRUE( secondScope.bind( &second ) );

    SW_REQUIRE_TRUE( list.bindServices( firstScope ).isBound() );
    const engine::EngineServiceBindResult again = list.bindServices( secondScope );
    SW_EXPECT_TRUE( again._kind == engine::EngineServiceBindKind::AlreadyBound );
    SW_EXPECT_NULL( again._pTypeName );
    SW_EXPECT_TRUE( &engine::getSubsystem<RequiredValue>() == &first ); // 앞 엔진의 조회가 그대로다
    SW_EXPECT_TRUE( list.findBoundScope() == &firstScope );

    list.unbindServices( secondScope ); // 묶지 못한 쪽의 종료 — 아무것도 하지 않는다
    SW_EXPECT_TRUE( engine::findSubsystem<RequiredValue>() == &first );
    list.unbindServices( firstScope );
    SW_EXPECT_NULL( engine::findSubsystem<RequiredValue>() );
    SW_EXPECT_NULL( list.findBoundScope() );
    SW_EXPECT_TRUE( list.bindServices( secondScope ).isBound() ); // 끊은 뒤에는 다시 묶인다
    SW_EXPECT_TRUE( &engine::getSubsystem<RequiredValue>() == &second );
    list.unbindServices( secondScope );
}

/** @brief [EngineRegistrationTest] 하네스의 엔진이 선 프로세스에서 엔진 서비스를 또 묶으면 거절되고 지금 엔진의 서비스가 그대로다(프로세스당 엔진 하나) */
SW_TEST_CASE( EngineRegistrationTest, RunningEngineRejectsASecondEngineBinding )
{
    SW_REQUIRE_TRUE( engine::areEngineServicesBound() );
    QuitRequest* const                    pBefore = &engine::getSubsystem<QuitRequest>();
    SubsystemScope                        otherScope;
    const engine::EngineServiceBindResult result = engine::bindEngineServices( otherScope );
    SW_EXPECT_TRUE( result._kind == engine::EngineServiceBindKind::AlreadyBound );
    engine::unbindEngineServices( otherScope ); // 두 번째 엔진의 종료 길 — 지금 엔진의 칸을 비우지 않는다
    SW_EXPECT_TRUE( engine::areEngineServicesBound() );
    SW_EXPECT_TRUE( &engine::getSubsystem<QuitRequest>() == pBefore );
    SW_EXPECT_TRUE( engine::getEngineSubsystems() != &otherScope );
}

/** @brief [EngineRegistrationTest] 공장이 받는 범위는 등록이 적은 먼저 만들 타입만 찾는다 — 먼저 서 있어도 적지 않은 타입은 찾기로 받지 않는다 */
SW_TEST_CASE( EngineRegistrationTest, FactoryScopeDeclaresOnlyTheAfterList )
{
    engine::EngineRegistrationList          list;
    const TestSubsystemRecord<Delta, Alpha> deltaRecord{ list, Util::kStartup };
    SubsystemScope                          scope;
    (void)scope.emplaceSubsystem<Alpha>();
    (void)scope.emplaceSubsystem<Bravo>(); // 먼저 서 있지만 적지 않았다
    engine::EngineSubsystemFactoryScope factoryScope{ scope, deltaRecord };
    SW_EXPECT_TRUE( factoryScope.isDeclared( getSubsystemTypeHash<Alpha>() ) );
    SW_EXPECT_FALSE( factoryScope.isDeclared( getSubsystemTypeHash<Bravo>() ) );
    SW_EXPECT_TRUE( &factoryScope.get<Alpha>() == scope.find<Alpha>() );
    SW_EXPECT_TRUE( &factoryScope.getScope() == &scope );
}

/** @brief [EngineRegistrationTest] 먼저 만든 서브시스템을 받아 만든 서브시스템은 그것에 (간접으로라도) 수명으로 기대야 한다 — 아니면 두 이름을 대며 거절한다 */
SW_TEST_CASE( EngineRegistrationTest, LifetimeDependencyMustFollowTheAfterList )
{
    engine::EngineRegistrationList            list;
    const TestSubsystemRecord<Alpha>          alphaRecord{ list, Util::kStartup };
    const TestSubsystemRecord<Delta, Alpha>   deltaRecord{ list, Util::kStartup };   // Delta 는 Alpha 에 기댄다
    const TestSubsystemRecord<Echo, Alpha>    echoRecord{ list, Util::kStartup };    // Echo 는 Delta 를 거쳐 Alpha 에 닿는다
    const TestSubsystemRecord<Foxtrot, Alpha> foxtrotRecord{ list, Util::kStartup }; // Foxtrot 은 Alpha 를 받았는데 기대지 않는다
    SubsystemScope                            scope;
    (void)scope.emplaceSubsystem<Alpha>();
    (void)scope.emplaceSubsystem<Delta>();
    (void)scope.emplaceSubsystem<Echo>();
    (void)scope.emplaceSubsystem<Foxtrot>();

    const vector<const engine::EngineSubsystemRecord*> listGood{ &alphaRecord, &deltaRecord, &echoRecord };
    const vector<vector<uint32>>                       listGoodBuilt{ { 0 }, { 1 }, { 2 } };
    SW_EXPECT_TRUE( engine::EngineRegistrationList::findLifetimeMismatch( listGood, listGoodBuilt, scope.getCollection() ).has_value() );

    const vector<const engine::EngineSubsystemRecord*> listBad{ &alphaRecord, &foxtrotRecord };
    const vector<vector<uint32>>                       listBadBuilt{ { 0 }, { 3 } };
    const expected<void>                               mismatch = engine::EngineRegistrationList::findLifetimeMismatch( listBad, listBadBuilt, scope.getCollection() );
    SW_REQUIRE_FALSE( mismatch.has_value() );
    const string error = Util::describe( mismatch.error() );
    SW_EXPECT_TRUE_MSG( error.find( "Foxtrot" ) != string::npos && error.find( "Alpha" ) != string::npos && error.find( "does not depend" ) != string::npos, error );

    // 서브시스템이 아닌 것만 건 공장(인터페이스 줄)은 대조할 것이 없다.
    const vector<vector<uint32>> listNothingBuilt{ { 0 }, {} };
    SW_EXPECT_TRUE( engine::EngineRegistrationList::findLifetimeMismatch( listBad, listNothingBuilt, scope.getCollection() ).has_value() );
}
