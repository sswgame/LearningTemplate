// EventDispatcher — 즉시 발행 · 큐(push → processEvents) · 고정 ID 이벤트 · 채널 필터 · 아레나 넘침 · 큐에 남은 이벤트의 파괴(clear · 소멸자) ·
// 워커 스레드의 push · 버스 스레드 판정 · 복사한 이벤트의 큐 링크.
#include "pch.h"

#include "Core/Concurrency/thread.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

namespace
{
    /** @brief 고정 ID 이벤트(`SW_REGISTER_EVENT_ID`) 테스트가 쓰는 엔진 예약 구간(1..255)의 번호입니다. */
    constexpr sw::EventTypeID kActivateProbeEventID = 1;
    constexpr sw::EventTypeID kCloseProbeEventID    = 2;
    constexpr sw::EventTypeID kResizeProbeEventID   = 3;

    /** @brief 고정 ID 이벤트 테스트용입니다 — 예약 ID 를 쓰는 값 타입의 모양(크기 페이로드)만 흉내 냅니다. */
    struct ResizeProbeEvent final : sw::IEvent
    {
        int32 _width{ 0 };
        int32 _height{ 0 };

        SW_REGISTER_EVENT_ID( kResizeProbeEventID );
    };

    /** @brief 페이로드 없는 고정 ID 이벤트입니다. */
    struct CloseProbeEvent final : sw::IEvent
    {
        SW_REGISTER_EVENT_ID( kCloseProbeEventID );
    };

    /** @brief 불리언 하나를 싣는 고정 ID 이벤트입니다. */
    struct ActivateProbeEvent final : sw::IEvent
    {
        bool _bActive{ false };

        SW_REGISTER_EVENT_ID( kActivateProbeEventID );
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

