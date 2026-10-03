#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "TestFramework/TestFramework.h"

#include <thread>

namespace
{
    /**
     * @brief 고정 ID 이벤트(`SW_REGISTER_ENGINE_EVENT`) 시험용입니다 — 엔진 예약 ID 를 쓰는 값 타입의 모양(크기 페이로드)만 흉내 냅니다.
     * @details 시험은 디스패처(Core)를 보므로 Engine 의 창 이벤트 타입을 쓰지 않습니다. 그 타입 자체는 EngineTest 의 `WindowEventTest` 가 봅니다.
     */
    struct ResizeProbeEvent final : sw::IEvent
    {
        int32 _width{ 0 };
        int32 _height{ 0 };

        SW_REGISTER_ENGINE_EVENT( WindowResize );
    };

    /** @brief 페이로드 없는 고정 ID 이벤트입니다. */
    struct CloseProbeEvent final : sw::IEvent
    {
        SW_REGISTER_ENGINE_EVENT( WindowClose );
    };

    /** @brief 불리언 하나를 싣는 고정 ID 이벤트입니다. */
    struct ActivateProbeEvent final : sw::IEvent
    {
        bool _bActive{ false };

        SW_REGISTER_ENGINE_EVENT( WindowActivate );
    };

    int32 s_LastResizeWidth{ 0 };
    int32 s_LastResizeHeight{ 0 };

    /** @brief 리사이즈 이벤트에서 너비·높이를 기록합니다. */
    void onResizeProbe( const ResizeProbeEvent& e )
    {
        s_LastResizeWidth  = e._width;
        s_LastResizeHeight = e._height;
    }

    bool s_bCloseReceived{ false };
    bool s_bActivateReceived{ false };

    /** @brief 닫기 이벤트를 받았음을 기록합니다. */
    void onCloseProbe( const CloseProbeEvent& )
    {
        s_bCloseReceived = true;
    }

    /** @brief 활성 이벤트의 값을 기록합니다. */
    void onActivateProbe( const ActivateProbeEvent& e )
    {
        s_bActivateReceived = e._bActive;
    }

    /** @brief 소멸 횟수를 세는 시험용 이벤트 — 큐에 남은 것이 실제로 파괴되는지 본다. */
    struct DestructorCountingEvent final : sw::IEvent
    {
        static int32 s_liveCount;

        sw::string _payload; ///< 실제 게임플레이 이벤트처럼 힙을 드는 멤버

        DestructorCountingEvent() { ++s_liveCount; }
        DestructorCountingEvent( const DestructorCountingEvent& other )
            : sw::IEvent( other )
            , _payload{ other._payload }
        {
            ++s_liveCount;
        }
        ~DestructorCountingEvent() override { --s_liveCount; }

        SW_DECLARE_GAMEPLAY_EVENT( DestructorCountingEvent );
    };

    int32 DestructorCountingEvent::s_liveCount = 0;

    int32 s_releaseProbeCount{ 0 };

    /** @brief 이미지 범위 풀기 테스트가 받은 리사이즈 이벤트 수를 셉니다. */
    void onReleaseProbeResize( const ResizeProbeEvent& )
    {
        ++s_releaseProbeCount;
    }

    /** @brief 이미지 범위 풀기에서 남는 쪽 이벤트입니다(`DestructorCountingEvent` 와 다른 vtable). */
    struct KeptProbeEvent final : sw::IEvent
    {
        int32 _value{ 0 };

        SW_DECLARE_GAMEPLAY_EVENT( KeptProbeEvent );
    };

    int32 s_keptProbeSum{ 0 };

    /** @brief 받은 `KeptProbeEvent` 의 값을 더합니다. */
    void onKeptProbe( const KeptProbeEvent& event )
    {
        s_keptProbeSum += event._value;
    }
} // namespace

// ------------------------------------------------------------------------------
// 1) Engine_Event — 디스패치·채널 필터
// ------------------------------------------------------------------------------
/**
 * @brief [EventTest] 디스패처 push 와 dispatch
 */
