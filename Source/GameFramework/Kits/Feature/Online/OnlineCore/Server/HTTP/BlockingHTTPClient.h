/**
 * @file BlockingHTTPClient.h
 * @brief 엔진의 블로킹 HTTP API(`IHTTPClient`, 텔레메트리와 크래시 보고)를 GameFramework 의 HTTP 클라이언트(`HTTPClient`)로 보내는 어댑터입니다.
 * @details - Engine 은 GameFramework 를 모르므로 엔진은 `IHTTPClient` 계약만 갖고, 실제 전송은 이 어댑터를 게임이 끼워 넣어 얻습니다.
 *            게임은 `initialize` 한 어댑터를 업로더(`HTTPTelemetryUploader`, `HTTPCrashReportUploader`)에 넘깁니다.
 *          - `send` 는 부른 스레드에서 응답이 올 때까지 막습니다. 안에서 `HTTPClient` 를 직접 돌리므로(`update`, `pollResponses`)
 *            다른 스레드가 같은 `HTTPClient` 를 돌리면 안 됩니다. 그래서 플랫폼 전송 · TLS 컨텍스트와 함께(`OwnedHTTPClient`) 이 객체가 소유하고 밖에 내놓지 않습니다.
 *            제공자 클라이언트와 같은 묶음이라 `https://` 끝점은 신뢰 파일이 있어야 하고, 없으면 `registerHostTLS` 가 오류입니다(개발 인증서로 바꾸지 않는다).
 *            여러 스레드가 동시에 `send` 를 부르면 잠금 하나로 한 번에 하나씩 보냅니다.
 *          - 방법은 GET 과 POST 만 받습니다(`HTTPClient` 가 아는 방법). 다른 방법은 보내지 않고 `_status` 0 과 까닭으로 돌려줍니다.
 *          - 시한은 요청마다 `HTTPClientRequest::_timeoutMs` 기본값이며, 넘으면 `HTTPClient` 가 전송 실패로 끝냅니다.
 *          언리얼의 `FHttpModule` 위에 막는 호출을 감싸는 것과 같은 구조입니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/Clock.h"

#include "Engine/Telemetry/HTTPClient.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/HTTP/OwnedHTTPClient.h"

namespace sw
{
    /**
     * @class BlockingHTTPClient
     * @brief `IHTTPClient` 를 구현해 요청 하나를 `HTTPClient` 로 보내고 응답을 기다립니다.
     */
    class SW_KIT_SERVER_API BlockingHTTPClient final : public IHTTPClient
    {
    public:
        BlockingHTTPClient();
        ~BlockingHTTPClient() override;

        BlockingHTTPClient( const BlockingHTTPClient& )            = delete;
        BlockingHTTPClient& operator=( const BlockingHTTPClient& ) = delete;

        /** @brief 플랫폼 전송으로 안의 `HTTPClient` 를 띄웁니다. 실패하면 false 입니다. */
        [[nodiscard]] bool initialize();
        void               shutdown();

        /** @brief @p url 이 `https://` 면 그 호스트의 TLS 컨텍스트를 @p trustFile(CA 번들 PEM)로 만들어 등록합니다(`OwnedHTTPClient::registerHostTLS`). 실패하면 false 와 @p outError 입니다. */
        [[nodiscard]] bool registerHostTLS( string_view url, string_view trustFile, string_view trustOption, string& outError );

        /** @brief 요청 하나를 보내고 응답까지 막습니다. 보내지 못하면 `_status` 0 과 `_error` 입니다. */
        HTTPResponse send( const HTTPRequest& request ) override;

        /** @brief 엔진 요청을 GameFramework 요청으로 바꿉니다. 방법이 GET 과 POST 가 아니면 false 입니다. 테스트가 직접 부릅니다. */
        [[nodiscard]] static bool makeClientRequest( const HTTPRequest& request, HTTPClientRequest& outRequest );

    private:
        mutex                   _sendMutex; ///< `send` 를 한 번에 하나로 묶는다
        SystemMonotonicClock    _clock;     ///< 시한을 재는 시계. `_client` 가 빌리므로 먼저 선언한다
        OwnedHTTPClient _client SW_GUARDED_BY( _sendMutex );
    };
} // namespace sw
