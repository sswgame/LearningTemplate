/**
 * @file PlatformLoginClient.h
 * @brief 외부 로그인의 클라이언트 쪽입니다. "구글로 로그인" 을 누르면 제공자 티켓(ID 토큰, 액세스 토큰)을 얻어 계정 서버의 외부 로그인(`kPlatformLogin`)에 넘깁니다.
 * @details - 모바일은 모바일 빌드가 생기면 플랫폼 SDK 를 쓴다. PC 는 **시스템 브라우저, 루프백 리다이렉트, PKCE** 를 쓰며(RFC 8252, 임베드 웹뷰 금지) `LoopbackPkceLoginClient` 가 맡는다.
 *          - 시스템 브라우저는 `IExternalBrowser` 로 연다. OS 구현은 아래 `SystemExternalBrowser` 이고 테스트에서는 가짜를 쓴다. 테스트와 개발에는 `FakePlatformLoginClient` 를 쓴다.
 *          - OIDC 제공자의 티켓은 `ID 토큰|nonce` 형식이다. 서버의 `OidcLoginProvider` 가 nonce 를 확인해 티켓의 재사용을 막는다.
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 끝난 외부 로그인 하나입니다. */
    struct PlatformLoginClientResult
    {
        vector<uint8> _ticket{}; ///< 성공했을 때의 티켓이다. 계정 서버의 외부 로그인에 그대로 넘긴다
        string        _provider{};
        string        _failureText{}; ///< 실패했을 때 사람이 읽는 이유이다. 로그와 화면에 쓴다
        uint64        _requestID{ 0 };
        uint8         _bSucceeded{ SW_FALSE };
        uint8         _bCancelled{ SW_FALSE }; ///< 사용자가 거절했거나(`error=access_denied`) 시한이 지났다
    };
} // namespace sw

namespace sw
{
    /**
     * @class IPlatformLoginClient
     * @brief 외부 로그인 티켓을 얻는 인터페이스입니다. 모든 호출은 한 스레드(게임 스레드)에서 해야 합니다.
     */
    class IPlatformLoginClient
    {
    public:
        IPlatformLoginClient()          = default;
        virtual ~IPlatformLoginClient() = default;

        IPlatformLoginClient( const IPlatformLoginClient& )            = delete;
        IPlatformLoginClient& operator=( const IPlatformLoginClient& ) = delete;

        /** @brief @p provider 로 로그인을 시작합니다. 0이 아닌 요청 id 를 반환하며 결과의 `_requestID` 와 같습니다. 실패나 취소도 결과는 반드시 한 번 전달합니다. */
        virtual uint64 beginLogin( string_view provider, MonotonicMilliseconds nowMs ) = 0;
        virtual void   update( MonotonicMilliseconds nowMs )                           = 0;
        virtual int32  pollResults( vector<PlatformLoginClientResult>& outListResult ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class IExternalBrowser
     * @brief 시스템 기본 브라우저로 주소를 엽니다. 사용자가 저장해 둔 로그인, 비밀번호 관리자, 2단계 인증을 그대로 쓸 수 있습니다.
     */
    class IExternalBrowser
    {
    public:
        IExternalBrowser()          = default;
        virtual ~IExternalBrowser() = default;

        IExternalBrowser( const IExternalBrowser& )            = delete;
        IExternalBrowser& operator=( const IExternalBrowser& ) = delete;

        [[nodiscard]] virtual bool openURL( string_view url ) = 0;
    };

    /**
     * @class SystemExternalBrowser
     * @brief `IExternalBrowser` 의 OS 구현입니다. 시스템 기본 브라우저로 주소를 엽니다(`Process::launchURL` — Windows `ShellExecuteW`, 리눅스 `xdg-open`).
     * @details - 받는 주소는 `http://` 와 `https://` 뿐입니다. 다른 스킴(`file:`, `javascript:`, 사용자 정의 프로토콜)은 OS 가 다른 프로그램을 띄우므로 거절합니다.
     *          - RFC 3986 의 글자(비예약, 예약, `%`) 가운데 따옴표, 백슬래시, `$`, 백틱을 뺀 것만 받습니다(주소는 인자 하나로 넘어가 셸을 거치지 않지만, 잘못 만든 주소를 일찍 거른다).
     *            외부 로그인 주소는 쿼리를 퍼센트 인코딩하므로(`HTTPUtil::encodePercent`) 이 규칙에 걸리지 않습니다.
     *          - 기다리지 않습니다. 알 수 있는 실패는 "열지 못했다" 뿐입니다. 상태가 없으므로 어느 스레드에서나 부를 수 있습니다.
     */
    class SW_KIT_API SystemExternalBrowser final : public IExternalBrowser
    {
    public:
        [[nodiscard]] bool openURL( string_view url ) override;

        /** @brief 명령 줄에 실어도 되는 `http(s)://` 주소면 true 입니다. */
        static bool isOpenableURL( string_view url );
    };
} // namespace sw

namespace sw
{
    /**
     * @class FakePlatformLoginClient
     * @brief 테스트와 개발용 구현입니다. `setTicket` 으로 정한 티켓을 다음 `tick` 에 돌려주며, 정하지 않은 제공자는 실패합니다.
     */
    class SW_KIT_API FakePlatformLoginClient final : public IPlatformLoginClient
    {
    public:
        FakePlatformLoginClient();

        void   setTicket( string_view provider, string_view ticketText );
        uint64 beginLogin( string_view provider, MonotonicMilliseconds nowMs ) override;
        void   update( MonotonicMilliseconds nowMs ) override;
        int32  pollResults( vector<PlatformLoginClientResult>& outListResult ) override;

    private:
        struct Preset
        {
            string _provider{};
            string _ticketText{};
        };

        vector<Preset>                         _listPreset;
        vector<PlatformLoginClientResult>      _listPending;
        EventBuffer<PlatformLoginClientResult> _resultBuffer;
        IDAllocator<uint64>                    _requestIDAllocator;
    };
} // namespace sw
