/**
 * @file TelemetryUploader.h
 * @brief 닫힌 스풀 파일(JSON lines) 하나를 바깥으로 보내는 창구 — 인터페이스, 아무것도 보내지 않는 기본값, HTTP 창구로 보내는 구현입니다.
 * @details 언리얼 `IAnalyticsProvider` 의 자리입니다(공급자를 갈아 끼운다). 업로더는 동의를 모릅니다 — 동의 · 표본 · 묶음 · 상한은
 *          `TelemetryService` 가 지키고, 업로더는 동의가 켜져 있을 때만 불립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class IHTTPClient;

    /** @brief 올리기 결과입니다. */
    enum class TelemetryUploadResult : uint8
    {
        Sent = 0, ///< 받는 쪽이 받았다 — 서비스가 파일을 지운다
        Kept,     ///< 보내지 않고 둔다(기본 업로더) — 파일은 상한 안에서 남는다
        Failed    ///< 보내다 실패 — 다음 flush 에 다시
    };

    SW_API const utf8* toString( TelemetryUploadResult result );

    /** @brief 올릴 묶음 하나 — 닫힌 스풀 파일 하나입니다. 첫 줄이 문맥 줄(세션 · 빌드 · 플랫폼)입니다. */
    struct TelemetryUploadBatch
    {
        string _filePath{};
        string _content{};
        string _sessionID{};
        uint32 _eventCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ITelemetryUploader
     * @brief 묶음 하나를 보냅니다. flush 를 부른 스레드에서 막고 돕니다.
     */
    class SW_API ITelemetryUploader
    {
    public:
        ITelemetryUploader()                                       = default;
        ITelemetryUploader( const ITelemetryUploader& )            = default;
        ITelemetryUploader& operator=( const ITelemetryUploader& ) = default;
        virtual ~ITelemetryUploader()                              = default;

        virtual TelemetryUploadResult upload( const TelemetryUploadBatch& batch ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NullTelemetryUploader
     * @brief 아무것도 보내지 않습니다(`Kept`). 기본 업로더입니다 — 동의해도 기계 밖으로 나가는 것은 없습니다.
     */
    class SW_API NullTelemetryUploader final : public ITelemetryUploader
    {
    public:
        TelemetryUploadResult upload( const TelemetryUploadBatch& batch ) override;

        /** @brief 이 프로세스의 공용 인스턴스입니다(상태가 없다). */
        static NullTelemetryUploader& get();
    };
} // namespace sw

namespace sw
{
    /**
     * @class HTTPTelemetryUploader
     * @brief 묶음을 `POST <endpoint>` 한 번으로 보냅니다 — 본문은 파일 그대로(`application/x-ndjson`), 헤더에 세션 · 사건 수 · API 키.
     * @details 보내기는 `IHTTPClient`(빌림)가 합니다. 엔진의 기본 창구(`NullHTTPClient`)는 보내지 않으므로 이 업로더도 실제로는 아무 데도 닿지 않습니다.
     *          2xx 면 `Sent`, 그 밖은 `Failed` 입니다.
     */
    class SW_API HTTPTelemetryUploader final : public ITelemetryUploader
    {
    public:
        HTTPTelemetryUploader( IHTTPClient& client, string_view endpoint, string_view apiKey );

        TelemetryUploadResult upload( const TelemetryUploadBatch& batch ) override;

    private:
        string       _endpoint;
        string       _apiKey;
        IHTTPClient* _pClient;
    };
} // namespace sw
