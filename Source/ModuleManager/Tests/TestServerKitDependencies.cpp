// ServerKitDependenciesTest — 게임 모듈이 받을 서버 키트 인터페이스를 형식 목록 하나로 적는 묶음(nogpu): 의존 이름은 목록에서 나오고, 필수 누락은 기동 오류, 선택 누락은 null.
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "ModuleAPI/Service/IServerKitLocator.h"

using namespace sw;

namespace
{
    class ITestStore
    {
    public:
        static constexpr const utf8* kProviderKit = "test.base";
        virtual ~ITestStore()                     = default;
    };

    class ITestBackfill
    {
    public:
        static constexpr const utf8* kProviderKit = "test.matchmaking";
        virtual ~ITestBackfill()                  = default;
    };

    class TestStore final : public ITestStore
    {
    };

    class TestBackfill final : public ITestBackfill
    {
    };

    /** @brief 호스트 창구 대역입니다. 비공개 가상 함수를 덮어 테스트할 범위를 내놓는다(게임 모듈은 범위를 보지 못한다). */
    class TestKitLocator final : public IServerKitLocator
    {
    public:
        const ISubsystemLocator* _pScope{ nullptr };

        [[nodiscard]] Subscription subscribeBeforeWithdraw( const Delegate<void()>& onBeforeWithdraw ) override { return _onBeforeWithdraw.add( onBeforeWithdraw ); }

    private:
        const ISubsystemLocator* findKitScope() const override { return _pScope; }

    private:
        MulticastDelegate<void()> _onBeforeWithdraw{};
    };

    using TestServerKits = ServerKitDependencies<ServerKitRequired<ITestStore>, ServerKitOptional<ITestBackfill>>;
} // namespace

/** @brief [ServerKitDependenciesTest] 의존 키트 이름은 형식 목록의 제공 키트 상수에서만 나온다 */
SW_TEST_CASE( ServerKitDependenciesTest, ProviderKitNamesComeFromTheTypeList )
{
    const span<const utf8* const> listRequired = TestServerKits::getRequiredKits();
    SW_REQUIRE_EQUAL( listRequired.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( string_view( listRequired[0] ), string_view( "test.base" ) );
    const span<const utf8* const> listOptional = TestServerKits::getOptionalKits();
    SW_REQUIRE_EQUAL( listOptional.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( string_view( listOptional[0] ), string_view( "test.matchmaking" ) );
    SW_EXPECT_EQUAL( ( ServerKitDependencies<ServerKitRequired<>, ServerKitOptional<>>::getRequiredKits().size() ), size_t( 0 ) );
}

/** @brief [ServerKitDependenciesTest] 조립이 섰는데 필수 인터페이스가 없으면 기동 확인이 제공 키트 이름으로 거절하고 묶음도 비며, 조립이 없으면 확인할 것이 없다 */
SW_TEST_CASE( ServerKitDependenciesTest, MissingRequiredInterfaceFailsStartup )
{
    TestKitLocator locator;
    SW_EXPECT_TRUE( TestServerKits::verify( &locator ).has_value() ); // 온라인 조립이 서기 전(설정 없음 · 리로드 중)
    SW_EXPECT_FALSE( TestServerKits::resolve( &locator ).has_value() );
    SW_EXPECT_TRUE( TestServerKits::verify( nullptr ).has_value() ); // 클라이언트 — 창구가 없다

    SubsystemScope kits;
    TestBackfill   backfill;
    SW_REQUIRE_TRUE( kits.bind<ITestBackfill>( &backfill ) );
    locator._pScope               = &kits;
    const expected<void> verified = TestServerKits::verify( &locator );
    SW_REQUIRE_FALSE( verified.has_value() );
    SW_EXPECT_TRUE( verified.error().kind() == ErrorKind::NotFound );
    SW_EXPECT_EQUAL( verified.error().detail(), string_view( "test.base" ) );
    SW_EXPECT_FALSE( TestServerKits::resolve( &locator ).has_value() );
}

/** @brief [ServerKitDependenciesTest] 필수가 있으면 묶음이 서고, 선택 인터페이스가 없으면 null 로 받는다 */
SW_TEST_CASE( ServerKitDependenciesTest, MissingOptionalInterfaceIsNull )
{
    SubsystemScope kits;
    TestStore      store;
    SW_REQUIRE_TRUE( kits.bind<ITestStore>( &store ) );
    TestKitLocator locator;
    locator._pScope = &kits;
    SW_EXPECT_TRUE( TestServerKits::verify( &locator ).has_value() );
    const optional<TestServerKits> withoutBackfill = TestServerKits::resolve( &locator );
    SW_REQUIRE_TRUE( withoutBackfill.has_value() );
    SW_EXPECT_TRUE( &withoutBackfill->get<ITestStore>() == &store );
    SW_EXPECT_NULL( withoutBackfill->find<ITestBackfill>() );

    TestBackfill backfill;
    SW_REQUIRE_TRUE( kits.bind<ITestBackfill>( &backfill ) );
    const optional<TestServerKits> withBackfill = TestServerKits::resolve( &locator );
    SW_REQUIRE_TRUE( withBackfill.has_value() );
    SW_EXPECT_TRUE( withBackfill->find<ITestBackfill>() == &backfill );
}