SW_TEST_CASE( EventTest, DispatcherPushAndDispatch )
{
    s_LastResizeWidth  = 0;
    s_LastResizeHeight = 0;

    sw::EventDispatcher                           dispatcher;
    sw::Delegate<void( const ResizeProbeEvent& )> del = SW_DELEGATE_FUNCTION( sw::Delegate<void( const ResizeProbeEvent& )>, onResizeProbe );
    dispatcher.subscribe<ResizeProbeEvent>( del );

    ResizeProbeEvent event;
    event._width  = 1920;
    event._height = 1080;
    dispatcher.push( event );

    SW_EXPECT_EQUAL( 0, s_LastResizeWidth );

    dispatcher.publish( event );

    SW_EXPECT_EQUAL( 1920, s_LastResizeWidth );
    SW_EXPECT_EQUAL( 1080, s_LastResizeHeight );

    dispatcher.unsubscribe<ResizeProbeEvent>( del );
    dispatcher.clear();
}

/**
 * @brief [EventTest] 닫기·활성화 이벤트
 */
SW_TEST_CASE( EventTest, DispatcherCloseAndActivateEvents )
{
    s_bCloseReceived    = false;
    s_bActivateReceived = false;

    sw::EventDispatcher                             dispatcher;
    sw::Delegate<void( const CloseProbeEvent& )>    closeDel    = SW_DELEGATE_FUNCTION( sw::Delegate<void( const CloseProbeEvent& )>, onCloseProbe );
    sw::Delegate<void( const ActivateProbeEvent& )> activateDel = SW_DELEGATE_FUNCTION( sw::Delegate<void( const ActivateProbeEvent& )>, onActivateProbe );

    dispatcher.subscribe<CloseProbeEvent>( closeDel );
    dispatcher.subscribe<ActivateProbeEvent>( activateDel );

    CloseProbeEvent    closeEvt;
    ActivateProbeEvent activateEvt;
    activateEvt._bActive = true;

    dispatcher.publish( closeEvt );
    dispatcher.publish( activateEvt );

    SW_EXPECT_TRUE( s_bCloseReceived );
    SW_EXPECT_TRUE( s_bActivateReceived );

    SW_EXPECT_EQUAL( sw::kEventWindowClose, closeEvt.getEventType() );
    SW_EXPECT_EQUAL( sw::kEventWindowActivate, activateEvt.getEventType() );

    dispatcher.clear();
}

/**
 * @brief [EventTest] 지연 이벤트 큐
 */
SW_TEST_CASE( EventTest, DeferredEventQueueTest )
{
    s_LastResizeWidth  = 0;
    s_LastResizeHeight = 0;

    sw::EventDispatcher                           dispatcher;
    sw::Delegate<void( const ResizeProbeEvent& )> resizeDel = SW_DELEGATE_FUNCTION( sw::Delegate<void( const ResizeProbeEvent& )>, onResizeProbe );
    dispatcher.subscribe<ResizeProbeEvent>( resizeDel );

    ResizeProbeEvent event;
    event._width  = 2560;
    event._height = 1440;

    dispatcher.publish( event );
    SW_EXPECT_EQUAL( 2560, s_LastResizeWidth );
    SW_EXPECT_EQUAL( 1440, s_LastResizeHeight );

    dispatcher.unsubscribe<ResizeProbeEvent>( resizeDel );
    dispatcher.clear();
}

/**
 * @brief [EventTest] 채널 필터링
 */
SW_TEST_CASE( EventTest, EventDispatcherChannelFiltering )
{
    sw::EventDispatcher dispatcher;

    static int32 s_uiChannelReceived{ 0 };
    static int32 s_audioChannelReceived{ 0 };

    sw::hashed_string uiChannel( "UI_Channel" );
    sw::hashed_string audioChannel( "Audio_Channel" );

    sw::Delegate<void( const ResizeProbeEvent& )> uiDel = SW_DELEGATE_LAMBDA( sw::Delegate<void( const ResizeProbeEvent& )>, []( const ResizeProbeEvent& e )
    { s_uiChannelReceived = e._width; } );

    sw::Delegate<void( const ResizeProbeEvent& )> audioDel = SW_DELEGATE_LAMBDA( sw::Delegate<void( const ResizeProbeEvent& )>, []( const ResizeProbeEvent& e )
    { s_audioChannelReceived = e._height; } );

    dispatcher.subscribe<ResizeProbeEvent>( uiChannel, uiDel );
    dispatcher.subscribe<ResizeProbeEvent>( audioChannel, audioDel );

    ResizeProbeEvent event;
    event._width  = 1280;
    event._height = 720;

    dispatcher.publish<ResizeProbeEvent>( uiChannel, event );
    SW_EXPECT_EQUAL( 1280, s_uiChannelReceived );
    SW_EXPECT_EQUAL( 0, s_audioChannelReceived );

    dispatcher.publish<ResizeProbeEvent>( audioChannel, event );
    SW_EXPECT_EQUAL( 720, s_audioChannelReceived );

    dispatcher.clear();
}

