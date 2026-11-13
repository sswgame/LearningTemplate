#include "pch.h"

#include "ModuleManager/Host/ServerDebugControl.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Process/ShutdownSignal.h"
#include "Core/String/StringUtil.h"

#include "DevTools/Console/DevConsole.h"

#include "Engine/Observability/DebugChannel.h"

// 개발 구성에만 있다 — 배포는 짓지 않고 `ServerApp` 이 `build::kShipping` 이 아닐 때만 만든다.

namespace sw
{
    /**
     * @class ServerDebugControl
     * @brief 전용 서버의 개발 조종입니다. 파일 머리말 참고.
     */
    class ServerDebugControl final : public IServerDebugControl
    {
    public:
        explicit ServerDebugControl( const ServerTickCountDelegate& tickCount );
        ~ServerDebugControl() override;
        ServerDebugControl( const ServerDebugControl& )            = delete;
        ServerDebugControl& operator=( const ServerDebugControl& ) = delete;

        const utf8*        getHelpSuffix() const override;
        void               startConsole() override;
        void               stopConsole() override;
        [[nodiscard]] bool startChannel() override;
        void               stopChannel() override;
        void               poll() override;
        [[nodiscard]] bool executeCommand( string_view command ) override;
        bool               isHoldingTick() const override { return _bPaused && _pendingStepCount == 0; }
        bool               isPaused() const override { return _bPaused; }
        void               onTickFinished() override;

    private:
        /** @brief 일시정지 명령(`pause` · `resume` · `step [N]`)이면 처리하고 true 입니다. */
        [[nodiscard]] bool executePauseCommand( string_view command );

    private:
        /** @brief 개발 콘솔 · 디버그 채널입니다. .cpp 가 정의한다. */
        struct DevParts;

        ServerTickCountDelegate _tickCount;
        unique_ptr<DevParts>    _pDev;                  ///< `startConsole` 이 세우고 `stopConsole` 이 지운다
        uint32                  _pendingStepCount{ 0 }; ///< 일시정지 중 더 돌 틱 수(`step N`)
        uint8                   _bPaused : 1;           ///< 게임 틱을 멈춘다(콘솔, 종료 신호, 네트워크 스레드는 돈다)
    };

