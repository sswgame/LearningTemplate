#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/ThreadTestAccess.h"

#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"

using namespace sw;

namespace
{
    /** @brief 이름 하나짜리 패스와 임시 텍스처를 선언하고, 선언이 몇 번 불렸는지 세는 확장입니다. */
    class FakeRenderExtension final : public IRenderExtension
    {
    public:
        uint32 _declareCount;

        explicit FakeRenderExtension( const utf8* pName )
            : _declareCount{ 0 }
            , _pName{ pName }
        {
        }
        void declarePasses( ExtensionPassBuilder& builder ) override
        {
            ++_declareCount;
            ExtensionTextureDesc texture;
            texture._name = _pName;
            ExtensionPassParams params;
            params._name      = _pName;
            params._listWrite = { builder.createTexture( texture ) };
            builder.addPass( std::move( params ), ExtensionPassExecuteFn{} );
        }

    private:
        const utf8* _pName;
    };

    /** @brief 소멸을 세고, 갱신 함수가 넣은 값을 기록하는 프록시입니다. */
    class FakeSceneProxy final : public IExtensionSceneProxy
    {
    public:
        vector<int32> _listAppliedValue;

        explicit FakeSceneProxy( int32* pDestroyCount = nullptr )
            : _listAppliedValue{}
            , _pDestroyCount{ pDestroyCount }
        {
        }
        ~FakeSceneProxy() override
        {
            if ( _pDestroyCount != nullptr )
                ++( *_pDestroyCount );
        }
        void collectDrawElements( ExtensionDrawCollector& /*out*/, const ExtensionFrameInfo& /*frame*/ ) const override {}

    private:
        int32* _pDestroyCount;
    };

    struct RenderExtensionRegistryTestUtil
    {
        /** @brief @p value 를 프록시에 적는 갱신 함수입니다(값 캡처 — 게임 스레드가 렌더 스레드로 넘기는 모양). */
        static ExtensionProxyUpdateFn makeUpdate( int32 value )
        {
            return SW_DELEGATE_LAMBDA( ExtensionProxyUpdateFn, [value]( IExtensionSceneProxy& proxy )
            { static_cast<FakeSceneProxy&>( proxy )._listAppliedValue.push_back( value ); } );
        }

        /** @brief 레지스트리가 내는 선언의 패스 이름들(등록 순서)입니다. */
        static vector<string> collectPassNames( const RenderExtensionRegistry& registry )
        {
            ExtensionPassBuilder builder;
            registry.copyDeclarations( builder );
            vector<string> listName;
            for ( const ExtensionPassBuilder::Entry& entry : builder.getEntries() )
            {
                listName.push_back( entry._params._name );
            }
            return listName;
        }

        static ExtensionDrawElement makeElement()
        {
            ExtensionDrawElement element;
            element._mesh = hashed_string{ "Cube" };
            return element;
        }
    };
    using Util = RenderExtensionRegistryTestUtil;
} // namespace

/** @brief [RenderExtensionRegistryTest] 확장은 올린 순서로 나오고, null · 겹침은 거절한다. 객체는 소유하지 않는다 */
SW_TEST_CASE( RenderExtensionRegistryTest, RegisterRejectsNullAndDuplicates )
{
    RenderExtensionRegistry registry;
    FakeRenderExtension     first( "First" );
    FakeRenderExtension     second( "Second" );
    SW_EXPECT_FALSE( registry.registerExtension( nullptr ) );
    SW_EXPECT_TRUE( registry.registerExtension( &first ) );
    SW_EXPECT_TRUE( registry.registerExtension( &second ) );
    SW_EXPECT_FALSE( registry.registerExtension( &first ) );

    const vector<string> listName = Util::collectPassNames( registry );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listName.size() ) );
    SW_EXPECT_EQUAL( string( "First" ), listName[0] );
    SW_EXPECT_EQUAL( string( "Second" ), listName[1] );
}

/**
 * @brief [RenderExtensionRegistryTest] 패스 선언은 등록할 때 한 번 받고, 프레임마다의 복사는 확장을 다시 부르지 않는다(임시 텍스처도 선언에 든다)
 * @details 결함이면 그래프의 모듈 노드가 프레임마다 바뀔 수 있어 컴파일 · 계획 캐시가 매 프레임 버려진다. 변이: 복사가 `declarePasses` 를 부르면 횟수 단언이 진다.
 */
