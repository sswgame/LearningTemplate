/**
 * @file CrashReportUploader.h
 * @brief 크래시 보고 묶음 하나를 바깥으로 보내는 창구 — 인터페이스, 보내지 않는 기본값, HTTP 창구로 multipart 업로드를 만드는 구현입니다.
 * @details 업로더는 동의를 모릅니다 — 동의(`telemetry.crashReports`)는 `CrashReportService` 가 지키고, 업로더는 보내기로 정해진(Queued) 묶음에만 불립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class IHttpClient;

    /** @brief 올리기 결과입니다. */
    enum class CrashReportUploadResult : uint8
    {
        Sent = 0, ///< 받는 쪽이 받았다
        Kept,     ///< 보내지 않고 둔다(기본 업로더)
        Failed    ///< 보내다 실패
    };

    SW_API const utf8* toString( CrashReportUploadResult result );

    /** @brief 올릴 묶음 — 묶음 폴더의 매니페스트와 파일들입니다. */
    struct CrashReportUploadBundle
    {
        vector<string> _listFilePath{}; ///< 덤프 · 컨텍스트 · 스택 · 빵부스러기 · 로그(매니페스트 제외)
        string         _folder{};
        string         _sessionId{};
        string         _manifest{}; ///< manifest.json 의 글
    };
} // namespace sw

namespace sw
{
    /**
     * @class ICrashReportUploader
     * @brief 묶음 하나를 보냅니다. 보고 프로세스(`App --crash-reporter=<폴더>`)에서 막고 돕니다 — 게임 프로세스를 세우지 않는다.
     */
    class SW_API ICrashReportUploader
    {
    public:
        ICrashReportUploader()                                         = default;
        ICrashReportUploader( const ICrashReportUploader& )            = default;
        ICrashReportUploader& operator=( const ICrashReportUploader& ) = default;
        virtual ~ICrashReportUploader()                                = default;

        virtual CrashReportUploadResult upload( const CrashReportUploadBundle& bundle ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NullCrashReportUploader
     * @brief 아무것도 보내지 않습니다(`Kept`). 기본 업로더입니다.
     */
    class SW_API NullCrashReportUploader final : public ICrashReportUploader
    {
    public:
        CrashReportUploadResult upload( const CrashReportUploadBundle& bundle ) override;

        static NullCrashReportUploader& get();
    };
} // namespace sw

namespace sw
{
    /**
     * @class HttpCrashReportUploader
     * @brief `POST <endpoint>` 한 번 — `multipart/form-data` 로 `manifest`(JSON)와 파일마다 한 부분. 덤프는 `upload_file_minidump` 이름으로 넣는다
     *        (Breakpad · Crashpad · Sentry 의 미니덤프 끝점이 받는 이름). 2xx 면 `Sent`.
     * @details 보내기는 `IHttpClient`(빌림)가 합니다. 엔진의 기본 창구(`NullHttpClient`)는 보내지 않습니다.
     */
    class SW_API HttpCrashReportUploader final : public ICrashReportUploader
    {
    public:
        static constexpr const utf8* kBoundary = "----SwCrashReportBoundary7d1f3a";

        HttpCrashReportUploader( IHttpClient& client, string_view endpoint );

        CrashReportUploadResult upload( const CrashReportUploadBundle& bundle ) override;

    private:
        string       _endpoint;
        IHttpClient* _pClient;
    };
} // namespace sw
