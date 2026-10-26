/**
 * @file AdminToolConnection.h
 * @brief GM 도구가 서버의 GM 포트(`ServerConfig::_adminServicePort`)에 거는 연결입니다. 전송, 서비스 클라이언트, `AdminClient` 를 한 덩어리로 들고 틱합니다.
 * @details 서버가 GM 리슨을 따로 열면(`_adminServicePort` 가 0 보다 큼) GM 서비스는 그 포트의 연결만 받는다. 이 연결은 그 주소 하나(`host:port`)로 건다.
 *          GM 연결도 일반 계정 로그인을 거치지만, 에디터 확장은 자기 키트(Admin)만 include 할 수 있어 계정 키트를 모른다.
 *          그래서 로그인은 부르는 쪽이 `LoginHook` 으로 끼운다 — 연결이 Hello 를 마칠 때마다(다시 연결한 뒤 포함) 불리고, 그 안에서 계정 클라이언트가 로그인한다.
 *          훅이 없으면 로그인하지 않은 채 `Ready` 에서 멈추고, 서버가 GM 명령을 `NotLoggedIn` 으로 거절한다(대신 로그인한 척하지 않는다).
 *          한 계정은 연결 하나에만 붙으므로 플레이어 포트와 GM 포트에 같은 계정으로 함께 로그인하지 못한다(GM 계정을 따로 쓴다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Kits/Feature/Online/Admin/Shared/AdminClient.h"

namespace sw::editor
{
    /** @brief GM 연결의 상태입니다. */
    enum class AdminToolConnectionState : uint8
    {
        Disconnected = 0,
        Connecting, ///< 연결을 걸었고 Hello 를 기다린다
        Ready,      ///< Hello 가 끝났다(로그인은 훅이 맡는다)
        Failed      ///< 주소를 읽지 못했거나 전송을 못 띄웠거나 스키마 해시가 다르다. 다시 `open` 하기 전에는 아무것도 하지 않는다
    };

    /**
     * @class AdminToolConnection
     * @brief GM 포트로 가는 연결 하나입니다. 에디터 프레임마다 `update` 를 부릅니다.
     */
    class AdminToolConnection
    {
    public:
        /** @brief 연결이 Ready 가 될 때마다 불립니다. 그 안에서 계정 클라이언트로 로그인합니다. */
        using LoginHook = Delegate<void( OnlineServiceClient& client )>;

        AdminToolConnection();
        ~AdminToolConnection();
        AdminToolConnection( const AdminToolConnection& )            = delete; // 서비스 클라이언트가 AdminClient 를 가리킨다
        AdminToolConnection& operator=( const AdminToolConnection& ) = delete;

        /**
         * @brief @p address("1.2.3.4:5678", 포트 필수)로 연결을 시작합니다. 이미 열려 있으면 먼저 닫습니다.
         * @param gameBuild Hello 에 싣는 빌드 글(64 B 이하)이다.
         * @return 주소를 읽지 못했거나 전송을 띄우지 못하면 false 이고 @p outError 에 이유가 있으며 상태는 `Failed` 입니다.
         */
        [[nodiscard]] bool open( string_view address, string_view gameBuild, string& outError );
        void               close();
        /** @brief 전송 · 요청 시한 · 다시 연결을 돌립니다. 닫혀 있으면 아무것도 하지 않습니다. */
        void update( MonotonicMilliseconds nowMs );
        void setLoginHook( const LoginHook& onReady ) { _onReady = onReady; }
        /** @brief 이 연결의 서비스 클라이언트(조립 쪽이 계정 클라이언트 같은 다른 키트 클라이언트를 `registerClientService` 로 등록하는 데 쓴다 — 연결을 열기 전에). */
        OnlineServiceClient& getServiceClient() { return _client; }

        AdminToolConnectionState getState() const { return _state; }
        /** @brief 상태를 글로 적습니다(패널 표시용). */
        const utf8* getStateText() const;
        /** @brief 열려 있지 않으면 nullptr 입니다. `AdminToolSession::useLiveClient` 에 건다. */
        AdminClient* getAdminClient() { return _state == AdminToolConnectionState::Disconnected || _state == AdminToolConnectionState::Failed ? nullptr : &_admin; }

    private:
        unique_ptr<IStreamTransport> _pTransport;
        OnlineServiceClient          _client;
        AdminClient                  _admin;
        LoginHook                    _onReady;
        AdminToolConnectionState     _state;
        bool                         _bServiceRegistered;
        bool                         _bReadyReported;
    };
} // namespace sw::editor
