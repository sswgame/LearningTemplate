// LinuxSplashThreadTest — X11 · Wayland 스플래시의 전용 스레드(nogpu, Linux 클라이언트 빌드만). 창 시스템 대신 파이프를 연결 fd 로 써서 깨우기 · 그리기 요청 · 끝내기만 본다.
// 예전에는 기동 스레드가 `updateStatus` 안에서만 연결을 처리해 긴 단계마다 Wayland 핑에 답하지 못했다(감사 P-13).
#include "pch.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Window/Linux/LinuxSplashThread.h"

#include <condition_variable>
#include <unistd.h>

using namespace sw;

namespace
{
    /** @brief 파이프 한 쌍입니다(읽는 쪽이 가짜 연결 fd). 케이스가 끝나면 닫는다. */
    class LinuxSplashThreadTestPipePair
    {
    public:
        LinuxSplashThreadTestPipePair() { _bOpened = ::pipe( _arrFD ) == 0; }
        ~LinuxSplashThreadTestPipePair()
        {
            if ( _bOpened == false )
                return;
            (void)::close( _arrFD[0] );
            (void)::close( _arrFD[1] );
        }
        LinuxSplashThreadTestPipePair( const LinuxSplashThreadTestPipePair& )            = delete;
        LinuxSplashThreadTestPipePair& operator=( const LinuxSplashThreadTestPipePair& ) = delete;

        bool  isOpened() const { return _bOpened; }
        int32 getReadFD() const { return _arrFD[0]; }

    private:
        int  _arrFD[2]{};
        bool _bOpened{ false }; ///< NOLINT(sw-bool-flag): 테스트 도우미의 칸 하나다
    };
} // namespace

/**
 * @brief [LinuxSplashThreadTest] 스레드는 첫 그림 상태를 곧 받고, 기동 스레드가 적은 상태로 다시 그리며, `stop` 에 끝난다 — 기동 스레드는 연결을 만지지 않는다.
 */
SW_TEST_CASE( LinuxSplashThreadTest, ThreadDrawsRequestedStatesUntilStopped )
{
    LinuxSplashThreadTestPipePair connection;
    SW_REQUIRE_TRUE( connection.isOpened() );

    mutex                         drawnMutex;
    std::condition_variable_any   drawnCondition;
    vector<string>                listDrawn;
    LinuxSplashThread             splashThread;
    const int32                   connectionFD = connection.getReadFD();
    LinuxSplashThread::PaintState initialState;
    initialState._status = "first";
    SW_REQUIRE_TRUE( splashThread.start(
        [&]( LinuxSplashThread& self )
    {
        self.markReady();
        for ( ;; )
        {
            LinuxSplashThread::PaintState state;
            const uint8                   wakeMask = self.waitForEvents( connectionFD, state );
            if ( ( wakeMask & static_cast<uint8>( LinuxSplashWakeReason::Quit ) ) != 0 )
                return;
            if ( ( wakeMask & static_cast<uint8>( LinuxSplashWakeReason::Redraw ) ) == 0 )
                continue;
            {
                const ScopedLock<mutex> lock{ drawnMutex };
                listDrawn.push_back( state._status );
            }
            drawnCondition.notify_all();
        }
    },
        initialState ) );

    const auto waitForDrawCount = [&]( size_t count )
    {
        UniqueLock<mutex> lock{ drawnMutex };
        drawnCondition.wait( lock, [&]()
        { return listDrawn.size() >= count; } );
    };
    waitForDrawCount( 1 ); // 첫 그림은 기동 스레드가 깨우지 않아도 온다
    LinuxSplashThread::PaintState next;
    next._status = "second";
    splashThread.requestRedraw( next );
    waitForDrawCount( 2 );
    splashThread.stop();

    SW_REQUIRE_EQUAL( size_t{ 2 }, listDrawn.size() );
    SW_EXPECT_TRUE( listDrawn[0] == "first" );
    SW_EXPECT_TRUE( listDrawn[1] == "second" );
}

/**
 * @brief [LinuxSplashThreadTest] 본문이 창을 만들지 못하면(`markFailed`, 또는 알리지 않고 돌아오면) `start` 가 false 로 돌아온다 — 기동이 영원히 기다리지 않는다.
 */
SW_TEST_CASE( LinuxSplashThreadTest, StartFailsWhenTheWindowCannotOpen )
{
    LinuxSplashThread splashThread;
    SW_EXPECT_FALSE( splashThread.start( []( LinuxSplashThread& self )
    { self.markFailed(); }, LinuxSplashThread::PaintState{} ) );
    splashThread.stop();

    LinuxSplashThread silentThread;
    SW_EXPECT_FALSE( silentThread.start( []( LinuxSplashThread& /*self*/ ) {}, LinuxSplashThread::PaintState{} ) );
    silentThread.stop();
}
