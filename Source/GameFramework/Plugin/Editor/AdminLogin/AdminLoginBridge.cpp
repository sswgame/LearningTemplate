#include "pch.h"

#include "GameFramework/Plugin/Editor/AdminLogin/AdminLoginBridge.h"

#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "GameFramework/Plugin/Editor/Admin/AdminToolSession.h"

namespace sw::editor
{
    SW_LOG_CALLER( "AdminLoginBridge" );

    AdminLoginBridge::AdminLoginBridge()
        : _login{}
        , _credentialsSubscription{}
        , _loginHookSubscription{}
        , _frameHookSubscription{}
    {
    }

    span<const utf8* const> AdminLoginBridge::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { AdminToolSession::kSubsystemName };
        return kArrDependency;
    }

    SubsystemInitResult AdminLoginBridge::initializeSubsystem()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
        {
            // GM Tool 확장이 올라오지 않았다 - 이을 패널이 없다. 조립할 대상이 없는 것이라 에디터 기동을 멈추지 않는다.
            SW_LOG_WARNING( "The GM Tool session is not available - the GM login assembly stays idle" );
            return SubsystemInitResult::Succeeded;
        }
        AccountClientInfo info;
        info._build    = kGmToolBuild;
        info._platform = kGmToolPlatform;
        // 계정 클라이언트는 연결을 열기 전에 서비스 클라이언트에 등록해야 한다. 등록이 안 되면(영역 겹침) 로그인 없는 반쪽 조립이므로 서지 않는다.
        if ( _login.attach( pSession->getConnection().getServiceClient(), info ) == false )
        {
            SW_LOG_ERROR( "The GM login assembly could not attach the account client to the GM connection" );
            return SubsystemInitResult::Failed;
        }
        _credentialsSubscription = pSession->bindCredentialsHandler( AdminToolSession::CredentialsHandler::create<&AdminLoginBridge::acceptCredentials>( this ) );
        _loginHookSubscription   = pSession->getConnection().bindLoginHook( AdminToolConnection::LoginHook::create<&AdminLoginBridge::onReady>( this ) );
        _frameHookSubscription   = pSession->bindFrameHook( AdminToolSession::FrameHook::create<&AdminLoginBridge::onFrame>( this ) );
        return SubsystemInitResult::Succeeded;
    }

    void AdminLoginBridge::shutdownSubsystem()
    {
        _frameHookSubscription.reset();
        _loginHookSubscription.reset();
        _credentialsSubscription.reset();
    }

    bool AdminLoginBridge::acceptCredentials( string_view loginName, string_view password )
    {
        return _login.setCredentials( loginName, password );
    }

    void AdminLoginBridge::onReady( OnlineServiceClient& client )
    {
        _login.onReady( client );
    }

    void AdminLoginBridge::onFrame()
    {
        _login.update();
    }

    SW_EDITOR_SUBSYSTEM( AdminLoginBridge, 9250 );
} // namespace sw::editor
