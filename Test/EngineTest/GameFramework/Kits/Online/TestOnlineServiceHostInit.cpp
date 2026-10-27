// 서비스 호스트 초기화가 도중에 실패할 때의 정리 — `listen` 이 실패해도 이미 선 엔드포인트 · 요청 서버 · 전송을 내려야 한다.
// 전에는 `_bInitialized` 가 맨 끝에서만 켜져서 실패한 뒤 `shutdown` · 소멸자가 아무것도 하지 않았다.
#include "pch.h"

#include "Core/Network/Transport/IStreamTransport.h"

#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief `listen` 만 실패하는 가짜 전송입니다. `initialize` · `shutdown` 호출 횟수를 센다. */
    class FailingListenTransport final : public IStreamTransport
    {
    public:
        [[nodiscard]] bool initialize( IStreamHandler* pHandler, const StreamTransportConfig& settings ) override
        {
            (void)pHandler;
            (void)settings;
            ++_initializeCount;
            return true;
        }
        void shutdown() override { ++_shutdownCount; }
        bool listen( const NetAddress& bindAddress ) override
        {
            (void)bindAddress;
            return false;
        }
        uint16                 getListenPort() const override { return 0; }
        StreamConnectionHandle connect( const NetAddress& remote ) override
        {
            (void)remote;
            return StreamConnectionHandle{};
        }
        StreamSendResult send( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            (void)handle;
            (void)pData;
            (void)size;
            return StreamSendResult::Closed;
        }
        void close( StreamConnectionHandle handle, StreamCloseMode mode ) override
        {
            (void)handle;
            (void)mode;
        }
        void setReceivePaused( StreamConnectionHandle handle, bool bPaused ) override
        {
            (void)handle;
            (void)bPaused;
        }
        int32 pollIO( int32 timeoutMilli ) override
        {
            (void)timeoutMilli;
            return 0;
        }
        NetAddress getRemoteAddress( StreamConnectionHandle handle ) const override
        {
            (void)handle;
            return NetAddress{};
        }
        StreamTransportStats getStats() const override { return StreamTransportStats{}; }

        uint32 _initializeCount{ 0 };
        uint32 _shutdownCount{ 0 };
    };
} // namespace

/**
 * @brief [OnlineServiceHostInitTest] `listen` 이 실패하면 이미 초기화한 전송을 내린다
 * @details 변이: 실패 반환 앞의 `releaseTransportSide()` 를 빼면 `_shutdownCount` 가 0 으로 남는다(호스트가 초기화 실패 뒤 아무것도 내리지 않는다).
 */
SW_TEST_CASE( OnlineServiceHostInitTest, ListenFailureShutsTheTransportDown )
{
    FailingListenTransport  transport;
    OnlineServiceHost       host;
    OnlineServiceHostConfig settings;
    string                  error;

    SW_EXPECT_FALSE( host.initialize( &transport, settings, error ) );
    SW_EXPECT_EQUAL( 1u, transport._initializeCount );
    SW_EXPECT_TRUE_MSG( transport._shutdownCount >= 1u, "a failed initialize left the transport running" );
    SW_EXPECT_FALSE( error.empty() );
}
