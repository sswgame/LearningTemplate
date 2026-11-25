#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 멤버 부분으로 공개하는 조회 대상 인터페이스입니다(`publishPart`). */
    class IScopeProbe
    {
    public:
        virtual ~IScopeProbe()         = default;
        virtual int32 getValue() const = 0;
    };

    /**
     * @brief 서브시스템 자체를 드러내는 조회 대상 인터페이스입니다(`expose` · `publish`).
     * @details 서브시스템 인터페이스라 `Subsystem` 을 기반으로 둔다(`IEditorPanelService` 와 같은 모양) — 구현이 `Subsystem` 과 함께 다중 상속하지 않는다.
     */
    class IScopeProbeSubsystem : public Subsystem
    {
    public:
        virtual int32 getValue() const = 0;
    };

    /** @brief 아무 일도 하지 않는 서브시스템입니다(조회만 본다). 소멸하면 @p pDestroyed 를 켭니다. */
    class ScopeProbeSubsystem final : public IScopeProbeSubsystem
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

    /** @brief 게임 인스턴스 수명의 상태를 든 서브시스템입니다 — 두 함수를 재정의한다. */
    class ScopeInstanceSubsystem final : public Subsystem
    {
    public:
        uint32 _queuedCount{ 0 };

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return "ScopeInstance"; }
        void                resetGameInstanceState( GameInstanceBoundary boundary ) override
        {
            if ( boundary == GameInstanceBoundary::Ended )
                _queuedCount = 0;
        }
        const utf8* findGameInstanceState() const override { return _queuedCount == 0 ? nullptr : "queued items"; }
    };

    /** @brief 하나만 재정의한 서브시스템입니다 — 어느 수명으로도 등록할 수 없다. */
    class ScopeHalfInstanceSubsystem final : public Subsystem
    {
    public:
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return "ScopeHalfInstance"; }
        void                resetGameInstanceState( GameInstanceBoundary boundary ) override { (void)boundary; }
    };

    // 등록 줄의 수명 검사(`SW_ENGINE_SUBSYSTEM` · `SW_MODULE_SUBSYSTEM` 의 static_assert)가 보는 판정이다.
    static_assert( kSubsystemLifetimeMatches<ScopeProbeSubsystem, SubsystemLifetime::Process> );
    static_assert( kSubsystemLifetimeMatches<ScopeProbeSubsystem, SubsystemLifetime::GameInstance> == false );
    static_assert( kSubsystemLifetimeMatches<ScopeInstanceSubsystem, SubsystemLifetime::GameInstance> );
    static_assert( kSubsystemLifetimeMatches<ScopeInstanceSubsystem, SubsystemLifetime::Process> == false );
    static_assert( kSubsystemLifetimeMatches<ScopeHalfInstanceSubsystem, SubsystemLifetime::GameInstance> == false );
    static_assert( kSubsystemLifetimeMatches<ScopeHalfInstanceSubsystem, SubsystemLifetime::Process> == false );
    static_assert( kSubsystemLifetimeMatches<IScopeProbe, SubsystemLifetime::Process> ); // 서브시스템이 아닌 창구

    /** @brief 인터페이스를 상속하지 않고 멤버로 든 서브시스템입니다(`publishPart`). */
    class ScopePartOwnerSubsystem final : public Subsystem
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

    /** @brief 값 묶음을 멤버로 들고 그것을 범위에 거는 서브시스템입니다(설정 · 타입 표를 든 서브시스템 자리 — `exposeMember`). */
    class ScopeCatalogOwnerSubsystem final : public Subsystem
    {
    public:
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return "ScopeCatalogOwner"; }
        ScopeCatalog&       getCatalog() { return _catalog; }

    private:
        ScopeCatalog _catalog{ 5 };
    };

    /**
     * @brief 초기화에서 같은 범위의 @p TTarget 을 찾는 서브시스템입니다 — 의존 선언(@p listDependency)이 그 조회를 덮는지 본다. 종료가 불리면 @p pShutDown 을 켭니다.
     * @tparam TTag 한 범위에 둘을 걸 때 타입을 가른다 — 범위는 타입마다 하나만 등록한다.
     */
    template <int32 TTag, class TTarget = ScopeProbeSubsystem>
    class ScopeLookupSubsystem final : public Subsystem
    {
    public:
        ScopeLookupSubsystem( const SubsystemScope& scope, const utf8* pName, vector<const utf8*> listDependency, bool* pShutDown )
            : _scope{ scope }
            , _pName{ pName }
            , _listDependency{ std::move( listDependency ) }
            , _pShutDown{ pShutDown }
        {
        }
        SubsystemInitResult initializeSubsystem() override
        {
            (void)_scope.get<TTarget>();
            return SubsystemInitResult::Succeeded;
        }
        void                    shutdownSubsystem() override { *_pShutDown = true; }
        const utf8*             getSubsystemName() const override { return _pName; }
        span<const utf8* const> getSubsystemDependencies() const override { return _listDependency; }

    private:
        const SubsystemScope& _scope;
        const utf8*           _pName;
        vector<const utf8*>   _listDependency;
        bool*                 _pShutDown;
    };

    /** @brief 의존만 잇는 빈 서브시스템입니다(간접 의존을 본다). */
    class ScopeRelaySubsystem final : public Subsystem
    {
    public:
        SubsystemInitResult     initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return "ScopeRelay"; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { "ScopeProbe" };
            return kArrDependency;
        }
    };
} // namespace

