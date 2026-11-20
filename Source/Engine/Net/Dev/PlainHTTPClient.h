/**
 * @file PlainHTTPClient.h
 * @brief 개발 전용 평문 HTTP/1.1 GET 클라이언트입니다(TLS 없음). 장치가 PC 의 팩 폴더를 받는 Fileserve 전송(`FileServeContentSource`)이 쓴다.
 * @details 설계: DesignBacklog "Fileserve" 결정 3. 콘텐츠 소스는 엔진이 첫 팩을 읽기 전(`android_main`)에 걸어야 하는데 실제 HTTP 클라이언트(GameFramework `BlockingHTTPClient`)는
 *          그때 없다 — 그래서 엔진 층에 막는 GET 하나만 하는 작은 클라이언트를 둔다(`PlatformSocketUtil` 의 TCP 연산 위). 요청마다 연결 하나(`Connection: close`)라 상태가 없고
 *          여러 스레드가 함께 불러도 된다. 리다이렉트 · 압축 · 1xx · keep-alive 는 하지 않는다.
 *          시한은 잇기 · 보내기 · 받기를 덮지만 호스트 이름 해석(`getaddrinfo` — OS 해석기 시한)은 덮지 않는다 — `-fileserve` 에는 IP 리터럴(`adb reverse` 면 127.0.0.1)을 준다.
 *          응답은 믿지 않는다: 상태 줄 · 헤더 문법, Content-Length(숫자만 · 겹치면 같아야) · chunked(16 진 크기 · CRLF), 둘 다 오면 거절(요청 밀반입 규칙 — RFC 9112 6.3),
 *          머리말 · 본문 · 원시 바이트 상한, 요청 시한 — 넘치면 그 요청은 실패(`_status` 0 + `_error`)다. 한도는 `EngineConfig::_fileServe`(`FileServeConfig`).
 *          `Dev/` 폴더라 배포 구성은 짓지 않는다(`Config/Rules/Layers/DevFolders.json`). 언리얼 쿡 데이터 네트워크 파일(`FNetworkPlatformFile`)의 개발 전용 전송 자리다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Net/HTTPClient.h"

namespace sw
{
    /** @brief 받은 바이트로 응답을 풀어 본 결과입니다. */
    enum class HTTPResponseParseStatus : uint8
    {
        Incomplete, ///< 더 받아야 한다
        Complete,   ///< 응답 하나가 끝났다(`_status` · `_body` 가 찼다)
        Rejected,   ///< 문법 · 한도 위반이거나 끝나기 전에 닫혔다(`_error` 가 이유다)
    };

    /**
     * @class PlainHTTPClient
     * @brief 평문 HTTP/1.1 GET 하나를 막고 보내는 `IHTTPClient` 입니다. 파일 머리말 참고.
     */
    class SW_API PlainHTTPClient final : public IHTTPClient
    {
    public:
        /** @param settings 응답 한도 · 시한(복사해 둔다). */
        explicit PlainHTTPClient( const FileServeConfig& settings );

        /** @brief GET 만 받는다(그 밖은 `_status` 0). URL 은 `http://<호스트>[:<포트>][/<경로>[?<질의>]]` 이다. */
        HTTPResponse send( const HTTPRequest& request ) override;

        /**
         * @brief URL 을 연결 대상(`호스트[:포트]`)과 요청 대상(`/경로?질의`)으로 나눕니다.
         * @return `http://` 가 아니거나, 출력 가능한 ASCII(0x21 ~ 0x7E) 밖 글자 · 사용자 정보(`@`) · 조각(`#`)이 있거나 호스트가 비면 false 입니다 — 요청 줄에 CR · LF · 공백이 들어가지 않는다.
         */
        [[nodiscard]] static bool splitURL( string_view url, string& outHostAndPort, string& outTarget );
        /** @brief 서버 뿌리 주소(`http://<호스트>[:<포트>]` — 경로 · 질의 · 끝 `/` 없음)이면 true 입니다. 끝점 경로(`/stat` …)를 이 뒤에 붙인다(`-fileserve`). */
        [[nodiscard]] static bool isServerRootURL( string_view url );

        /**
         * @brief 지금까지 받은 바이트 @p bytes 로 응답 하나를 풉니다(받을 때마다 처음부터 다시 부른다 — 상태를 들지 않는다).
         * @param bStreamEnded 저쪽이 연결을 닫았으면 true 다 — 길이 없는 본문은 닫힘이 끝이고, 그 밖은 끝나기 전 닫힘이 거절이다.
         * @param maxHeaderBytes 상태 줄 + 헤더(빈 줄까지) 상한이고, chunked 의 구간 머리 줄 하나 · 트레일러에도 같은 상한이다.
         * @param maxBodyBytes 본문 상한이다. 원시 바이트(머리말 + chunked 틀 포함)는 `maxHeaderBytes + 2 × maxBodyBytes` 까지다(작은 구간을 잔뜩 보내 메모리를 채우지 못하게).
         */
        [[nodiscard]] static HTTPResponseParseStatus parseResponse( string_view bytes, bool bStreamEnded, uint32 maxHeaderBytes, uint32 maxBodyBytes, HTTPResponse& outResponse );

    private:
        FileServeConfig _settings;
    };
} // namespace sw
