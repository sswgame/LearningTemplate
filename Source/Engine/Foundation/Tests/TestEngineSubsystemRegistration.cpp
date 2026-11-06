// 엔진 등록 목록 — 등록 → 칸 → 찾기, 필수 칸 누락 보고, 게임 노출 거르기, 만드는 순서(의존 · 이름) · 순환 · 모르는 타입 · 다른 단계 오류.
// 테스트는 자기 목록(`EngineRegistrationList`)에 자기 등록을 단다 — 엔진의 전역 목록과 칸을 건드리지 않는다.
#include "pch.h"

#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

// 칸을 가진 타입은 외부 연결이어야 한다(칸이 `SW_API` 정적 멤버다) — 익명 namespace 에 둘 수 없다.
namespace sw::engine_registration_test
{
    /** @brief 필수 칸 · 게임 노출로 등록하는 값입니다. */
    struct RequiredValue
    {
        int32 _value;
    };

    /** @brief 선택 칸 · 호스트 전용으로 등록하는 값입니다. */
    struct OptionalValue
    {
        int32 _value;
    };

    /** @brief 만드는 순서를 보는 서브시스템입니다(인자 없이 만든다). 이름은 형식의 상수다. */
    template <int32 TId>
    class OrderedSubsystem final : public ISubsystem
    {
    public:
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::EngineMisc;
        static constexpr const utf8* kSubsystemName      = TId == 0 ? "Alpha" : TId == 1 ? "Bravo"
                                                                          : TId == 2     ? "Charlie"
                                                                          : TId == 3     ? "Delta"
                                                                                         : "Echo";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
    };
    using Alpha   = OrderedSubsystem<0>;
    using Bravo   = OrderedSubsystem<1>;
    using Charlie = OrderedSubsystem<2>;
    using Delta   = OrderedSubsystem<3>;
    using Echo    = OrderedSubsystem<4>;
} // namespace sw::engine_registration_test

namespace sw
{
    SW_SERVICE_SLOT( engine_registration_test::RequiredValue, Required );
    SW_SERVICE_SLOT( engine_registration_test::OptionalValue, Optional );
    ServiceSlot ServiceSlotOf<engine_registration_test::RequiredValue>::s_slot{ nullptr };
    ServiceSlot ServiceSlotOf<engine_registration_test::OptionalValue>::s_slot{ nullptr };
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
                                           ServiceSlotOf<T>::s_slot,
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

    SW_EXPECT_NULL( list.bindServices( scope ) );
    SW_EXPECT_TRUE( &engine::getSubsystem<RequiredValue>() == &required );
    SW_EXPECT_EQUAL( 7, engine::getSubsystem<RequiredValue>()._value );
    SW_EXPECT_TRUE( engine::findSubsystem<OptionalValue>() == &optional );
    SW_EXPECT_TRUE( engine::findSubsystem<RequiredValue>() == &required );

    list.unbindServices();
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
    const utf8* const                      pMissing = list.bindServices( emptyScope );
    SW_REQUIRE_NOT_NULL( pMissing );
    SW_EXPECT_EQUAL( string( "engine_registration_test::RequiredValue" ), string( pMissing ) );
    SW_EXPECT_NULL( engine::findSubsystem<RequiredValue>() );

    RequiredValue  required{ 1 };
    SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind( &required ) );
    SW_EXPECT_NULL( list.bindServices( scope ) ); // 선택 칸(OptionalValue)은 비었다
    SW_EXPECT_NULL( list.findMissingRequiredService() );
    list.unbindServices();
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
    SW_REQUIRE_TRUE( list.bindServices( scope ) == nullptr );

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

    list.unbindServices();
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
    string                                       error;
    SW_REQUIRE_TRUE_MSG( list.orderSubsystems( Util::kStartup, listOrder, error ), error );
    SW_EXPECT_EQUAL( string( "Alpha Charlie Delta Echo" ), Util::joinNames( listOrder ) );
    SW_REQUIRE_TRUE_MSG( list.orderSubsystems( Util::kServices, listOrder, error ), error );
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
        string                                       error;
        SW_EXPECT_FALSE( list.orderSubsystems( Util::kStartup, listOrder, error ) );
        SW_EXPECT_TRUE_MSG( error.find( "cycle" ) != string::npos, error );
        SW_EXPECT_TRUE( listOrder.empty() );
    }
    {
        engine::EngineRegistrationList               list;
        const TestSubsystemRecord<Alpha, Charlie>    alphaRecord{ list, Util::kStartup }; // Charlie 는 등록이 없다
        vector<const engine::EngineSubsystemRecord*> listOrder;
        string                                       error;
        SW_EXPECT_FALSE( list.orderSubsystems( Util::kStartup, listOrder, error ) );
        SW_EXPECT_TRUE_MSG( error.find( "no registration" ) != string::npos, error );
    }
    {
        engine::EngineRegistrationList               list;
        const TestSubsystemRecord<Alpha, Bravo>      alphaRecord{ list, Util::kStartup };
        const TestSubsystemRecord<Bravo>             bravoRecord{ list, Util::kServices }; // 범위가 다르면 공장이 찾지 못한다
        vector<const engine::EngineSubsystemRecord*> listOrder;
        string                                       error;
        SW_EXPECT_FALSE( list.orderSubsystems( Util::kStartup, listOrder, error ) );
        SW_EXPECT_TRUE_MSG( error.find( "another stage" ) != string::npos, error );
    }
}