/**
 * @brief [EventTest] 64KB 프레임 할당자를 초과하는 대량 이벤트 큐잉 시 오버플로우 메모리 처리 및 0-유실 검증
 */
SW_TEST_CASE( EventTest, FrameAllocatorOverflowFallback )
{
    sw::EventDispatcher dispatcher;

    int32 receivedCount{ 0 };
    int32 lastReceivedIndex{ -1 };

    sw::Delegate<void( const ResizeProbeEvent& )> del = SW_DELEGATE_LAMBDA( sw::Delegate<void( const ResizeProbeEvent& )>, [&]( const ResizeProbeEvent& e )
    {
        ++receivedCount;
        SW_EXPECT_EQUAL( lastReceivedIndex + 1, e._width );
        lastReceivedIndex = e._width;
    } );

    dispatcher.subscribe<ResizeProbeEvent>( del );

    // Push 3000 events (> 90 KB, exceeding default 64KB linear arena)
    constexpr int32 kTotalEvents = 3000;
    for ( int32 index = 0; index < kTotalEvents; ++index )
    {
        ResizeProbeEvent evt;
        evt._width  = index;
        evt._height = index * 2;
        dispatcher.push( evt );
    }

    SW_EXPECT_EQUAL( 0, receivedCount );

    dispatcher.processEvents();

    SW_EXPECT_EQUAL( kTotalEvents, receivedCount );
    SW_EXPECT_EQUAL( kTotalEvents - 1, lastReceivedIndex );

    dispatcher.clear();
}

/**
 * @brief [EventTest] `clear()` 는 큐에 남은 이벤트를 **파괴하고** 버린다
 * @details `processEvents` 는 방송한 뒤 `pEvent->~IEvent()` 를 부르는데, `clear()` 는 큐 맵만
 *          비우고 아레나를 되감았다. 이벤트는 아레나에 placement new 로 올라가므로, 소멸자를
 *          부르지 않으면 **멤버가 든 힙이 그대로 샌다**(게임플레이 이벤트는 `sw::string` 을 든다).
 *          소멸자가 도는지를 살아 있는 개수로 본다.
 */
SW_TEST_CASE( EventTest, ClearDestroysQueuedEvents )
{
    sw::EventDispatcher dispatcher;

    DestructorCountingEvent::s_liveCount = 0;
    {
        DestructorCountingEvent queued;
        queued._payload = "게임 세이브 경로처럼 힙을 드는 문자열";
        dispatcher.push( queued ); // 큐에 복사본이 하나 더 생긴다
        SW_EXPECT_EQUAL( 2, DestructorCountingEvent::s_liveCount );
    }
    // 지역 변수는 죽고 큐에 든 복사본만 남는다.
    SW_EXPECT_EQUAL( 1, DestructorCountingEvent::s_liveCount );
    SW_EXPECT_EQUAL( size_t{ 1 }, dispatcher.getPendingEventCount() );

    dispatcher.clear();

    SW_EXPECT_TRUE_MSG( DestructorCountingEvent::s_liveCount == 0,
                        "clear() 가 큐에 남은 이벤트의 소멸자를 부르지 않았다 — 멤버가 든 힙이 샌다" );
    SW_EXPECT_EQUAL( size_t{ 0 }, dispatcher.getPendingEventCount() );
}

/**
 * @brief [EventTest] 디스패처가 죽을 때도 큐에 남은 이벤트를 파괴한다
 * @details `clear()` 와 같은 구멍이 소멸자에도 있었다. 종료 시점에 큐가 비어 있지 않으면 그대로 샌다.
 */
SW_TEST_CASE( EventTest, DestructorDestroysQueuedEvents )
{
    DestructorCountingEvent::s_liveCount = 0;
    {
        sw::EventDispatcher dispatcher;
        {
            DestructorCountingEvent queued;
            queued._payload = "종료 직전에 밀어 넣은 이벤트";
            dispatcher.push( queued );
        }
        SW_EXPECT_EQUAL( 1, DestructorCountingEvent::s_liveCount );
    }

    SW_EXPECT_TRUE_MSG( DestructorCountingEvent::s_liveCount == 0,
                        "디스패처가 죽을 때 큐에 남은 이벤트의 소멸자를 부르지 않았다" );
}