SW_TEST_CASE( RenderExtensionRegistryTest, DeclarationIsTakenOnceAtRegistration )
{
    RenderExtensionRegistry registry;
    FakeRenderExtension     extension( "Pass" );
    SW_REQUIRE_TRUE( registry.registerExtension( &extension ) );
    SW_EXPECT_EQUAL( 1u, extension._declareCount );
    ExtensionPassBuilder builder;
    registry.copyDeclarations( builder );
    registry.copyDeclarations( builder );
    SW_EXPECT_EQUAL( 1u, extension._declareCount );
    SW_REQUIRE_EQUAL( size_t{ 1 }, builder.getEntries().size() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, builder.getTextures().size() );
    SW_EXPECT_EQUAL( string( "Extension.Pass" ), builder.getTextures()[0]._name );
    SW_EXPECT_EQUAL( string( "Extension.Pass" ), builder.getEntries()[0]._params._listWrite[0] );
}

/** @brief [RenderExtensionRegistryTest] 풀면 빠지고 다시 올릴 수 있다. 없는 객체를 풀어도 아무 일 없다 */
SW_TEST_CASE( RenderExtensionRegistryTest, UnregisterRemovesOnlyThatExtension )
{
    RenderExtensionRegistry registry;
    FakeRenderExtension     first( "First" );
    FakeRenderExtension     second( "Second" );
    FakeRenderExtension     stranger( "Stranger" );
    SW_REQUIRE_TRUE( registry.registerExtension( &first ) );
    SW_REQUIRE_TRUE( registry.registerExtension( &second ) );

    registry.unregisterExtension( &stranger );
    registry.unregisterExtension( nullptr );
    registry.unregisterExtension( &first );

    const vector<string> listName = Util::collectPassNames( registry );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listName.size() ) );
    SW_EXPECT_EQUAL( string( "Second" ), listName[0] );
    SW_EXPECT_TRUE( registry.registerExtension( &first ) );
}

/** @brief [RenderExtensionRegistryTest] 복사는 앞 내용을 덮어쓴다 — 지난 프레임의 포인터가 섞이지 않는다 */
SW_TEST_CASE( RenderExtensionRegistryTest, CopiesReplaceTheOutput )
{
    RenderExtensionRegistry registry;
    SW_EXPECT_TRUE( registry.addProxy( sw::make_unique<FakeSceneProxy>() ).isValid() );
    vector<IExtensionSceneProxy*> listProxy;
    registry.copyProxies( listProxy );
    registry.copyProxies( listProxy );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listProxy.size() ) );
}

/** @brief [RenderExtensionRegistryTest] 프록시 번호는 0 이 실패이고, 올릴 때마다 커지며, 지운 번호를 다시 쓰지 않는다 */
SW_TEST_CASE( RenderExtensionRegistryTest, ProxyIdsAreNeverReused )
{
    RenderExtensionRegistry registry;
    SW_EXPECT_FALSE( registry.addProxy( unique_ptr<IExtensionSceneProxy>{} ).isValid() );

    const ExtensionSceneProxyID first  = registry.addProxy( sw::make_unique<FakeSceneProxy>() );
    const ExtensionSceneProxyID second = registry.addProxy( sw::make_unique<FakeSceneProxy>() );
    SW_EXPECT_TRUE( first.isValid() );
    SW_EXPECT_TRUE( first < second );
    registry.removeProxy( second );
    const ExtensionSceneProxyID third = registry.addProxy( sw::make_unique<FakeSceneProxy>() );
    SW_EXPECT_TRUE( second < third );

    vector<IExtensionSceneProxy*> listProxy;
    registry.copyProxies( listProxy );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listProxy.size() ) );
}

/** @brief [RenderExtensionRegistryTest] 프록시는 레지스트리가 소유한다 — 지울 때 소멸하고, 없는 번호는 아무 일 없고, 레지스트리와 함께 남은 것도 소멸한다 */
SW_TEST_CASE( RenderExtensionRegistryTest, RegistryOwnsTheProxies )
{
    int32 destroyCount{ 0 };
    {
        RenderExtensionRegistry     registry;
        const ExtensionSceneProxyID first = registry.addProxy( sw::make_unique<FakeSceneProxy>( &destroyCount ) );
        (void)registry.addProxy( sw::make_unique<FakeSceneProxy>( &destroyCount ) );
        registry.removeProxy( ExtensionSceneProxyID{ first.getValue() + 100u } );
        SW_EXPECT_EQUAL( 0, destroyCount );
        registry.removeProxy( first );
        SW_EXPECT_EQUAL( 1, destroyCount );
        registry.removeProxy( first ); // 두 번 지워도 같다
        SW_EXPECT_EQUAL( 1, destroyCount );
    }
    SW_EXPECT_EQUAL( 2, destroyCount );
}

