#include "pch.h"

#include "Core/Common/Macros.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace sw::internal
{
    namespace
    {
        /** @brief 로그가 설치한 보고자입니다. 상수 초기화라 정적 초기화 순서를 타지 않습니다. */
        std::atomic<LowLevelReporterFunc> s_pfnLowLevelReporter{ nullptr };

        struct LowLevelReportInternal
        {
            /** @brief 글이 버퍼를 넘쳤을 때 끝에 붙이는 표시입니다(널 포함 크기가 곧 차지하는 칸 수). */
            static constexpr utf8 kTruncatedSuffix[] = "...[truncated]";
            /** @brief 단언 보고 글의 버퍼 크기입니다 — 식 · 메시지(`kLowLevelMessageCapacity`) · 파일 · 줄을 담는다. */
            static constexpr size_t kAssertTextCapacity = 1024;

            /** @brief stderr 에 한 줄을 쓰고 비웁니다. 낮은 층 보고가 stderr 로 가는 유일한 위치입니다. */
            static void writeStderr( LowLevelReportLevel level, const utf8* pFile, int32 line, const utf8* pMessage ) noexcept
            {
                std::fprintf( stderr, "[%s] %s:%d: %s\n", level == LowLevelReportLevel::Error ? "Error" : "Warning", pFile, line, pMessage );
                std::fflush( stderr );
            }
        };
    } // namespace

    LowLevelReporterFunc setLowLevelReporter( LowLevelReporterFunc pfnReporter ) noexcept
    {
        return s_pfnLowLevelReporter.exchange( pfnReporter, std::memory_order_acq_rel );
    }

    void formatLowLevelMessage( utf8* pBuffer, size_t capacity, const utf8* pFormat, ... ) noexcept
    {
        va_list arguments;
        va_start( arguments, pFormat );
        const int32 written = static_cast<int32>( std::vsnprintf( pBuffer, capacity, pFormat, arguments ) );
        va_end( arguments );

        if ( written < 0 )
        {
            // 서식 자체가 어긋났다(인코딩 오류 등) — 빈 글로 두면 보고가 의미 없어지므로 사실을 남긴다.
            std::snprintf( pBuffer, capacity, "(low-level message could not be formatted)" );
            return;
        }
        if ( static_cast<size_t>( written ) >= capacity && capacity >= sizeof( LowLevelReportInternal::kTruncatedSuffix ) )
            std::memcpy( pBuffer + capacity - sizeof( LowLevelReportInternal::kTruncatedSuffix ), LowLevelReportInternal::kTruncatedSuffix, sizeof( LowLevelReportInternal::kTruncatedSuffix ) );
    }

    void reportLowLevel( LowLevelReportLevel level, const utf8* pFile, int32 line, const utf8* pMessage ) noexcept
    {
        // Error 는 보고자가 있어도 먼저 동기로 쓴다 — 비동기 로거의 큐는 크래시 직전 메시지를 잃는다(SW_ASSERT 가 멈추기 전에 쓰는 것과 같다).
        if ( level == LowLevelReportLevel::Error )
            LowLevelReportInternal::writeStderr( level, pFile, line, pMessage );

        const LowLevelReporterFunc pfnReporter = s_pfnLowLevelReporter.load( std::memory_order_acquire );
        const bool                 bReported   = pfnReporter != nullptr && pfnReporter( level, pFile, line, pMessage );
        // 로거가 서기 전이거나 로거가 없는 도구이거나 싱크가 없다 — 경고를 잃지 않게 stderr 에 남긴다(Error 는 위에서 이미 썼다).
        if ( bReported == false && level != LowLevelReportLevel::Error )
            LowLevelReportInternal::writeStderr( level, pFile, line, pMessage );
    }

    void reportLowLevelAssert( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line ) noexcept
    {
        utf8 arrText[LowLevelReportInternal::kAssertTextCapacity];
        std::snprintf( arrText, sizeof( arrText ), "ASSERT failed\nExpression : %s\nMessage    : %s\nFileName   : %s\nLine       : %d", pExpression, pMessage, pFile, line );
        reportLowLevel( LowLevelReportLevel::Error, pFile, line, arrText );
    }
} // namespace sw::internal

#if defined( SW_DEBUG )
namespace sw::internal
{
    namespace
    {
        /** @brief 걸린 가로채기 수 — 0 이면 단언이 멈춘다. */
        std::atomic<int32> s_assertCaptureDepth{ 0 };
        /** @brief 지금까지 가로챈 단언 수. */
        std::atomic<uint32> s_capturedAssertCount{ 0 };

        /** @brief "이 자리 늘 무시" 로 고른 자리(파일 · 줄)입니다. 넘치면 가장 오래된 자리를 덮는다. */
        struct IgnoredAssertSite
        {
            const utf8* _pFile{ nullptr };
            int32       _line{ 0 };
        };