/**
 * @brief [EventTest] 다른 스레드에서 `push` 한 이벤트가 퍼내는 스레드에 도착한다
 * @details 이것이 이 클래스가 실제로 쓰이는 방식이자, **교차 스레드로 지원되는 유일한 입구**다.
 *          워커가 큐에 밀어 넣고(`_queueSpinLock` + 프레임 아레나가 지킨다), 버스를 퍼내는
 *          스레드가 프레임마다 `processEvents` 로 빼서 방송한다(`EngineLoop::tick`).
 *          버스 쪽(subscribe/publish)은 퍼내는 스레드 전용이며 Debug 에서 확인한다.
 */
SW_TEST_CASE( EventTest, PushFromWorkerThreadsReachesPumpingThread )
{
    sw::EventDispatcher dispatcher;

    int32 receivedCount = 0;
    int32 widthSum      = 0;
    dispatcher.subscribe<ResizeProbeEvent>( SW_DELEGATE_LAMBDA(
        sw::Delegate<void( const ResizeProbeEvent& )>, [&]( const ResizeProbeEvent& e )
    {
        ++receivedCount;
        widthSum += e._width;
    } ) );

    constexpr int32 kThreadCount     = 4;
    constexpr int32 kEventsPerThread = 25;

    sw::vector<std::thread> listWorker;
    for ( int32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listWorker.emplace_back( [&dispatcher]()
        {
            for ( int32 eventIndex = 0; eventIndex < kEventsPerThread; ++eventIndex )
            {
                ResizeProbeEvent resizeEvent;
                resizeEvent._width  = 1;
                resizeEvent._height = 1;
                dispatcher.push( resizeEvent );
            }
        } );
    }
    for ( std::thread& worker : listWorker )
    {
        worker.join();
    }

    SW_EXPECT_EQUAL( static_cast<size_t>( kThreadCount * kEventsPerThread ), dispatcher.getPendingEventCount() );

    dispatcher.processEvents();

    SW_EXPECT_EQUAL( kThreadCount * kEventsPerThread, receivedCount );
    SW_EXPECT_EQUAL( kThreadCount * kEventsPerThread, widthSum );
    SW_EXPECT_EQUAL( size_t{ 0 }, dispatcher.getPendingEventCount() );

    dispatcher.clear();
}

/**
 * @brief [EventTest] releaseCodeWithin 은 그 이미지가 만든 구독과 채널 항목을 치우고, 다시 구독하면 새 항목으로 돈다
 * @details 핫 리로드가 모듈 이미지를 내리기 전에 부르는 길이다. 여기서는 **테스트 실행 파일 자신의 이미지**를 범위로 준다 — 구독의
 *          스텁도, 이 이벤트 타입의 채널 항목 함수도 이 번역 단위에서 인스턴스화되었으니 모두 그 범위 안이다. 항목이 남으면 이미지가
 *          내려간 뒤의 발행이 내려간 코드로 뛴다.
 */
SW_TEST_CASE( EventTest, ReleaseCodeWithinDropsTheSubscriptionsAndEntriesTheImageCreated )
{
    using ResizeDelegate = sw::Delegate<void( const ResizeProbeEvent& )>;

    sw::EventDispatcher dispatcher;
    s_releaseProbeCount = 0;
    dispatcher.subscribe<ResizeProbeEvent>( SW_DELEGATE_FUNCTION( ResizeDelegate, onReleaseProbeResize ) );

    ResizeProbeEvent event;
    event._width  = 10;
    event._height = 20;
    dispatcher.publish( event );
    SW_EXPECT_EQUAL( 1, s_releaseProbeCount );

    const void* pBegin{ nullptr };
    const void* pEnd{ nullptr };
    SW_ASSERT_TRUE( sw::FileUtil::findLoadedImageRange( reinterpret_cast<const void*>( &onReleaseProbeResize ), pBegin, pEnd ) );

    uint32 remainingEntryCount{ 99 };
    SW_EXPECT_EQUAL( 1u, dispatcher.releaseCodeWithin( pBegin, pEnd, remainingEntryCount ) );
    SW_EXPECT_EQUAL( 0u, remainingEntryCount );

    dispatcher.publish( event );
    SW_EXPECT_EQUAL( 1, s_releaseProbeCount );

    // 항목이 지워졌어도 다시 구독하면 이쪽에서 새 항목을 만든다.
    dispatcher.subscribe<ResizeProbeEvent>( SW_DELEGATE_FUNCTION( ResizeDelegate, onReleaseProbeResize ) );
    dispatcher.publish( event );
    SW_EXPECT_EQUAL( 2, s_releaseProbeCount );

    dispatcher.clear();
}

