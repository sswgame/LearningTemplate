#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

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

    /** @brief 인터페이스를 상속하지 않고 멤버로 든 서브시스템입니다(`publishPart`). */
    class ScopePartOwnerSubsystem final : public ISubsystem
    {
    public:
        /** @brief 멤버로 드는 인터페이스 구현입니다. */
        class Part final : public IScopeProbe
        {
        public:
            int32 getValue() const override { return 9; }
        };

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return "ScopePartOwner"; }
        Part&               getPart() { return _part; }

    private:
        Part _part;
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

/**
 * @brief [SubsystemScopeTest] 매단 자식 범위가 공개한 칸만 부모의 조회에 보이고, 떼거나 부모를 지우면 자식 서브시스템이 함께 사라진다
 */
SW_TEST_CASE( SubsystemScopeTest, MountedChildPublishesIntoTheParent )
{
    bool           bDestroyed = false;
    SubsystemScope parent;
    {
        SubsystemScope&      child = parent.mountChildScope( "ModuleA" );
        ScopeProbeSubsystem& probe = child.addSubsystem( make_unique<ScopeProbeSubsystem>( &bDestroyed ) );
        SW_EXPECT_TRUE( &parent.mountChildScope( "ModuleA" ) == &child ); // 같은 이름은 같은 자식이다
        SW_EXPECT_TRUE( parent.find<ScopeProbeSubsystem>() == nullptr );  // 공개하기 전에는 부모가 보지 못한다
        SW_EXPECT_TRUE( child.publish<IScopeProbe>( probe ) );
        SW_EXPECT_TRUE( child.publish<ScopeProbeSubsystem>( probe ) ); // 제 타입은 공개 표시만 더한다
        SW_EXPECT_EQUAL( 7, parent.find<IScopeProbe>()->getValue() );
        SW_EXPECT_TRUE( parent.find<ScopeProbeSubsystem>() == &probe );
        SW_EXPECT_TRUE( child.getParentScope() == &parent ); // 자식은 위로 찾는다
    }
    ScopeCatalog catalog{ 5 };
    SW_EXPECT_TRUE( parent.bind( &catalog ) );
    SW_EXPECT_TRUE( locateSubsystem<ScopeCatalog>( parent.findChildScope( "ModuleA" ) ) == &catalog );

    parent.unmountChildScope( "ModuleA" );
    SW_EXPECT_TRUE( bDestroyed );
    SW_EXPECT_TRUE( parent.findChildScope( "ModuleA" ) == nullptr );
    SW_EXPECT_TRUE( parent.find<IScopeProbe>() == nullptr );

    bool bSecondDestroyed = false;
    parent.mountChildScope( "ModuleB" ).addSubsystem( make_unique<ScopeProbeSubsystem>( &bSecondDestroyed ) );
    parent.destroyAll();
    SW_EXPECT_TRUE( bSecondDestroyed ); // 부모를 지우면 매단 자식도 먼저 사라진다
    SW_EXPECT_TRUE( parent.find<ScopeCatalog>() == &catalog );
}

/**
 * @brief [SubsystemScopeTest] 소유한 서브시스템이 멤버로 든 부분을 공개하면 부모가 찾고, 자식 범위를 비우면 그 칸도 함께 사라진다
 */
SW_TEST_CASE( SubsystemScopeTest, PublishedPartFollowsItsOwner )
{
    SubsystemScope           parent;
    SubsystemScope&          child = parent.mountChildScope( "ModuleA" );
    ScopePartOwnerSubsystem& owner = child.addSubsystem( make_unique<ScopePartOwnerSubsystem>() );
    SW_EXPECT_TRUE( child.publishPart<IScopeProbe>( owner, owner.getPart() ) );
    SW_EXPECT_EQUAL( 9, parent.find<IScopeProbe>()->getValue() );

    child.destroyAll();
    SW_EXPECT_TRUE( parent.find<IScopeProbe>() == nullptr ); // 주인의 칸처럼 소유 칸이라 지운 멤버를 가리키지 않는다
}