    /** @brief 소멸 횟수를 세는 테스트용 이벤트 — 큐에 남은 것이 실제로 파괴되는지 본다. */
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
} // namespace

/**
 * @brief 디스패처 push 와 dispatch
 */
SW_TEST_CASE( EventTest, DispatcherPushAndDispatch )
{
    s_LastResizeWidth  = 0;
    s_LastResizeHeight = 0;

    sw::EventDispatcher                           dispatcher;
    sw::Delegate<void( const ResizeProbeEvent& )> del          = SW_DELEGATE_FUNCTION( sw::Delegate<void( const ResizeProbeEvent& )>, onResizeProbe );
    sw::Subscription                              subscription = dispatcher.subscribe<ResizeProbeEvent>( del );

    ResizeProbeEvent event;
    event._width  = 1920;
    event._height = 1080;
    dispatcher.push( event );

    SW_EXPECT_EQUAL( 0, s_LastResizeWidth );

    dispatcher.publish( event );

    SW_EXPECT_EQUAL( 1920, s_LastResizeWidth );
    SW_EXPECT_EQUAL( 1080, s_LastResizeHeight );

    subscription.reset();
    dispatcher.clear();
}

/**
 * @brief 닫기·활성화 이벤트
 */
SW_TEST_CASE( EventTest, DispatcherCloseAndActivateEvents )
{
    s_bCloseReceived    = false;
    s_bActivateReceived = false;

    sw::EventDispatcher                             dispatcher;
    sw::Delegate<void( const CloseProbeEvent& )>    closeDel    = SW_DELEGATE_FUNCTION( sw::Delegate<void( const CloseProbeEvent& )>, onCloseProbe );
    sw::Delegate<void( const ActivateProbeEvent& )> activateDel = SW_DELEGATE_FUNCTION( sw::Delegate<void( const ActivateProbeEvent& )>, onActivateProbe );

    const sw::Subscription closeSubscription    = dispatcher.subscribe<CloseProbeEvent>( closeDel );
    const sw::Subscription activateSubscription = dispatcher.subscribe<ActivateProbeEvent>( activateDel );

    CloseProbeEvent    closeEvt;
    ActivateProbeEvent activateEvt;
    activateEvt._bActive = true;

    dispatcher.publish( closeEvt );
    dispatcher.publish( activateEvt );

    SW_EXPECT_TRUE( s_bCloseReceived );
    SW_EXPECT_TRUE( s_bActivateReceived );

    SW_EXPECT_EQUAL( kCloseProbeEventID, closeEvt.getEventType() );
    SW_EXPECT_EQUAL( kActivateProbeEventID, activateEvt.getEventType() );

    dispatcher.clear();
}

/**
 * @brief 지연 이벤트 큐
 */
SW_TEST_CASE( EventTest, DeferredEventQueueTest )
{
    s_LastResizeWidth  = 0;
    s_LastResizeHeight = 0;

    sw::EventDispatcher                           dispatcher;
    sw::Delegate<void( const ResizeProbeEvent& )> resizeDel    = SW_DELEGATE_FUNCTION( sw::Delegate<void( const ResizeProbeEvent& )>, onResizeProbe );
    sw::Subscription                              subscription = dispatcher.subscribe<ResizeProbeEvent>( resizeDel );

    ResizeProbeEvent event;
    event._width  = 2560;
    event._height = 1440;

    dispatcher.publish( event );
    SW_EXPECT_EQUAL( 2560, s_LastResizeWidth );
    SW_EXPECT_EQUAL( 1440, s_LastResizeHeight );

    subscription.reset();
    s_LastResizeWidth = 0;
    dispatcher.publish( event );
    SW_EXPECT_TRUE_MSG( s_LastResizeWidth == 0, "풀린 구독이 아직 받는다" );
    dispatcher.clear();
}

/**
 * @brief 채널 필터링
 */
SW_TEST_CASE( EventTest, EventDispatcherChannelFiltering )
{
    sw::EventDispatcher dispatcher;

    static int32 s_uiChannelReceived{ 0 };
    static int32 s_audioChannelReceived{ 0 };
    s_uiChannelReceived    = 0; // --test-repeat 의 다음 회차가 앞 회차 값을 보지 않게
    s_audioChannelReceived = 0;

    sw::hashed_string uiChannel( "UI_Channel" );
    sw::hashed_string audioChannel( "Audio_Channel" );

    sw::Delegate<void( const ResizeProbeEvent& )> uiDel = SW_DELEGATE_LAMBDA( sw::Delegate<void( const ResizeProbeEvent& )>, []( const ResizeProbeEvent& e )
    { s_uiChannelReceived = e._width; } );

    sw::Delegate<void( const ResizeProbeEvent& )> audioDel = SW_DELEGATE_LAMBDA( sw::Delegate<void( const ResizeProbeEvent& )>, []( const ResizeProbeEvent& e )
    { s_audioChannelReceived = e._height; } );

    const sw::Subscription uiSubscription    = dispatcher.subscribe<ResizeProbeEvent>( uiChannel, uiDel );
    const sw::Subscription audioSubscription = dispatcher.subscribe<ResizeProbeEvent>( audioChannel, audioDel );

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
 * @brief 64KB 프레임 할당자를 초과하는 대량 이벤트 큐잉 시 오버플로우 메모리 처리 및 0-유실 검증
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

    const sw::Subscription subscription = dispatcher.subscribe<ResizeProbeEvent>( del );

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
 * @brief `clear()` 는 큐에 남은 이벤트를 **파괴하고** 버린다
 * @details `processEvents` 는 방송한 뒤 `pEvent->~IEvent()` 를 부른다. `clear()` 가 큐 맵만
 *          비우고 아레나를 되감으면 안 된다. 이벤트는 아레나에 placement new 로 올라가므로, 소멸자를
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
 * @brief 디스패처가 죽을 때도 큐에 남은 이벤트를 파괴한다
 * @details `clear()` 와 같은 계약이다. 소멸자가 큐에 남은 이벤트를 파괴하지 않으면 종료 시점에 그대로 샌다.
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
 * @brief 다른 스레드에서 `push` 한 이벤트가 퍼내는 스레드에 도착한다
 * @details 이것이 이 클래스가 실제로 쓰이는 방식이자, **교차 스레드로 지원되는 유일한 입구**다.
 *          워커가 큐에 밀어 넣고(`_queueSpinLock` + 프레임 아레나가 지킨다), 버스를 퍼내는
 *          스레드가 프레임마다 `processEvents` 로 빼서 방송한다(`Engine::update`).
 *          버스 쪽(subscribe/publish)은 퍼내는 스레드 전용이며 Debug 에서 확인한다.
 */
SW_TEST_CASE( EventTest, PushFromWorkerThreadsReachesPumpingThread )
{
    sw::EventDispatcher dispatcher;

    int32                  receivedCount = 0;
    int32                  widthSum      = 0;
    const sw::Subscription subscription  = dispatcher.subscribe<ResizeProbeEvent>( SW_DELEGATE_LAMBDA(
        sw::Delegate<void( const ResizeProbeEvent& )>, [&]( const ResizeProbeEvent& e )
    {
        ++receivedCount;
        widthSum += e._width;
    } ) );

    constexpr int32 kThreadCount     = 4;
    constexpr int32 kEventsPerThread = 25;

    sw::thread arrWorker[kThreadCount];
    for ( int32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        arrWorker[threadIndex] = sw::thread( [&dispatcher]()
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
    for ( sw::thread& worker : arrWorker )
        worker.join();

    SW_EXPECT_EQUAL( static_cast<size_t>( kThreadCount * kEventsPerThread ), dispatcher.getPendingEventCount() );

    dispatcher.processEvents();

    SW_EXPECT_EQUAL( kThreadCount * kEventsPerThread, receivedCount );
    SW_EXPECT_EQUAL( kThreadCount * kEventsPerThread, widthSum );
    SW_EXPECT_EQUAL( size_t{ 0 }, dispatcher.getPendingEventCount() );

    dispatcher.clear();
}

/**
 * @brief 버스 스레드는 큐를 비우는 스레드다 — 주인이 정해지기 전에는 어느 스레드든, 정해진 뒤에는 그 스레드만 바로 발행할 수 있다
 * @details 바로 알리고 싶은 쪽(`GameEventUtil::send`)이 `publish` 와 `push` 를 고르는 물음이다. 주인을 Debug 에서만 기억하면(단언용) 배포본에서
 *          물을 수 없어 게임플레이 이벤트가 늘 큐로 실려 한 프레임 늦는다.
 */
SW_TEST_CASE( EventTest, BusThreadIsTheThreadThatProcessesEvents )
{
    sw::EventDispatcher dispatcher;
    bool                bOtherThreadBeforeOwner = false;
    sw::thread          before( [&dispatcher, &bOtherThreadBeforeOwner]()
    { bOtherThreadBeforeOwner = dispatcher.isBusThread(); } );
    before.join();
    SW_EXPECT_TRUE( bOtherThreadBeforeOwner );
    SW_EXPECT_TRUE( dispatcher.isBusThread() );

    dispatcher.processEvents(); // 이 스레드가 주인이 된다
    SW_EXPECT_TRUE( dispatcher.isBusThread() );
    bool       bOtherThreadAfterOwner = true;
    sw::thread after( [&dispatcher, &bOtherThreadAfterOwner]()
    { bOtherThreadAfterOwner = dispatcher.isBusThread(); } );
    after.join();
    SW_EXPECT_FALSE( bOtherThreadAfterOwner );
}

/**
 * @brief 복사 · 이동으로 만든 이벤트는 어느 큐에도 매달리지 않은 상태(`_next == nullptr`)로 시작한다
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