/**
 * @brief [SubsystemScopeTest] 초기화에서 같은 목록의 서브시스템을 의존 선언 없이 찾으면 기동이 지고, 오류가 두 이름을 적으며, 선 것은 종료가 불린다
 * @details 찾는 쪽 이름("ZLookup")이 뒤라 이름 순서로는 대상이 먼저 선다 — 우연히 맞는 순서여도 선언이 빠졌으면 진다(서브시스템 하나를 더하면 순서가 바뀐다).
 */
SW_TEST_CASE( SubsystemScopeTest, UndeclaredLookupDuringInitializeFailsStartup )
{
    bool           bShutDown = false;
    SubsystemScope scope;
    (void)scope.emplaceSubsystem<ScopeProbeSubsystem>( nullptr );
    (void)scope.emplaceSubsystem<ScopeLookupSubsystem<0>>( scope, "ZLookup", vector<const utf8*>{}, &bShutDown );
    SW_EXPECT_FALSE( scope.getCollection().initializeAll( SubsystemHostMask::All ) );
    const string& error = scope.getCollection().getError();
    SW_EXPECT_TRUE_MSG( StringUtil::contains( error, "'ZLookup' failed to initialize" ), error.c_str() );
    SW_EXPECT_TRUE_MSG( StringUtil::contains( error, "looked up 'ScopeProbe' without declaring it as a dependency" ), error.c_str() );
    SW_EXPECT_TRUE( bShutDown );                                                          // 선 것은 내린다 — 종료 목록에서 빠져도 자원을 쥔 채 남지 않는다
    SW_EXPECT_EQUAL( size_t( 1 ), scope.getCollection().getInitializedIndices().size() ); // 대상만 선 채로 남아 `shutdownAll` 이 내린다
}

/**
 * @brief [SubsystemScopeTest] 서브시스템의 멤버를 `exposeMember` 로 건 칸은 주인이 그 서브시스템이다 — 선언 없이 찾으면 기동이 지고, 선언하면 서며, 주인과 함께 지워진다
 * @details `bind` 로 걸면 주인이 비어(빌린 칸) 같은 조회가 검사를 빠져나갔다 — 설정 · 타입 표 · 기본 에셋 표가 그 모양이었다.
 */
