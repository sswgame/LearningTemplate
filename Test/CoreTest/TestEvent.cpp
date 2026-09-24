#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/File/FileUtil.h"

#include "TestFramework/TestFramework.h"

#include <thread>

namespace
{
    int32 s_LastResizeWidth{ 0 };
    int32 s_LastResizeHeight{ 0 };

    /** @brief 리사이즈 이벤트에서 너비·높이를 기록합니다. */
    void onWindowResize( const sw::WindowResizeEvent& e )
    {
        s_LastResizeWidth  = e._width;
        s_LastResizeHeight = e._height;
    }

    bool s_bWindowClosed{ false };
    bool s_bWindowActivated{ false };

    /** @brief 창 닫기 이벤트를 기록합니다. */
    void onWindowClose( const sw::WindowCloseEvent& )
    {
        s_bWindowClosed = true;
    }

    /** @brief 창 활성화 여부를 기록합니다. */
    void onWindowActivate( const sw::WindowActivateEvent& e )
    {
        s_bWindowActivated = e._bIsActivate;
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
    void onReleaseProbeResize( const sw::WindowResizeEvent& )
    {
        ++s_releaseProbeCount;
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

    sw::EventDispatcher                                dispatcher;
    sw::Delegate<void( const sw::WindowResizeEvent& )> del = SW_DELEGATE_FUNCTION( sw::Delegate<void( const sw::WindowResizeEvent& )>, onWindowResize );
    dispatcher.subscribe<sw::WindowResizeEvent>( del );

    sw::WindowResizeEvent event;
    event._width  = 1920;
    event._height = 1080;
    dispatcher.push( event );

    SW_EXPECT_EQUAL( 0, s_LastResizeWidth );

    dispatcher.publish( event );

    SW_EXPECT_EQUAL( 1920, s_LastResizeWidth );
    SW_EXPECT_EQUAL( 1080, s_LastResizeHeight );

    dispatcher.unsubscribe<sw::WindowResizeEvent>( del );
    dispatcher.clear();
}

/**
 * @brief [EventTest] 닫기·활성화 이벤트
 */
SW_TEST_CASE( EventTest, DispatcherCloseAndActivateEvents )
{
    s_bWindowClosed    = false;
    s_bWindowActivated = false;

    sw::EventDispatcher                                  dispatcher;
    sw::Delegate<void( const sw::WindowCloseEvent& )>    closeDel    = SW_DELEGATE_FUNCTION( sw::Delegate<void( const sw::WindowCloseEvent& )>, onWindowClose );
    sw::Delegate<void( const sw::WindowActivateEvent& )> activateDel = SW_DELEGATE_FUNCTION( sw::Delegate<void( const sw::WindowActivateEvent& )>, onWindowActivate );

    dispatcher.subscribe<sw::WindowCloseEvent>( closeDel );
    dispatcher.subscribe<sw::WindowActivateEvent>( activateDel );

    sw::WindowCloseEvent    closeEvt;
    sw::WindowActivateEvent activateEvt;
    activateEvt._bIsActivate = SW_TRUE;

    dispatcher.publish( closeEvt );
    dispatcher.publish( activateEvt );

    SW_EXPECT_TRUE( s_bWindowClosed );
    SW_EXPECT_TRUE( s_bWindowActivated );

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

    sw::EventDispatcher                                dispatcher;
    sw::Delegate<void( const sw::WindowResizeEvent& )> resizeDel = SW_DELEGATE_FUNCTION( sw::Delegate<void( const sw::WindowResizeEvent& )>, onWindowResize );
    dispatcher.subscribe<sw::WindowResizeEvent>( resizeDel );

    sw::WindowResizeEvent event;
    event._width  = 2560;
    event._height = 1440;

    dispatcher.publish( event );
    SW_EXPECT_EQUAL( 2560, s_LastResizeWidth );
    SW_EXPECT_EQUAL( 1440, s_LastResizeHeight );

    dispatcher.unsubscribe<sw::WindowResizeEvent>( resizeDel );
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

    sw::Delegate<void( const sw::WindowResizeEvent& )> uiDel = SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::WindowResizeEvent& )>, []( const sw::WindowResizeEvent& e )
    { s_uiChannelReceived = e._width; } );

    sw::Delegate<void( const sw::WindowResizeEvent& )> audioDel = SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::WindowResizeEvent& )>, []( const sw::WindowResizeEvent& e )
    { s_audioChannelReceived = e._height; } );

    dispatcher.subscribe<sw::WindowResizeEvent>( uiChannel, uiDel );
    dispatcher.subscribe<sw::WindowResizeEvent>( audioChannel, audioDel );

    sw::WindowResizeEvent event;
    event._width  = 1280;
    event._height = 720;

    dispatcher.publish<sw::WindowResizeEvent>( uiChannel, event );
    SW_EXPECT_EQUAL( 1280, s_uiChannelReceived );
    SW_EXPECT_EQUAL( 0, s_audioChannelReceived );

    dispatcher.publish<sw::WindowResizeEvent>( audioChannel, event );
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

    sw::Delegate<void( const sw::WindowResizeEvent& )> del = SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::WindowResizeEvent& )>, [&]( const sw::WindowResizeEvent& e )
    {
        ++receivedCount;
        SW_EXPECT_EQUAL( lastReceivedIndex + 1, e._width );
        lastReceivedIndex = e._width;
    } );

    dispatcher.subscribe<sw::WindowResizeEvent>( del );

    // Push 3000 events (> 90 KB, exceeding default 64KB linear arena)
    constexpr int32 kTotalEvents = 3000;
    for ( int32 index = 0; index < kTotalEvents; ++index )
    {
        sw::WindowResizeEvent evt;
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
    dispatcher.subscribe<sw::WindowResizeEvent>( SW_DELEGATE_LAMBDA(
        sw::Delegate<void( const sw::WindowResizeEvent& )>, [&]( const sw::WindowResizeEvent& e )
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
                sw::WindowResizeEvent resizeEvent;
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
    using ResizeDelegate = sw::Delegate<void( const sw::WindowResizeEvent& )>;

    sw::EventDispatcher dispatcher;
    s_releaseProbeCount = 0;
    dispatcher.subscribe<sw::WindowResizeEvent>( SW_DELEGATE_FUNCTION( ResizeDelegate, onReleaseProbeResize ) );

    sw::WindowResizeEvent event;
    event._width  = 10;
    event._height = 20;
    dispatcher.publish( event );
    SW_EXPECT_EQUAL( 1, s_releaseProbeCount );

    const void* pBegin{ nullptr };
    const void* pEnd{ nullptr };
    SW_ASSERT_TRUE( sw::FileUtil::findLoadedImageRange( reinterpret_cast<const void*>( &onReleaseProbeResize ), pBegin, pEnd ) );

    uint32 stuckEntryCount{ 99 };
    SW_EXPECT_EQUAL( 1u, dispatcher.releaseCodeWithin( pBegin, pEnd, stuckEntryCount ) );
    SW_EXPECT_EQUAL( 0u, stuckEntryCount );

    dispatcher.publish( event );
    SW_EXPECT_EQUAL( 1, s_releaseProbeCount );

    // 항목이 지워졌어도 다시 구독하면 이쪽에서 새 항목을 만든다.
    dispatcher.subscribe<sw::WindowResizeEvent>( SW_DELEGATE_FUNCTION( ResizeDelegate, onReleaseProbeResize ) );
    dispatcher.publish( event );
    SW_EXPECT_EQUAL( 2, s_releaseProbeCount );

    dispatcher.clear();
}
