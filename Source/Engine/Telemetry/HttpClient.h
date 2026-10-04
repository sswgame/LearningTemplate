/**
 * @file HttpClient.h
 * @brief 텔레메트리 · 크래시 보고가 바깥으로 보낼 때 쓰는 HTTP 창구 — 요청 · 응답 값과 인터페이스, 그리고 아무것도 보내지 않는 기본 구현입니다.
 * @details 이 저장소는 실제 네트워크 클라이언트를 싣지 않습니다. 업로더(`HttpTelemetryUploader` · `HttpCrashReportUploader`)는 요청을 만들어 이 창구에
 *          넘기기까지만 하고, 기본 창구(`NullHttpClient`)는 보내지 않고 거절합니다. 배포 게임은 플랫폼 HTTP(WinHTTP · libcurl · 콘솔 SDK)로
 *          `IHttpClient` 를 구현해 끼웁니다. 시험은 요청을 받아 적기만 하는 가짜를 끼웁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/pair.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief HTTP 요청 하나입니다. */
    struct HttpRequest
    {
        vector<pair<string, string>> _listHeader{};
        string                       _method{ "POST" };
        string                       _url{};
        string                       _body{};

        /** @brief 이름의 헤더 값입니다(대소문자 무시). 없으면 빈 글입니다. */
        SW_API string_view findHeader( string_view name ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief HTTP 응답입니다. `_status` 0 은 보내지 못함(연결 · 거절)입니다. */
    struct HttpResponse
    {
        string _error{};
        int32  _status{ 0 };

        bool isSuccess() const { return 200 <= _status && _status < 300; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class IHttpClient
     * @brief 요청 하나를 보내고 응답을 기다리는 창구입니다. 부르는 쪽이 정한 스레드에서 막고 돕니다(텔레메트리는 flush 하는 스레드, 크래시 보고는 보고 프로세스).
     */
    class SW_API IHttpClient
    {
    public:
        IHttpClient()                                = default;
        IHttpClient( const IHttpClient& )            = default;
        IHttpClient& operator=( const IHttpClient& ) = default;
        virtual ~IHttpClient()                       = default;

        virtual HttpResponse send( const HttpRequest& request ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NullHttpClient
     * @brief 아무것도 보내지 않고 거절합니다(`_status` 0, "network disabled"). 엔진의 기본 창구입니다.
     */
    class SW_API NullHttpClient final : public IHttpClient
    {
    public:
        HttpResponse send( const HttpRequest& request ) override;

        /** @brief 이 프로세스의 공용 인스턴스입니다(상태가 없다). */
        static NullHttpClient& get();
    };
} // namespace sw
