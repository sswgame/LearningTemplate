#include "pch.h"

#include "Core/Common/Macros.h"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

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

        AssertDialogState& getAssertDialogState()
        {
            static AssertDialogState s_state;
            return s_state;
        }

        /** @brief 같은 자리인가 — 파일 리터럴 주소는 번역 단위마다 다를 수 있어 글로 견준다. */
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

    void beginAssertCapture() noexcept { s_assertCaptureDepth.fetch_add( 1, std::memory_order_acq_rel ); }

    void endAssertCapture() noexcept { s_assertCaptureDepth.fetch_sub( 1, std::memory_order_acq_rel ); }

    uint32 getCapturedAssertCount() noexcept { return s_capturedAssertCount.load( std::memory_order_acquire ); }

    void setAssertDialog( AssertDialogFunc pfnDialog ) noexcept
    {
        AssertDialogState&                state = getAssertDialogState();
        const std::lock_guard<std::mutex> lock{ state._mutex };
        state._pfnDialog.store( pfnDialog, std::memory_order_release );
        state._ignoredSiteCount = 0;
    }

    bool hasAssertDialog() noexcept { return getAssertDialogState()._pfnDialog.load( std::memory_order_acquire ) != nullptr; }

    bool shouldBreakOnAssert( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, int32 line ) noexcept
    {
        AssertDialogState&                state = getAssertDialogState();
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