    /** @brief `-gv_debugChannelPort=N`: 디버그 채널(개발 명령 원격 실행, 루프백 TCP)을 이 포트에 엽니다(0=끄기). 토큰(`gv_debugChannelToken`)이 있어야 열립니다. Shipping 에는 없습니다. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_debugChannelPort, 0, "디버그 채널 포트 (0=끄기, 루프백 전용, 토큰 필요)" );
    SW_TEST_GLOBAL_VARIABLE( string, gv_debugChannelToken, "", "디버그 채널 접속 토큰" );
    /**
     * @brief `-gv_serverStartPaused=1`: 첫 틱을 돌기 전에 멈춘 채로 기동합니다(서버 디버깅 S7 되감기).
     * @details 명령(`intent.play` · `step N`)은 기동 뒤 표준 입력이나 디버그 채널로 오므로 그 사이 틱이 돌면 목표 틱에서 정확히 멈출 수 없다. 멈춘 채 뜨면 틱 0 에서
     *          재생을 걸고 `step` 으로 한 틱씩 진행할 수 있다(언리얼 `-pause` · 유니티 Play Mode 의 Pause On Start 와 같은 쓰임). Shipping 에는 없다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_serverStartPaused, false, "첫 틱 전에 멈춘 채 기동 (재생 서버를 틱 0 에서 잡는다)" );
    /** @brief `-gv_serverExitAfterTicks=N`: N 틱을 돈 뒤 스스로 정상 종료합니다(기동과 종료 테스트 · 스크립트의 서버 짝 실행). 배포 서버는 운영과 같은 종료 신호로 끝낸다. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_serverExitAfterTicks, 0, "이 틱 수를 돈 뒤 정상 종료 (0=끄기)" );
} // namespace sw

namespace sw
{
    namespace
    {
        struct ServerDebugControlInternal
        {
            /** @brief Dev 서버 루프의 명령(`executePauseCommand`)입니다. 표준 입력 `help` 와 디버그 채널 `help` 가 같은 줄을 보입니다. */
            static constexpr const utf8* kLoopCommandHelp = "pause | resume | step [N]";
        };
    } // namespace

    struct ServerDebugControl::DevParts
    {
        DevConsole                   _console;
        unique_ptr<IStreamTransport> _transport; ///< 디버그 채널의 스트림 전송(채널을 종료한 뒤 지운다)
        unique_ptr<DebugChannel>     _channel;   ///< 읽기 중심의 원격 개발 명령 채널(`gv_debugChannelPort` 가 0 이 아닐 때만)
    };

    ServerDebugControl::ServerDebugControl( const ServerTickCountDelegate& tickCount )
        : _tickCount{ tickCount }
        , _pDev{ nullptr }
        , _bPaused{ SW_FALSE }
    {
    }

    ServerDebugControl::~ServerDebugControl() = default;

    const utf8* ServerDebugControl::getHelpSuffix() const
    {
        return " (Dev: pause | resume | step [N], dev commands and gv_* variables too)";
    }

    void ServerDebugControl::startConsole()
    {
        _pDev = make_unique<DevParts>();
        if ( gv_serverStartPaused )
        {
            _bPaused = SW_TRUE;
            SW_LOG_INFO_ID( "Server.ServerStartsPausedTick", "Server starts paused at tick %# (-gv_serverStartPaused)", _tickCount() );
        }
    }

    void ServerDebugControl::stopConsole()
    {
        stopChannel();
        _pDev.reset();
    }

    bool ServerDebugControl::startChannel()
    {
        if ( gv_debugChannelPort <= 0 )
            return true;
        if ( _pDev == nullptr )
        {
            SW_LOG_ERROR_ID( "Server.DebugChannelRequestedGv", "The debug channel was requested (-gv_debugChannelPort=%#) before the dev console started", gv_debugChannelPort );
            return false;
        }
        // 토큰 변수 이름은 이 개발 파일만 안다 — 채널(관리 채널과 함께 쓰는 배포 코드)은 이름 없이 거절만 한다.
        if ( gv_debugChannelToken.empty() )
        {
            SW_LOG_ERROR_ID( "Server.DebugChannelNeedsTokenGv", "The debug channel was requested (-gv_debugChannelPort=%#) without -gv_debugChannelToken - not started", gv_debugChannelPort );
            return false;
        }
        const uint16 port = static_cast<uint16>( gv_debugChannelPort );
        _pDev->_transport = StreamTransportFactory::createPlatformTransport();
        _pDev->_channel   = make_unique<DebugChannel>();
        DebugChannelConfig settings;
        settings._bindAddress = NetAddress::makeLoopback( port );
        settings._token       = gv_debugChannelToken;
        StreamTransportConfig transportSettings;
        transportSettings._idleTimeoutSeconds = 0.0; // 에디터는 서버가 멈춰 있는 동안에도 붙어 있다
        const bool bStarted                   = _pDev->_channel->initialize( _pDev->_transport.get(), transportSettings, settings,
                                                                             SW_DELEGATE_LAMBDA( DebugChannelExecuteDelegate, [this]( string_view line, string& outReply )
                          {
            // 일시정지 · 한 틱 진행은 개발 명령이 아니라 서버 루프의 명령이다 - 같은 길로 받는다. 답은 서버가 지금 어디인지다.
            if ( executePauseCommand( line ) )
            {
                outReply = ( _bPaused ? "paused tick " : "running tick " ) + to_string( _tickCount() );
                return true;
            }
            const bool bOk = _pDev->_console.execute( line, outReply ) == DevConsoleResult::Ok;
            // 채널 `help` 는 채널이 받는 줄 전부를 알린다 — 개발 콘솔의 목록(변수 · 개발 명령, 에디터 자동완성의 원천) 앞에 서버 루프 명령 한 줄.
            const string_view trimmed = StringUtil::trim( line );
            if ( bOk && StringUtil::equals( trimmed.substr( 0, trimmed.find( ' ' ) ), "help", true ) )
            {
                string reply{ "server loop: " };
                reply.append( ServerDebugControlInternal::kLoopCommandHelp ).append( "\n" ).append( outReply );
                outReply = std::move( reply );
            }
            return bOk;
        } ) );
        if ( bStarted == false )
        {
            SW_LOG_ERROR_ID( "Server.DebugChannelRequestedGv2", "The debug channel was requested (-gv_debugChannelPort=%#) but could not start", gv_debugChannelPort );
            _pDev->_channel.reset();
            _pDev->_transport.reset();
            return false;
        }
        return true;
    }

    void ServerDebugControl::stopChannel()
    {
        if ( _pDev == nullptr )
            return;
        if ( _pDev->_channel != nullptr )
            _pDev->_channel->shutdown();
        _pDev->_channel.reset();
        _pDev->_transport.reset();
    }

    void ServerDebugControl::poll()
    {
        if ( _pDev != nullptr && _pDev->_channel != nullptr )
            (void)_pDev->_channel->poll();
    }

    bool ServerDebugControl::executeCommand( string_view command )
    {
        if ( executePauseCommand( command ) )
            return true;
        // Dev 는 개발 콘솔과 같은 명령(개발 명령과 gv_* 전역 변수)을 받는다. Shipping 서버는 ServerApp 의 네 가지만 받는다.
        if ( _pDev != nullptr )
        {
            (void)_pDev->_console.submit( command ); // 응답과 실패는 DevConsole 이 로그에 남긴다
            return true;
        }
        return false;
    }

    void ServerDebugControl::onTickFinished()
    {
        const uint64 tickCount = _tickCount();
        if ( _bPaused && _pendingStepCount > 0 && --_pendingStepCount == 0 )
            SW_LOG_INFO_ID( "Server.ServerSteppedTickPaused", "Server stepped to tick %# (paused)", tickCount );
        if ( gv_serverExitAfterTicks > 0 && tickCount >= static_cast<uint64>( gv_serverExitAfterTicks ) )
            ShutdownSignal::request( ShutdownCause::TickLimit );
    }

    bool ServerDebugControl::executePauseCommand( string_view command )
    {
        if ( command == "pause" )
        {
            _bPaused          = SW_TRUE;
            _pendingStepCount = 0;
            SW_LOG_INFO_ID( "Server.ServerPausedTick", "Server paused at tick %#", _tickCount() );
            return true;
        }
        if ( command == "resume" )
        {
            _bPaused          = SW_FALSE;
            _pendingStepCount = 0;
            SW_LOG_INFO_ID( "Server.ServerResumedTick", "Server resumed at tick %#", _tickCount() );
            return true;
        }
        const string_view kStep = "step";
        const bool        bStep = command.substr( 0, kStep.size() ) == kStep && ( command.size() == kStep.size() || command[kStep.size()] == ' ' );
        if ( bStep == false )
            return false;
        int32             stepCount = 1;
        const string_view argument  = StringUtil::trim( command.substr( kStep.size() ) );
        if ( argument.empty() == false && ( StringUtil::parseInt( argument, stepCount ) == false || stepCount <= 0 ) )
        {
            SW_LOG_WARNING_ID( "Server.StepTakesPositiveTick", "step takes a positive tick count - got '%#'", string( argument ).c_str() );
            return true;
        }
        if ( _bPaused == false )
            SW_LOG_INFO_ID( "Server.ServerPausedTick", "Server paused at tick %#", _tickCount() );
        _bPaused          = SW_TRUE; // 도는 중에 step 이면 멈추고 그만큼만 돈다.
        _pendingStepCount = static_cast<uint32>( stepCount );
        return true;
    }

    SW_HOST_DEV_API unique_ptr<IServerDebugControl> makeServerDebugControl( const ServerTickCountDelegate& tickCount )
    {
        return make_unique<ServerDebugControl>( tickCount );
    }
} // namespace sw