/**
 * @brief [EventTest] 버스 스레드는 큐를 비우는 스레드다 — 주인이 정해지기 전에는 어느 스레드든, 정해진 뒤에는 그 스레드만 바로 발행할 수 있다
 * @details 바로 알리고 싶은 쪽(`GameEventUtil::send`)이 `publish` 와 `push` 를 고르는 물음이다. 주인은 예전에는 Debug 에서만 기억해(단언용) 배포본에서
 *          물을 수 없었다 — 그래서 게임플레이 이벤트는 늘 큐로 실려 한 프레임 늦었다.
 */
SW_TEST_CASE( EventTest, BusThreadIsTheThreadThatProcessesEvents )
{
    sw::EventDispatcher dispatcher;
    bool                bOtherThreadBeforeOwner = false;
    std::thread         before( [&dispatcher, &bOtherThreadBeforeOwner]()
    { bOtherThreadBeforeOwner = dispatcher.isBusThread(); } );
    before.join();
    SW_EXPECT_TRUE( bOtherThreadBeforeOwner );
    SW_EXPECT_TRUE( dispatcher.isBusThread() );

    dispatcher.processEvents(); // 이 스레드가 주인이 된다
    SW_EXPECT_TRUE( dispatcher.isBusThread() );
    bool        bOtherThreadAfterOwner = true;
    std::thread after( [&dispatcher, &bOtherThreadAfterOwner]()
    { bOtherThreadAfterOwner = dispatcher.isBusThread(); } );
    after.join();
    SW_EXPECT_FALSE( bOtherThreadAfterOwner );
}

/**
 * @brief [EventTest] 큐에 쌓인 이벤트 가운데 내리려는 이미지가 정의한 것은 그 자리에서 파괴되고 빠진다 — 나머지는 순서대로 남아 발행된다
 * @details 모듈이 `push` 한 이벤트의 소멸자 · 타입 조회는 그 모듈 이미지의 vtable 을 지난다. 이미지를 내린 뒤 `processEvents` · 디스패처 소멸이 그 이벤트를
 *          만지면 내려간 코드로 뛴다. 여기서는 범위를 한 이벤트 타입의 vtable 하나로 좁힌다.
 */
SW_TEST_CASE( EventTest, ReleaseQueuedEventsDestroysTheEventsTheImageDefined )
{
    sw::EventDispatcher dispatcher;
    DestructorCountingEvent::s_liveCount = 0;
    s_keptProbeSum                       = 0;
    using KeptDelegate                   = sw::Delegate<void( const KeptProbeEvent& )>;
    dispatcher.subscribe<KeptProbeEvent>( SW_DELEGATE_FUNCTION( KeptDelegate, onKeptProbe ) );
    {
        DestructorCountingEvent dropped;
        dispatcher.push( dropped );
        KeptProbeEvent kept;
        kept._value = 3;
        dispatcher.push( kept );
        dispatcher.push( dropped );
        kept._value = 4;
        dispatcher.push( kept );

        const uint8* pVtable = static_cast<const uint8*>( sw::IModuleCodeHolder::findVtableAddress( &dropped ) );
        SW_EXPECT_EQUAL( 3, DestructorCountingEvent::s_liveCount );
        SW_EXPECT_EQUAL( 2u, dispatcher.releaseQueuedEventsWithin( pVtable, pVtable + 1 ) );
        SW_EXPECT_EQUAL( 1, DestructorCountingEvent::s_liveCount ); // 큐의 둘은 소멸자가 불렸다
        SW_EXPECT_EQUAL( size_t( 2 ), dispatcher.getPendingEventCount() );
    }

    dispatcher.processEvents();
    SW_EXPECT_EQUAL( 7, s_keptProbeSum );
    SW_EXPECT_EQUAL( 0, DestructorCountingEvent::s_liveCount );
    dispatcher.clear();
}

/**
 * @brief [EventTest] 내리려는 이미지가 만든 채널을 다른 코드가 아직 구독하면 "이미지를 내리지 말라" 고 답하고, 구독이 없으면 채널을 치운다
 * @details 채널 항목의 브로드캐스트 함수는 그 이벤트 타입을 처음 구독한 쪽의 이미지에 인스턴스화된다. 다른 이미지의 구독이 남았는데 내리면 다음 발행이
 *          내려간 코드로 뛴다(`releaseModuleCode` 의 keep-mapped 길). 범위는 이 실행 파일의 이미지를 구독 스텁 앞 · 뒤 둘로 나눈 것이다 — 브로드캐스트 함수는
 *          둘 중 한쪽에 들고 구독 스텁은 어느 쪽에도 들지 않으니, 그 한쪽이 "채널을 만든 이미지" 이고 구독은 "다른 코드" 다.
 */