SW_TEST_CASE( SubsystemScopeTest, UndeclaredMemberLookupDuringInitializeFailsStartup )
{
    bool                        bShutDown = false;
    SubsystemScope              scope;
    ScopeCatalogOwnerSubsystem& owner = scope.emplaceSubsystem<ScopeCatalogOwnerSubsystem>();
    SW_REQUIRE_TRUE( scope.exposeMember( owner, owner.getCatalog() ) );
    SW_EXPECT_FALSE( scope.exposeMember( owner, owner.getCatalog() ) );                         // 같은 타입을 두 번 걸지 않는다
    SW_EXPECT_TRUE( scope.findOwnerInScope( getSubsystemTypeHash<ScopeCatalog>() ) == &owner ); // 서비스 칸이 바인딩 때 묻는 주인
    (void)scope.emplaceSubsystem<ScopeLookupSubsystem<0, ScopeCatalog>>( scope, "ZLookup", vector<const utf8*>{}, &bShutDown );
    SW_EXPECT_FALSE( scope.getCollection().initializeAll( SubsystemHostMask::All ) );
    const string& error = scope.getCollection().getError();
    SW_EXPECT_TRUE_MSG( StringUtil::contains( error, "looked up 'ScopeCatalogOwner' without declaring it as a dependency" ), error.c_str() );

    bool                        bDeclaredShutDown = false;
    SubsystemScope              declared;
    ScopeCatalogOwnerSubsystem& declaredOwner = declared.emplaceSubsystem<ScopeCatalogOwnerSubsystem>();
    SW_REQUIRE_TRUE( declared.exposeMember( declaredOwner, declaredOwner.getCatalog() ) );
    (void)declared.emplaceSubsystem<ScopeLookupSubsystem<0, ScopeCatalog>>( declared, "ZLookup", vector<const utf8*>{ "ScopeCatalogOwner" }, &bDeclaredShutDown );
    SW_EXPECT_TRUE_MSG( declared.getCollection().initializeAll( SubsystemHostMask::All ), declared.getCollection().getError().c_str() );
    SW_EXPECT_EQUAL( 5, declared.get<const ScopeCatalog>()._value );
    declared.destroyAll();
    SW_EXPECT_TRUE( declared.find<ScopeCatalog>() == nullptr ); // 멤버 칸은 주인과 함께 지워진다(빌린 칸처럼 남아 매달리지 않는다)
}

/**
 * @brief [SubsystemScopeTest] 직접 · 간접으로 선언한 의존과 빌려 건 객체 · 상위 범위의 서브시스템은 초기화 중에 찾아도 된다
 */
SW_TEST_CASE( SubsystemScopeTest, DeclaredLookupDuringInitializeIsAllowed )
{
    bool           bDirectShutDown   = false;
    bool           bIndirectShutDown = false;
    SubsystemScope scope;
    (void)scope.emplaceSubsystem<ScopeProbeSubsystem>( nullptr );
    (void)scope.emplaceSubsystem<ScopeRelaySubsystem>();
    (void)scope.emplaceSubsystem<ScopeLookupSubsystem<0>>( scope, "Direct", vector<const utf8*>{ "ScopeProbe" }, &bDirectShutDown );
    (void)scope.emplaceSubsystem<ScopeLookupSubsystem<1>>( scope, "Indirect", vector<const utf8*>{ "ScopeRelay" }, &bIndirectShutDown );
    SW_EXPECT_TRUE_MSG( scope.getCollection().initializeAll( SubsystemHostMask::All ), scope.getCollection().getError().c_str() );
    SW_EXPECT_FALSE( bDirectShutDown );
    SW_EXPECT_FALSE( bIndirectShutDown );

    // 상위 범위에 선 서브시스템은 범위 수명이 지킨다 — 자식 목록의 의존 선언과 무관하다.
    bool           bChildShutDown = false;
    SubsystemScope child{ &scope };
    (void)child.emplaceSubsystem<ScopeLookupSubsystem<0>>( scope, "Child", vector<const utf8*>{}, &bChildShutDown );
    SW_EXPECT_TRUE_MSG( child.getCollection().initializeAll( SubsystemHostMask::All ), child.getCollection().getError().c_str() );
    child.destroyAll();
    SW_EXPECT_TRUE( bChildShutDown );
}

/**
 * @brief [SubsystemScopeTest] 소유한 서브시스템과 빌려 건 객체를 타입으로 찾고, 인터페이스로도 드러낸다
 */