/** @brief [RenderExtensionRegistryTest] 갱신은 적용 전까지 프록시에 닿지 않고, 올린 순서로 그 번호의 프록시에 한 번만 적용된다 */
SW_TEST_CASE( RenderExtensionRegistryTest, UpdatesApplyInOrderOnce )
{
    RenderExtensionRegistry       registry;
    const ExtensionSceneProxyID   firstID  = registry.addProxy( sw::make_unique<FakeSceneProxy>() );
    const ExtensionSceneProxyID   secondID = registry.addProxy( sw::make_unique<FakeSceneProxy>() );
    vector<IExtensionSceneProxy*> listProxy;
    registry.copyProxies( listProxy );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listProxy.size() ) );
    const FakeSceneProxy& first  = static_cast<const FakeSceneProxy&>( *listProxy[0] );
    const FakeSceneProxy& second = static_cast<const FakeSceneProxy&>( *listProxy[1] );

    SW_EXPECT_TRUE( registry.enqueueProxyUpdate( firstID, Util::makeUpdate( 1 ) ) );
    SW_EXPECT_TRUE( registry.enqueueProxyUpdate( secondID, Util::makeUpdate( 2 ) ) );
    SW_EXPECT_TRUE( registry.enqueueProxyUpdate( firstID, Util::makeUpdate( 3 ) ) );
    SW_EXPECT_TRUE( first._listAppliedValue.empty() );

    registry.applyPendingProxyUpdates();
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( first._listAppliedValue.size() ) );
    SW_EXPECT_EQUAL( 1, first._listAppliedValue[0] );
    SW_EXPECT_EQUAL( 3, first._listAppliedValue[1] );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( second._listAppliedValue.size() ) );
    SW_EXPECT_EQUAL( 2, second._listAppliedValue[0] );

    registry.applyPendingProxyUpdates();
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( first._listAppliedValue.size() ) );
}

/** @brief [RenderExtensionRegistryTest] 없는 번호 · 빈 함수의 갱신은 거절한다 */
SW_TEST_CASE( RenderExtensionRegistryTest, BadUpdatesAreRejected )
{
    RenderExtensionRegistry     registry;
    const ExtensionSceneProxyID id = registry.addProxy( sw::make_unique<FakeSceneProxy>() );
    SW_EXPECT_FALSE( registry.enqueueProxyUpdate( ExtensionSceneProxyID{ id.getValue() + 1u }, Util::makeUpdate( 1 ) ) );
    SW_EXPECT_FALSE( registry.enqueueProxyUpdate( ExtensionSceneProxyID{}, Util::makeUpdate( 1 ) ) );
    SW_EXPECT_FALSE( registry.enqueueProxyUpdate( id, ExtensionProxyUpdateFn{} ) );
    registry.removeProxy( id );
    SW_EXPECT_FALSE( registry.enqueueProxyUpdate( id, Util::makeUpdate( 1 ) ) );
}

/** @brief [RenderExtensionRegistryTest] 프록시를 지우면 대기 중인 그 갱신도 버린다 — 다른 프록시의 갱신은 남는다 */
SW_TEST_CASE( RenderExtensionRegistryTest, RemovingAProxyDropsItsUpdates )
{
    RenderExtensionRegistry     registry;
    const ExtensionSceneProxyID goneID = registry.addProxy( sw::make_unique<FakeSceneProxy>() );
    const ExtensionSceneProxyID keptID = registry.addProxy( sw::make_unique<FakeSceneProxy>() );
    SW_EXPECT_TRUE( registry.enqueueProxyUpdate( goneID, Util::makeUpdate( 1 ) ) );
    SW_EXPECT_TRUE( registry.enqueueProxyUpdate( keptID, Util::makeUpdate( 2 ) ) );
    SW_EXPECT_TRUE( registry.enqueueProxyUpdate( goneID, Util::makeUpdate( 3 ) ) );
    registry.removeProxy( goneID );
    registry.applyPendingProxyUpdates();

    vector<IExtensionSceneProxy*> listProxy;
    registry.copyProxies( listProxy );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listProxy.size() ) );
    const FakeSceneProxy& kept = static_cast<const FakeSceneProxy&>( *listProxy[0] );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( kept._listAppliedValue.size() ) );
    SW_EXPECT_EQUAL( 2, kept._listAppliedValue[0] );
}

