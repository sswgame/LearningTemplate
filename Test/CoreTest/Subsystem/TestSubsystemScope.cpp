#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 조회 대상 인터페이스입니다. */
    class IScopeProbe
    {
    public:
        virtual ~IScopeProbe()         = default;
        virtual int32 getValue() const = 0;
    };

    /** @brief 아무 일도 하지 않는 서브시스템입니다(조회만 본다). 소멸하면 @p pDestroyed 를 켭니다. */
    class ScopeProbeSubsystem final : public ISubsystem, public IScopeProbe
    {
    public:
        explicit ScopeProbeSubsystem( bool* pDestroyed )
            : _pDestroyed{ pDestroyed }
        {
        }
        ~ScopeProbeSubsystem() override
        {
            if ( _pDestroyed != nullptr )
                *_pDestroyed = true;
        }
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return "ScopeProbe"; }
        int32               getValue() const override { return 7; }

    private:
        bool* _pDestroyed;
    };

    /** @brief 빌려 거는 객체입니다(카탈로그 · 테스트 스택 객체 자리). */
    struct ScopeCatalog
    {
        int32 _value{ 0 };
    };
} // namespace

/**
 * @brief [SubsystemScopeTest] 소유한 서브시스템과 빌려 건 객체를 타입으로 찾고, 인터페이스로도 드러낸다
 */
SW_TEST_CASE( SubsystemScopeTest, FindsOwnedAndBorrowedByType )
{
    SubsystemScope       scope;
    ScopeProbeSubsystem& probe = scope.addSubsystem( make_unique<ScopeProbeSubsystem>( nullptr ) );
    SW_EXPECT_TRUE( scope.find<ScopeProbeSubsystem>() == &probe );
    SW_EXPECT_TRUE( &scope.get<ScopeProbeSubsystem>() == &probe );
    SW_EXPECT_TRUE( scope.find<IScopeProbe>() == nullptr ); // 드러내기 전에는 인터페이스로 찾지 못한다
    SW_EXPECT_TRUE( scope.expose<IScopeProbe>( probe ) );
    SW_EXPECT_EQUAL( 7, scope.find<IScopeProbe>()->getValue() );
    SW_EXPECT_FALSE( scope.expose<IScopeProbe>( probe ) ); // 같은 인터페이스를 두 번 걸지 않는다

    ScopeCatalog catalog{ 3 };
    SW_EXPECT_TRUE( scope.bind( &catalog ) );
    SW_EXPECT_TRUE( scope.find<ScopeCatalog>() == &catalog );
    SW_EXPECT_TRUE( locateSubsystem<const ScopeCatalog>( &scope ) == &catalog ); // const 로 찾아도 같은 칸이다

    ScopeCatalog other{ 4 };
    SW_EXPECT_FALSE( scope.bind( &other ) ); // 같은 타입이 걸려 있으면 거절 — 나중 것이 앞 것을 소리 없이 덮지 않는다
    SW_EXPECT_TRUE( scope.find<ScopeCatalog>() == &catalog );
    scope.unbind<ScopeCatalog>();
    SW_EXPECT_TRUE( scope.find<ScopeCatalog>() == nullptr );
    SW_EXPECT_TRUE( scope.bind( &other ) );
}

/**
 * @brief [SubsystemScopeTest] 이 범위에 없으면 상위 범위에서 찾고, 하위 범위가 같은 타입을 걸면 그것이 이긴다
 */
SW_TEST_CASE( SubsystemScopeTest, LookupFallsBackToTheParentScope )
{
    SubsystemScope parent;
    SubsystemScope child( &parent );
    ScopeCatalog   parentCatalog{ 1 };
    ScopeCatalog   childCatalog{ 2 };
    SW_EXPECT_TRUE( parent.bind( &parentCatalog ) );
    SW_EXPECT_TRUE( child.find<ScopeCatalog>() == nullptr );                     // find 는 이 범위만 본다
    SW_EXPECT_TRUE( locateSubsystem<ScopeCatalog>( &child ) == &parentCatalog ); // locate 는 상위로 올라간다
    SW_EXPECT_TRUE( child.bind( &childCatalog ) );
    SW_EXPECT_TRUE( locateSubsystem<ScopeCatalog>( &child ) == &childCatalog );   // 가까운 범위가 이긴다
    SW_EXPECT_TRUE( locateSubsystem<ScopeCatalog>( &parent ) == &parentCatalog ); // 상위는 하위를 보지 않는다
    SW_EXPECT_TRUE( locateSubsystem<ScopeCatalog>( nullptr ) == nullptr );
}

/**
 * @brief [SubsystemScopeTest] destroyAll 은 소유한 서브시스템과 그 칸(드러낸 인터페이스 포함)을 지우고 빌려 건 객체는 남긴다
 */
SW_TEST_CASE( SubsystemScopeTest, DestroyAllDropsOwnedEntriesOnly )
{
    bool                 bDestroyed = false;
    SubsystemScope       scope;
    ScopeProbeSubsystem& probe = scope.addSubsystem( make_unique<ScopeProbeSubsystem>( &bDestroyed ) );
    SW_EXPECT_TRUE( scope.expose<IScopeProbe>( probe ) );
    ScopeCatalog catalog{};
    SW_EXPECT_TRUE( scope.bind( &catalog ) );

    scope.destroyAll();
    SW_EXPECT_TRUE( bDestroyed );
    SW_EXPECT_TRUE( scope.find<ScopeProbeSubsystem>() == nullptr );
    SW_EXPECT_TRUE( scope.find<IScopeProbe>() == nullptr ); // 드러낸 칸도 함께 사라진다(지운 객체를 가리키지 않는다)
    SW_EXPECT_TRUE( scope.find<ScopeCatalog>() == &catalog );
}