SW_TEST_CASE( EventTest, ChannelCreatedByTheImageKeepsItMappedWhileOtherCodeSubscribes )
{
    using KeptDelegate = sw::Delegate<void( const KeptProbeEvent& )>;
    sw::EventDispatcher dispatcher;
    s_keptProbeSum                                          = 0;
    const KeptDelegate                           subscriber = SW_DELEGATE_FUNCTION( KeptDelegate, onKeptProbe );
    const sw::EventDispatcher::EventSubscription token      = dispatcher.subscribe<KeptProbeEvent>( subscriber );

    const void* pImageBegin{ nullptr };
    const void* pImageEnd{ nullptr };
    SW_ASSERT_TRUE( sw::FileUtil::findLoadedImageRange( subscriber.getCodeAddress(), pImageBegin, pImageEnd ) );
    const uint8* pStub = static_cast<const uint8*>( subscriber.getCodeAddress() );
    const void*  arrRangeBegin[2]{ pImageBegin, pStub + 1 };
    const void*  arrRangeEnd[2]{ pStub, pImageEnd };

    uint32 keepIndex{ 2 };
    for ( uint32 rangeIndex = 0; rangeIndex < 2; ++rangeIndex )
    {
        bool bKeepImageMapped{ false };
        SW_EXPECT_EQUAL( 0u, dispatcher.releaseModuleCodeWithin( arrRangeBegin[rangeIndex], arrRangeEnd[rangeIndex], bKeepImageMapped ) );
        if ( bKeepImageMapped )
        {
            SW_EXPECT_EQUAL( 2u, keepIndex ); // 한쪽에서만
            keepIndex = rangeIndex;
        }
    }
    SW_ASSERT_TRUE_MSG( keepIndex < 2u, "the channel's broadcast function was in neither half of the image" );

    // 구독은 그대로 받는다.
    KeptProbeEvent event;
    event._value = 5;
    dispatcher.publish( event );
    SW_EXPECT_EQUAL( 5, s_keptProbeSum );

    // 구독이 빠지면 같은 범위가 채널을 치우고 이미지를 내려도 된다고 답한다. 다시 구독하면 새 채널로 돈다.
    dispatcher.unsubscribe( token );
    bool bKeepImageMapped{ false };
    (void)dispatcher.releaseModuleCodeWithin( arrRangeBegin[keepIndex], arrRangeEnd[keepIndex], bKeepImageMapped );
    SW_EXPECT_FALSE( bKeepImageMapped );
    dispatcher.subscribe<KeptProbeEvent>( subscriber );
    dispatcher.publish( event );
    SW_EXPECT_EQUAL( 10, s_keptProbeSum );
    dispatcher.clear();
}

/**
 * @brief [EventTest] 복사 · 이동으로 만든 이벤트는 어느 큐에도 매달리지 않은 상태(`_next == nullptr`)로 시작한다
 * @details 큐 링크는 복사하지 않는 것이 계약이다. 생성자가 링크를 목록에 두지 않으면 사본은 그 메모리에 있던 값을 링크로 들고 시작한다 —
 *          그래서 0xFF 로 채운 메모리 위에 만들어 본다.
 */
SW_TEST_CASE( EventTest, CopiedEventStartsUnlinked )
{
    const CloseProbeEvent source{};

    alignas( CloseProbeEvent ) uint8 arrStorage[sizeof( CloseProbeEvent )];
    sw::Memory::set( arrStorage, 0xFF, sizeof( arrStorage ) );
    CloseProbeEvent* pCopy = sw_placement_new( arrStorage ) CloseProbeEvent( source );
    SW_EXPECT_TRUE( pCopy->_next.load() == nullptr );
    pCopy->~CloseProbeEvent();

    sw::Memory::set( arrStorage, 0xFF, sizeof( arrStorage ) );
    CloseProbeEvent  moveSource{};
    CloseProbeEvent* pMoved = sw_placement_new( arrStorage ) CloseProbeEvent( std::move( moveSource ) );
    SW_EXPECT_TRUE( pMoved->_next.load() == nullptr );
    pMoved->~CloseProbeEvent();
}
