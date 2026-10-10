/**
 * @file PlatformLoginClient.h
 * @brief 외부 로그인의 클라이언트 쪽 — "구글로 로그인" 을 누르면 제공자 표(ID 토큰 · 액세스 토큰)를 얻어 계정 서버의 외부 로그인(`kPlatformLogin`)에 넘깁니다.
 * @details - 모바일은 플랫폼 SDK(모바일 빌드가 생기면), PC 는 **시스템 브라우저 + 루프백 리다이렉트 + PKCE**(RFC 8252 — 임베드 웹뷰 금지): `LoopbackPkceLoginClient`.
 *          - 시스템 브라우저 열기는 `IExternalBrowser`(게임이 OS 로 구현 — ShellExecute · xdg-open, 시험은 가짜). 시험 · 개발은 `FakePlatformLoginClient`.
 *          - OIDC 제공자의 표는 `ID 토큰|nonce` — 서버의 `OidcLoginProvider` 가 nonce 를 맞춰 본다(재사용 막기).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 끝난 외부 로그인 하나입니다. */
    struct PlatformLoginClientResult
    {
        vector<uint8> _ticket{}; ///< 성공 — 계정 서버의 외부 로그인에 그대로 넘긴다
        string        _provider{};
        string        _failureText{}; ///< 실패 — 사람이 읽는 까닭(로그 · 화면)
        uint64        _requestID{ 0 };
        uint8         _bSucceeded{ SW_FALSE };
        uint8         _bCancelled{ SW_FALSE }; ///< 사용자가 거절했거나(`error=access_denied`) 시한이 지났다
    };
} // namespace sw

namespace sw
{
    /**
     * @class IPlatformLoginClient
     * @brief 외부 로그인 표를 얻는 쪽입니다. 모든 호출은 한 스레드(게임 스레드)에서.
     */
    class SW_GF_API IPlatformLoginClient
    {
    public:
        IPlatformLoginClient()          = default;
        virtual ~IPlatformLoginClient() = default;

        IPlatformLoginClient( const IPlatformLoginClient& )            = delete;
        IPlatformLoginClient& operator=( const IPlatformLoginClient& ) = delete;

        /** @brief @p provider 로 로그인을 시작합니다. 0 이 아닌 요청 id(결과의 `_requestID`) — 결과는 반드시 한 번(실패 · 취소도). */
        virtual uint64 beginLogin( string_view provider, int64 nowMs )                 = 0;
        virtual void   tick( int64 nowMs )                                             = 0;
        virtual int32  pollResults( vector<PlatformLoginClientResult>& outListResult ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class IExternalBrowser
     * @brief 시스템 기본 브라우저로 주소를 엽니다(사용자의 저장된 로그인 · 비밀번호 관리자 · 2 단계 인증을 그대로 쓴다).
     */
    class SW_GF_API IExternalBrowser
    {
    public:
        IExternalBrowser()          = default;
        virtual ~IExternalBrowser() = default;

        IExternalBrowser( const IExternalBrowser& )            = delete;
        IExternalBrowser& operator=( const IExternalBrowser& ) = delete;

        [[nodiscard]] virtual bool openURL( string_view url ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class FakePlatformLoginClient
     * @brief 시험 · 개발용 — `setTicket` 으로 정한 표를 다음 `tick` 에 돌려줍니다(정하지 않은 제공자는 실패).
     */
    class SW_GF_API FakePlatformLoginClient final : public IPlatformLoginClient
    {
    public:
        FakePlatformLoginClient();

        void   setTicket( string_view provider, string_view ticketText );
        uint64 beginLogin( string_view provider, int64 nowMs ) override;
        void   tick( int64 nowMs ) override;
        int32  pollResults( vector<PlatformLoginClientResult>& outListResult ) override;

    private:
        struct Preset
        {
            string _provider{};
            string _ticketText{};
        };

        vector<Preset>                    _listPreset;
        vector<PlatformLoginClientResult> _listPending;
        vector<PlatformLoginClientResult> _listDone;
        uint64                            _nextRequestID;
    };
} // namespace sw
