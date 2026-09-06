/**
 * @file FrameProfiler.h
 * @brief 프레임 구간별 CPU 시간·카운터를 누적해 통계로 보고합니다.
 *
 * [왜 필요한가]
 * 지금까지의 최적화는 전부 "코드를 읽어서 낭비라고 판정한 것"을 없앤 것이다. 그게 프레임
 * 시간의 1% 였는지 30% 였는지는 아무도 모른다. 무엇이 실제로 비싼지 모르면, 다음 최적화는
 * 근거 없이 고르게 되고 되돌리기도 어렵다.
 *
 * ScopeCpuTimer 는 스코프마다 로그를 한 줄씩 남기므로 프레임 루프에 넣을 수 없다(초당 수천 줄).
 * 여기서는 **누적만** 하고, 보고는 요청할 때 한 번 한다.
 *
 * [비용]
 * 꺼져 있으면 스코프 진입/이탈이 bool 검사 하나다. 켜져 있으면 시계 두 번 + relaxed
 * fetch_add 두 번. 패스·프레임 단위로만 걸고 드로우 단위에는 걸지 말 것 — 측정이 측정 대상을
 * 바꾸면 의미가 없다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

namespace sw
{
    /**
     * @class FrameProfiler
     * @brief 이름 붙은 구간의 프레임당 시간·호출수를 모읍니다. 프로세스 전역 싱글턴입니다.
     * @details 병렬 패스 기록이 여러 스레드에서 동시에 같은 구간을 누적하므로 relaxed 원자 덧셈을
     *          쓴다. 구간은 이름당 슬롯 하나로 고정되고, 슬롯 번호는 매크로가 static 으로 캐시한다.
     */
    class SW_API FrameProfiler
    {
    public:
        /** @brief 등록 가능한 구간 수. 넘으면 새 구간은 조용히 무시됩니다(측정이 실행을 막으면 안 된다). */
        static constexpr uint32 kMaxScope = 64;
        /** @brief 슬롯을 못 받았을 때의 값. */
        static constexpr uint32 kInvalidSlot = 0xFFFFFFFFu;

        /** @brief 프로세스 전역 인스턴스입니다. */
        static FrameProfiler& get();

        /**
         * @brief 이름을 슬롯에 등록하고 번호를 돌려줍니다. 같은 이름이면 같은 번호입니다.
         * @note 최초 1회만 부르도록 매크로가 static 지역변수로 캐시합니다.
         */
        uint32 registerScope( const utf8* pName );

        /** @brief 구간에 경과 나노초와 호출 1회를 더합니다. 스레드 안전합니다. */
        void addSample( uint32 slot, uint64 nanos );
        /** @brief 구간에 임의의 수를 더합니다(드로우 수 등). 시간이 아닌 카운터입니다. */
        void addCount( uint32 slot, uint64 count );

        /** @brief 이번 프레임 누적을 비웁니다. */
        void beginFrame();
        /** @brief 이번 프레임 누적을 통계로 접습니다. */
        void endFrame();

        /** @brief 켜져 있으면 true. 꺼져 있으면 스코프는 시계를 읽지 않습니다. */
        bool isEnabled() const { return _bEnabled.load( std::memory_order_relaxed ); }
        /** @brief 계측을 켜거나 끕니다. */
        void setEnabled( bool bEnabled ) { _bEnabled.store( bEnabled, std::memory_order_relaxed ); }

        /** @brief endFrame 이 불린 횟수입니다. */
        uint64 getFrameCount() const { return _frameCount.load( std::memory_order_relaxed ); }

        /** @brief 모은 통계를 로그로 남깁니다. */
        void report( const utf8* pTitle ) const;
        /** @brief 통계와 프레임 수를 비웁니다. 워밍업 구간을 버릴 때 씁니다. */
        void reset();

    private:
        FrameProfiler() = default;

        /** @brief 구간 하나의 누적치. */
        struct Scope
        {
            const utf8*    _pName{ nullptr };
            atomic<uint64> _frameNanos{ 0 }; ///< 이번 프레임 누적 (endFrame 에서 비움)
            atomic<uint64> _frameCalls{ 0 }; ///< 이번 프레임 호출/카운트
            uint64         _totalNanos{ 0 }; ///< 전체 프레임 누적
            uint64         _totalCalls{ 0 };
            uint64         _minNanos{ 0 }; ///< 프레임 단위 최소/최대
            uint64         _maxNanos{ 0 };
            uint64         _sampledFrames{ 0 };
        };

        Scope          _arrScope[kMaxScope];
        atomic<uint32> _scopeCount{ 0 };
        atomic<uint64> _frameCount{ 0 };
        atomic<bool>   _bEnabled{ false };
    };

    /**
     * @class ScopedFrameProfile
     * @brief 스코프 경과를 FrameProfiler 에 더하는 RAII 도우미입니다.
     */
    class SW_API ScopedFrameProfile final
    {
    public:
        /** @brief 계측이 켜져 있을 때만 시작 시각을 읽습니다. */
        explicit ScopedFrameProfile( uint32 slot ) noexcept;
        /** @brief 경과를 슬롯에 더합니다. */
        ~ScopedFrameProfile() noexcept;

        ScopedFrameProfile( const ScopedFrameProfile& )            = delete;
        ScopedFrameProfile& operator=( const ScopedFrameProfile& ) = delete;

    private:
        uint64 _startNanos;
        uint32 _slot;
    };
} // namespace sw

/** @brief 두 토큰을 붙입니다(매크로 확장 후). */
#define SW_PROFILE_CONCAT_INNER( a, b ) a##b
/** @brief 두 토큰을 붙입니다. */
#define SW_PROFILE_CONCAT( a, b ) SW_PROFILE_CONCAT_INNER( a, b )

/**
 * @brief 이 스코프의 CPU 시간을 name 구간에 누적합니다.
 * @details 슬롯 번호는 함수 지역 static 으로 한 번만 받는다(C++11 이후 스레드 안전 초기화).
 */
#define SW_PROFILE_SCOPE( name )                                            \
    static const uint32 SW_PROFILE_CONCAT( swProfileSlot_, __LINE__ ) =     \
        ::sw::FrameProfiler::get().registerScope( name );                   \
    ::sw::ScopedFrameProfile SW_PROFILE_CONCAT( swProfileScope_, __LINE__ ) \
    {                                                                       \
        SW_PROFILE_CONCAT( swProfileSlot_, __LINE__ )                       \
    }

/** @brief name 카운터에 value 를 더합니다(드로우 수 등). 시간이 아닙니다. */
#define SW_PROFILE_COUNT( name, value )                                                      \
    do                                                                                       \
    {                                                                                        \
        static const uint32 SW_PROFILE_CONCAT( swProfileCount_, __LINE__ ) =                 \
            ::sw::FrameProfiler::get().registerScope( name );                                \
        ::sw::FrameProfiler::get().addCount( SW_PROFILE_CONCAT( swProfileCount_, __LINE__ ), \
                                             static_cast<uint64>( value ) );                 \
    } while ( false )