SW_TEST_CASE( SubsystemScopeTest, FindsOwnedAndBorrowedByType )
{
    SubsystemScope       scope;
    ScopeProbeSubsystem& probe = scope.emplaceSubsystem<ScopeProbeSubsystem>( nullptr );
    SW_EXPECT_TRUE( scope.find<ScopeProbeSubsystem>() == &probe );
    SW_EXPECT_TRUE( &scope.get<ScopeProbeSubsystem>() == &probe );
    SW_EXPECT_TRUE( scope.find<IScopeProbeSubsystem>() == nullptr ); // 드러내기 전에는 인터페이스로 찾지 못한다
    SW_EXPECT_TRUE( scope.expose<IScopeProbeSubsystem>( probe ) );
    SW_EXPECT_EQUAL( 7, scope.find<IScopeProbeSubsystem>()->getValue() );
    SW_EXPECT_FALSE( scope.expose<IScopeProbeSubsystem>( probe ) ); // 같은 인터페이스를 두 번 걸지 않는다

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

/** @brief [SubsystemScopeTest] const 로 건 객체는 const 로 찾는 쪽만 찾고, 같은 타입은 const 든 아니든 두 칸이 되지 않는다. */
SW_TEST_CASE( SubsystemScopeTest, ConstBindingIsFoundOnlyAsConst )
{
    SubsystemScope     parent;
    SubsystemScope     scope{ &parent };
    const ScopeCatalog catalog{ 3 };
    SW_EXPECT_TRUE( scope.bind( &catalog ) );
    SW_EXPECT_TRUE( scope.find<const ScopeCatalog>() == &catalog );
    SW_EXPECT_TRUE( locateSubsystem<const ScopeCatalog>( &scope ) == &catalog );
    SW_EXPECT_TRUE( scope.find<ScopeCatalog>() == nullptr );
    SW_EXPECT_TRUE( locateSubsystem<ScopeCatalog>( &scope ) == nullptr );

    ScopeCatalog mutableCatalog{ 4 };
    SW_EXPECT_FALSE( scope.bind( &mutableCatalog ) );
    const ScopeCatalog otherConst{ 5 };
    SW_EXPECT_FALSE( scope.bind( &otherConst ) );

    // 상위의 일반 칸은 비-const 조회에 그대로 보이고, 가까운 const 칸이 const 조회에서 이긴다.
    SW_EXPECT_TRUE( parent.bind( &mutableCatalog ) );
    SW_EXPECT_TRUE( locateSubsystem<ScopeCatalog>( &scope ) == &mutableCatalog );
    SW_EXPECT_TRUE( locateSubsystem<const ScopeCatalog>( &scope ) == &catalog );

    scope.unbind<const ScopeCatalog>();
    SW_EXPECT_TRUE( scope.find<const ScopeCatalog>() == nullptr );
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
    ScopeProbeSubsystem& probe = scope.emplaceSubsystem<ScopeProbeSubsystem>( &bDestroyed );
    SW_EXPECT_TRUE( scope.expose<IScopeProbeSubsystem>( probe ) );
    ScopeCatalog catalog{};
    SW_EXPECT_TRUE( scope.bind( &catalog ) );

    scope.destroyAll();
    SW_EXPECT_TRUE( bDestroyed );
    SW_EXPECT_TRUE( scope.find<ScopeProbeSubsystem>() == nullptr );
    SW_EXPECT_TRUE( scope.find<IScopeProbeSubsystem>() == nullptr ); // 드러낸 칸도 함께 사라진다(지운 객체를 가리키지 않는다)
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
        ScopeProbeSubsystem& probe = child.emplaceSubsystem<ScopeProbeSubsystem>( &bDestroyed );
        SW_EXPECT_TRUE( &parent.mountChildScope( "ModuleA" ) == &child ); // 같은 이름은 같은 자식이다
        SW_EXPECT_TRUE( parent.find<ScopeProbeSubsystem>() == nullptr );  // 공개하기 전에는 부모가 보지 못한다
        SW_EXPECT_TRUE( child.publish<IScopeProbeSubsystem>( probe ) );
        SW_EXPECT_TRUE( child.publish<ScopeProbeSubsystem>( probe ) ); // 제 타입은 공개 표시만 더한다
        SW_EXPECT_EQUAL( 7, parent.find<IScopeProbeSubsystem>()->getValue() );
        SW_EXPECT_TRUE( parent.find<ScopeProbeSubsystem>() == &probe );
        SW_EXPECT_TRUE( child.getParentScope() == &parent ); // 자식은 위로 찾는다
    }
    ScopeCatalog catalog{ 5 };
    SW_EXPECT_TRUE( parent.bind( &catalog ) );
    SW_EXPECT_TRUE( locateSubsystem<ScopeCatalog>( parent.findChildScope( "ModuleA" ) ) == &catalog );

    parent.unmountChildScope( "ModuleA" );
    SW_EXPECT_TRUE( bDestroyed );
    SW_EXPECT_TRUE( parent.findChildScope( "ModuleA" ) == nullptr );
    SW_EXPECT_TRUE( parent.find<IScopeProbeSubsystem>() == nullptr );

    bool bSecondDestroyed = false;
    parent.mountChildScope( "ModuleB" ).emplaceSubsystem<ScopeProbeSubsystem>( &bSecondDestroyed );
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
    ScopePartOwnerSubsystem& owner = child.emplaceSubsystem<ScopePartOwnerSubsystem>();
    SW_EXPECT_TRUE( child.publishPart<IScopeProbe>( owner, owner.getPart() ) );
    SW_EXPECT_EQUAL( 9, parent.find<IScopeProbe>()->getValue() );

    child.destroyAll();
    SW_EXPECT_TRUE( parent.find<IScopeProbe>() == nullptr ); // 주인의 칸처럼 소유 칸이라 지운 멤버를 가리키지 않는다
}

