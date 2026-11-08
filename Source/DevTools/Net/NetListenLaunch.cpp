#include "pch.h"

#include "DevTools/Net/NetListenLaunch.h"

#include "Core/Foundation/Constants.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Net/GameNetSession.h"

namespace sw
{
    /** @brief `-gv_netListenPort=<포트>`: 게임 인스턴스가 이 UDP 포트를 열고 클라이언트를 받습니다(0=끄기). 리슨 호스트 · 전용 서버(설정보다 우선) 모두. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_netListenPort, 0, "게임 네트워크 서버로 열 UDP 포트 (0=끄기)" );
    /**
     * @brief `-gv_netLoopbackOnly=0`: `-gv_netListenPort` 로 연 서버의 UDP 소켓을 모든 인터페이스에 묶는다(기본 1 = 127.0.0.1 만).
     * @details 모든 인터페이스에 묶으면 Windows 방화벽이 실행 파일 경로마다 "허용" 창을 띄운다 — 워크트리 · 테스트 실행 파일이 많은 개발 환경에서는 매번 뜬다.
     *          다른 기계와 붙을 때만 0 으로 둔다. 클라이언트는 서버 주소가, 서버 설정으로 연 전용 서버는 `ServerConfig::_listenAddress` 가 묶을 곳을 정한다 — 이 값을 읽지 않는다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_netLoopbackOnly, true, "게임 UDP 소켓을 127.0.0.1 에만 묶는다 (0=모든 인터페이스, 다른 기계와 붙을 때)" );

    NetListenLaunchSubsystem::NetListenLaunchSubsystem( GameNetSession& session )
        : _pSession{ &session }
    {
    }

    NetListenLaunchSubsystem::~NetListenLaunchSubsystem() = default;

    span<const utf8* const> NetListenLaunchSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { GameNetSession::kSubsystemName };
        return kArrDependency;
    }

    SubsystemInitResult NetListenLaunchSubsystem::initializeSubsystem()
    {
        _pSession->setLaunchOverride( SW_DELEGATE_METHOD( GameNetLaunchOverrideDelegate, &NetListenLaunchSubsystem::makeListenParams, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void NetListenLaunchSubsystem::shutdownSubsystem()
    {
        _pSession->setLaunchOverride( GameNetLaunchOverrideDelegate{} );
    }

    GameNetLaunch NetListenLaunchSubsystem::makeListenParams( GameNetSessionParams& outParams )
    {
        if ( gv_netListenPort == 0 )
            return GameNetLaunch::None;
        const bool bPortInRange = 0 < gv_netListenPort && gv_netListenPort <= constant::network::kMaxPort;
        if ( bPortInRange == false )
        {
            SW_LOG_ERROR_ID( "NetListenLaunch.PortNotUDP", "gv_netListenPort %# is not a UDP port", gv_netListenPort );
            return GameNetLaunch::Invalid;
        }
        outParams._role          = GameNetRole::Server;
        outParams._listenPort    = static_cast<uint16>( gv_netListenPort );
        outParams._bLoopbackOnly = gv_netLoopbackOnly;
        return GameNetLaunch::Open;
    }
} // namespace sw