/** @brief [RenderExtensionRegistryTest] 등록을 풀면 렌더 스레드의 모듈 구간이 끝날 때까지 기다린다 — 그 전에는 목록에 남아, 도는 중인 모듈 코드가 내려가지 않는다 */
SW_TEST_CASE( RenderExtensionRegistryTest, UnregisterFlushesTheRenderScope )
{
    RenderExtensionRegistry registry;
    FakeRenderExtension     extension( "Pass" );
    SW_REQUIRE_TRUE( registry.registerExtension( &extension ) );

    atomic<bool> bStarted{ false };
    atomic<bool> bReturned{ false };
    thread       other;
    {
        const RenderExtensionRegistry::RenderScope scope{ registry };
        other = thread{ ThreadTestAccess::getKey(), [&]
        {
            bStarted.store( true );
            registry.unregisterExtension( &extension );
            bReturned.store( true );
        } };
        while ( bStarted.load() == false )
        {
            ThreadUtil::yield();
        }
        // 구간 안: 어느 시점에 보든 풀리지 않았다. 등록 올리기와 목록 읽기는 막히지 않는다(`_mutex` 만 쓴다).
        for ( uint32 spinIndex = 0; spinIndex < 1000; ++spinIndex )
        {
            ThreadUtil::yield();
        }
        SW_EXPECT_FALSE( bReturned.load() );
        SW_EXPECT_EQUAL( 1u, static_cast<uint32>( Util::collectPassNames( registry ).size() ) );
    }
    other.join();
    SW_EXPECT_TRUE( bReturned.load() );
    SW_EXPECT_TRUE( Util::collectPassNames( registry ).empty() );
}

/** @brief [RenderExtensionRegistryTest] 실패 알림은 키마다 처음 한 번만 참이다 */
SW_TEST_CASE( RenderExtensionRegistryTest, NoteFailureOncePerKey )
{
    RenderExtensionRegistry registry;
    SW_EXPECT_TRUE( registry.recordFailureOnce( "PassA" ) );
    SW_EXPECT_FALSE( registry.recordFailureOnce( "PassA" ) );
    SW_EXPECT_TRUE( registry.recordFailureOnce( "PassB" ) );

    RenderExtensionRegistry other; // 레지스트리마다 따로다
    SW_EXPECT_TRUE( other.recordFailureOnce( "PassA" ) );
}

/** @brief [RenderExtensionRegistryTest] 확장 등록은 수명이다 — 만들 때 올리고 소멸할 때 푼다 */
SW_TEST_CASE( RenderExtensionRegistryTest, ExtensionRegistrationFollowsItsLifetime )
{
    RenderExtensionRegistry registry;
    FakeRenderExtension     extension( "Pass" );
    {
        const RenderExtensionRegistration registration{ registry, extension };
        SW_EXPECT_TRUE( registration.isRegistered() );
        SW_EXPECT_EQUAL( 1u, static_cast<uint32>( Util::collectPassNames( registry ).size() ) );
    }
    SW_EXPECT_TRUE( Util::collectPassNames( registry ).empty() );
}

/** @brief [RenderExtensionRegistryTest] 같은 확장의 두 번째 등록은 오류이고, 그것이 소멸해도 첫 등록을 풀지 않는다 */
SW_TEST_CASE( RenderExtensionRegistryTest, DuplicateRegistrationDoesNotUnregisterTheFirst )
{
    SW_EXPECT_LOG( "ExtensionRenderPass.ModuleRenderExtensionAlready" );
    RenderExtensionRegistry           registry;
    FakeRenderExtension               extension( "Pass" );
    const RenderExtensionRegistration first{ registry, extension };
    {
        const RenderExtensionRegistration second{ registry, extension };
        SW_EXPECT_FALSE( second.isRegistered() );
    }
    SW_EXPECT_TRUE( first.isRegistered() );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( Util::collectPassNames( registry ).size() ) );
}