        /** @brief 대화상자 상태입니다. 잠금은 대화상자가 떠 있는 동안 다른 스레드의 단언을 줄 세운다. */
        struct AssertDialogState
        {
            static constexpr uint32 kMaxIgnoredSiteCount = 256;

            std::mutex                                          _mutex;
            std::array<IgnoredAssertSite, kMaxIgnoredSiteCount> _arrIgnoredSite{};
            uint32                                              _ignoredSiteCount{ 0 };
            std::atomic<AssertDialogFunc>                       _pfnDialog{ nullptr };
        };

        /**
         * @brief 단언 대화상자의 설치 칸과 "항상 무시" 자리입니다. 프로세스에 하나이고 `setAssertDialog` 가 설치 · 해제(nullptr)합니다.
         * @details 단언은 서비스 범위보다 아래 층이라 범위에 맡길 수 없고, 단언 안에서 할당하면 안 되어 지연 생성도 못 한다. 그래서 위의 `s_pfnLowLevelReporter` 와
         *          같은 모양 — 이름공간의 이 한 변수(상수 초기화라 정적 초기화 순서를 타지 않는다)가 소유자이고, 접근자 함수를 두지 않는다.
         */
        AssertDialogState s_dialogState;

        /** @brief 같은 자리인가 — 파일 리터럴 주소는 번역 단위마다 다를 수 있어 글로 비교한다. */
        bool isSameSite( const IgnoredAssertSite& site, const utf8* pFile, int32 line )
        {
            return site._line == line && site._pFile != nullptr && pFile != nullptr && std::strcmp( site._pFile, pFile ) == 0;
        }
    } // namespace

    bool tryCaptureAssert() noexcept
    {
        if ( s_assertCaptureDepth.load( std::memory_order_acquire ) <= 0 )
            return false;
        s_capturedAssertCount.fetch_add( 1, std::memory_order_acq_rel );
        return true;
    }

    void beginAssertCapture() noexcept
    {
        s_assertCaptureDepth.fetch_add( 1, std::memory_order_acq_rel );
    }

    void endAssertCapture() noexcept
    {
        s_assertCaptureDepth.fetch_sub( 1, std::memory_order_acq_rel );
    }

    uint32 getCapturedAssertCount() noexcept
    {
        return s_capturedAssertCount.load( std::memory_order_acquire );
    }

    void setAssertDialog( AssertDialogFunc pfnDialog ) noexcept
    {
        AssertDialogState&                state = s_dialogState;
        const std::lock_guard<std::mutex> lock{ state._mutex };
        state._pfnDialog.store( pfnDialog, std::memory_order_release );
        state._ignoredSiteCount = 0;
    }

    bool hasAssertDialog() noexcept
    {
        return s_dialogState._pfnDialog.load( std::memory_order_acquire ) != nullptr;
    }

    bool shouldBreakOnAssert( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line ) noexcept
    {
        AssertDialogState&                state = s_dialogState;
        const std::lock_guard<std::mutex> lock{ state._mutex };
        const AssertDialogFunc            pfnDialog = state._pfnDialog.load( std::memory_order_acquire );
        if ( pfnDialog == nullptr )
            return true;
        const uint32 storedCount = state._ignoredSiteCount < AssertDialogState::kMaxIgnoredSiteCount ? state._ignoredSiteCount : AssertDialogState::kMaxIgnoredSiteCount;
        for ( uint32 index = 0; index < storedCount; ++index )
        {
            if ( isSameSite( state._arrIgnoredSite[index], pFile, line ) )
                return false;
        }

        switch ( pfnDialog( pExpression, pMessage, pFile, line ) )
        {
            case AssertAction::Break:
            {
                return true;
            }
            case AssertAction::IgnoreOnce:
            {
                return false;
            }
            case AssertAction::IgnoreAlways:
            {
                IgnoredAssertSite& site = state._arrIgnoredSite[state._ignoredSiteCount % AssertDialogState::kMaxIgnoredSiteCount];
                site._pFile             = pFile;
                site._line              = line;
                ++state._ignoredSiteCount;
                return false;
            }
        }
        return true;
    }
} // namespace sw::internal
#endif

namespace sw::internal
{
    void reportMissingRequiredService( const utf8* pLookup ) noexcept
    {
        utf8 arrText[kLowLevelMessageCapacity];
        formatLowLevelMessage( arrText, kLowLevelMessageCapacity, "Required lookup is missing: %s", pLookup );
        reportLowLevel( LowLevelReportLevel::Error, __FILE__, static_cast<int32>( __LINE__ ), arrText ); // Error 는 stderr 에 먼저 동기로 쓴다 — 멈추기 전에 남는다
#if defined( SW_DEBUG )
        if ( tryCaptureAssert() )
            return; // 단언 가로채기를 건 테스트(`ScopedAssertCapture`)만 멈추지 않고 개수를 센다 — 가로채기는 Debug 에만 있다
#endif
        std::abort();
    }
} // namespace sw::internal