namespace
{
    /** @brief 호스트 모듈(에디터)에 보인다고 스스로 선언한 서브시스템입니다(`kModuleVisibility`). */
    class HostModuleProbeSubsystem final : public Subsystem
    {
    public:
        static constexpr SubsystemModuleVisibility kModuleVisibility = SubsystemModuleVisibility::HostModules;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return "HostModuleProbe"; }
    };

    /** @brief 호스트 모듈에 보인다고 선언한, 서브시스템이 아닌 멤버 형식입니다. */
    struct DeclaredMember
    {
        static constexpr SubsystemModuleVisibility kModuleVisibility = SubsystemModuleVisibility::HostModules;
    };

    /** @brief 노출을 선언하지 않은 멤버 형식입니다. */
    struct PlainMember
    {
    };
} // namespace

/**
 * @brief [SubsystemScopeTest] 서브시스템이 선언한 호스트 모듈 노출만 호스트 모듈 조회에 보이고, 기본 서브시스템 · 빌려 건 객체 · 상위 범위는 보이지 않는다
 */
SW_TEST_CASE( SubsystemScopeTest, HostModuleVisibilityFollowsTheDeclaration )
{
    SubsystemScope                  parent;
    SubsystemScope                  scope{ &parent };
    const HostModuleProbeSubsystem& visible = scope.emplaceSubsystem<HostModuleProbeSubsystem>();
    (void)scope.emplaceSubsystem<ScopeProbeSubsystem>( nullptr );
    ScopeCatalog catalog{};
    SW_EXPECT_TRUE( scope.bind( &catalog ) );

    SW_EXPECT_TRUE( scope.findHostModuleVisibleInScope( getSubsystemTypeHash<HostModuleProbeSubsystem>() ) == &visible );
    SW_EXPECT_TRUE( scope.findHostModuleVisibleInScope( getSubsystemTypeHash<ScopeProbeSubsystem>() ) == nullptr );       // 선언하지 않은 서브시스템
    SW_EXPECT_TRUE( scope.findHostModuleVisibleInScope( getSubsystemTypeHash<ScopeCatalog>() ) == nullptr );              // 빌려 건 객체
    SW_EXPECT_TRUE( parent.findHostModuleVisibleInScope( getSubsystemTypeHash<HostModuleProbeSubsystem>() ) == nullptr ); // 상위는 하위를 보지 않는다

    scope.destroyAll();
    SW_EXPECT_TRUE( scope.findHostModuleVisibleInScope( getSubsystemTypeHash<HostModuleProbeSubsystem>() ) == nullptr ); // 지운 서브시스템을 가리키지 않는다
}