/** @brief [RenderExtensionRegistryTest] 프록시 등록은 수명이다 — 소멸하면 프록시가 소멸하고, 대기 중인 갱신도 사라진다 */
SW_TEST_CASE( RenderExtensionRegistryTest, ProxyRegistrationFollowsItsLifetime )
{
    int32                   destroyCount{ 0 };
    RenderExtensionRegistry registry;
    {
        ExtensionSceneProxyRegistration registration{ registry, sw::make_unique<FakeSceneProxy>( &destroyCount ) };
        SW_EXPECT_TRUE( registration.isRegistered() );
        SW_EXPECT_TRUE( registration.update( Util::makeUpdate( 1 ) ) );
        SW_EXPECT_EQUAL( 0, destroyCount );
    }
    SW_EXPECT_EQUAL( 1, destroyCount );
    registry.applyPendingProxyUpdates(); // 사라진 프록시의 갱신이 남아 있으면 여기서 해제된 객체를 부른다
    vector<IExtensionSceneProxy*> listProxy;
    registry.copyProxies( listProxy );
    SW_EXPECT_TRUE( listProxy.empty() );
}

/** @brief [RenderExtensionRegistryTest] 프록시 없이 만든 등록은 실패 상태이고 갱신을 거절하며, 소멸해도 남의 프록시를 건드리지 않는다 */
SW_TEST_CASE( RenderExtensionRegistryTest, ProxyRegistrationWithoutProxyIsInert )
{
    int32                       destroyCount{ 0 };
    RenderExtensionRegistry     registry;
    const ExtensionSceneProxyID otherID = registry.addProxy( sw::make_unique<FakeSceneProxy>( &destroyCount ) );
    SW_EXPECT_TRUE( otherID.isValid() );
    {
        ExtensionSceneProxyRegistration registration{ registry, unique_ptr<IExtensionSceneProxy>{} };
        SW_EXPECT_FALSE( registration.isRegistered() );
        SW_EXPECT_FALSE( registration.update( Util::makeUpdate( 1 ) ) );
    }
    SW_EXPECT_EQUAL( 0, destroyCount );
    vector<IExtensionSceneProxy*> listProxy;
    registry.copyProxies( listProxy );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listProxy.size() ) );
}

/** @brief [ExtensionDrawCollectorTest] 메시 없는 요소는 거절한다 */
SW_TEST_CASE( ExtensionDrawCollectorTest, ElementNeedsAMesh )
{
    SW_EXPECT_LOG( "ExtensionRenderPass.ExtensionDrawElementNo" );
    ExtensionDrawCollector collector;
    SW_EXPECT_FALSE( collector.add( ExtensionDrawElement{} ) );
    SW_EXPECT_TRUE( collector.getElements().empty() );
}

/** @brief [ExtensionDrawCollectorTest] 인스턴스 수는 1 ~ 상한이다 — 양 끝은 받고 밖은 거절한다 */
SW_TEST_CASE( ExtensionDrawCollectorTest, InstanceCountBoundaries )
{
    SW_EXPECT_LOG( "ExtensionRenderPass.ExtensionDrawElementInstance" );
    ExtensionDrawCollector collector;
    ExtensionDrawElement   element = Util::makeElement();
    element._instanceCount         = 0;
    SW_EXPECT_FALSE( collector.add( element ) );
    element._instanceCount = ExtensionDrawElement::kMaxInstanceCount + 1u;
    SW_EXPECT_FALSE( collector.add( element ) );
    SW_EXPECT_TRUE( collector.getElements().empty() );

    element._instanceCount = 1;
    SW_EXPECT_TRUE( collector.add( element ) );
    element._instanceCount = ExtensionDrawElement::kMaxInstanceCount;
    SW_EXPECT_TRUE( collector.add( element ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( collector.getElements().size() ) );
}

/** @brief [ExtensionDrawCollectorTest] 상수 수는 상한까지 받고 넘으면 거절한다(넘은 수로 배열 밖을 읽지 않는다) */
SW_TEST_CASE( ExtensionDrawCollectorTest, ConstantCountBoundary )
{
    SW_EXPECT_LOG( "ExtensionRenderPass.ExtensionDrawElementConstants" );
    ExtensionDrawCollector collector;
    ExtensionDrawElement   element = Util::makeElement();
    element._constantCount         = ExtensionDrawElement::kMaxConstantCount + 1u;
    SW_EXPECT_FALSE( collector.add( element ) );
    element._constantCount = ExtensionDrawElement::kMaxConstantCount;
    SW_EXPECT_TRUE( collector.add( element ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( collector.getElements().size() ) );
    SW_EXPECT_EQUAL( ExtensionDrawElement::kMaxConstantCount, collector.getElements()[0]._constantCount );
}