/** @brief [SubsystemScopeTest] 서브시스템이 드러낸 멤버는 멤버의 형식이 선언한 호스트 모듈 노출을 따른다 — 선언하지 않은 멤버는 보이지 않고 주인과 함께 사라진다 */
SW_TEST_CASE( SubsystemScopeTest, ExposedMemberFollowsItsOwnDeclaration )
{
    SubsystemScope       scope;
    ScopeProbeSubsystem& owner = scope.emplaceSubsystem<ScopeProbeSubsystem>( nullptr );
    DeclaredMember       declared{};
    PlainMember          plain{};
    SW_EXPECT_TRUE( scope.exposeMember<DeclaredMember>( owner, declared ) );
    SW_EXPECT_TRUE( scope.exposeMember<PlainMember>( owner, plain ) );

    SW_EXPECT_TRUE( scope.findHostModuleVisibleInScope( getSubsystemTypeHash<DeclaredMember>() ) == &declared );
    SW_EXPECT_TRUE( scope.findHostModuleVisibleInScope( getSubsystemTypeHash<PlainMember>() ) == nullptr );
    SW_EXPECT_TRUE( scope.find<PlainMember>() == &plain );

    scope.destroyAll();
    SW_EXPECT_TRUE( scope.findHostModuleVisibleInScope( getSubsystemTypeHash<DeclaredMember>() ) == nullptr );
}

/**
 * @brief [SubsystemScopeTest] 매단 자식 범위(모듈 서브시스템)가 선언한 호스트 모듈 노출도 부모의 호스트 모듈 조회에 보이고, 떼면 사라진다
 */
SW_TEST_CASE( SubsystemScopeTest, HostModuleVisibilityReachesMountedChildren )
{
    SubsystemScope                  parent;
    SubsystemScope&                 child   = parent.mountChildScope( "ModuleA" );
    const HostModuleProbeSubsystem& visible = child.emplaceSubsystem<HostModuleProbeSubsystem>();
    (void)child.emplaceSubsystem<ScopeProbeSubsystem>( nullptr );

    SW_EXPECT_TRUE( parent.findHostModuleVisibleInScope( getSubsystemTypeHash<HostModuleProbeSubsystem>() ) == &visible );
    SW_EXPECT_TRUE( parent.findHostModuleVisibleInScope( getSubsystemTypeHash<ScopeProbeSubsystem>() ) == nullptr ); // 선언하지 않은 서브시스템
    SW_EXPECT_TRUE( parent.findInScope( getSubsystemTypeHash<HostModuleProbeSubsystem>() ) == nullptr );             // 공개하지 않았으니 일반 조회에는 없다

    parent.unmountChildScope( "ModuleA" );
    SW_EXPECT_TRUE( parent.findHostModuleVisibleInScope( getSubsystemTypeHash<HostModuleProbeSubsystem>() ) == nullptr );
}

/**
 * @brief [SubsystemScopeTest] 초기화된 서브시스템만, 매단 자식의 것까지 모인다 — 게임 인스턴스 경계가 이 목록의 상태를 비운다. 결함이면 모듈 서브시스템의 상태가 게임을 다시 시작해도 남거나 서지 않은 것을 부른다
 */
SW_TEST_CASE( SubsystemScopeTest, CollectsInitializedSubsystemsOfMountedChildren )
{
    SubsystemScope          parent;
    ScopeProbeSubsystem&    probe    = parent.emplaceSubsystem<ScopeProbeSubsystem>( nullptr );
    SubsystemScope&         child    = parent.mountChildScope( "ModuleA" );
    ScopeInstanceSubsystem& instance = child.emplaceSubsystem<ScopeInstanceSubsystem>();

    vector<Subsystem*> listSubsystem;
    parent.collectInitializedSubsystems( listSubsystem );
    SW_EXPECT_TRUE( listSubsystem.empty() ); // 아직 아무것도 서지 않았다

    SW_REQUIRE_TRUE( parent.getCollection().initializeAll( SubsystemHostMask::All ) );
    SW_REQUIRE_TRUE( child.getCollection().initializeAll( SubsystemHostMask::All ) );
    parent.collectInitializedSubsystems( listSubsystem );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listSubsystem.size() );
    SW_EXPECT_TRUE( listSubsystem[0] == &probe );
    SW_EXPECT_TRUE( listSubsystem[1] == &instance );

    instance._queuedCount = 3;
    SW_EXPECT_TRUE( listSubsystem[1]->findGameInstanceState() != nullptr );
    for ( Subsystem* const pSubsystem : listSubsystem )
    {
        pSubsystem->resetGameInstanceState( GameInstanceBoundary::Ended );
    }
    SW_EXPECT_TRUE( listSubsystem[1]->findGameInstanceState() == nullptr );
}
