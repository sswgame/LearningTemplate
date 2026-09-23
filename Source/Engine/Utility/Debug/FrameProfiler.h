/**
 * @file FrameProfiler.h
 * @brief 프레임 구간별 CPU 시간 · 카운터를 누적해 통계로 보고합니다.
 *
 * [왜 필요한가]
 * 이것이 생기기 전의 최적화는 모두 "코드를 읽어서 낭비라고 판정한 것" 을 없앤 것이었습니다. 그것이 프레임
 * 시간의 1% 였는지 30% 였는지는 아무도 몰랐습니다. 무엇이 실제로 비싼지 모르면 다음 최적화는
 * 근거 없이 고르게 되고 되돌리기도 어렵습니다.
 *
 * ScopeCpuTimer 는 스코프마다 로그를 한 줄씩 남기므로 프레임 루프에 넣을 수 없습니다(초당 수천 줄).
 * 여기서는 **누적만** 하고, 보고는 요청할 때 한 번 합니다.
 *
 * [비용]
 * 꺼져 있으면 스코프 진입 · 이탈이 bool 검사 하나입니다. 켜져 있으면 시계 두 번 + relaxed
 * fetch_add 두 번입니다. 패스 · 프레임 단위로만 걸고 드로우 단위에는 걸지 마십시오. 측정이 측정 대상을
 * 바꾸면 의미가 없습니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"

namespace sw
{
    /**
     * @class FrameProfiler
     * @brief 이름 붙은 구간의 프레임당 시간 · 호출 수를 모읍니다.
     * @details 병렬 패스 기록이 여러 스레드에서 동시에 같은 구간을 누적하므로 relaxed 원자 덧셈을
     *          씁니다. 구간은 이름당 슬롯 하나로 고정되고, 슬롯 번호는 매크로가 static 으로 캐시합니다.
     *
     *          소유는 `EngineLoop` 이고 조회는 `engine::getFrameProfiler()` 입니다. 예전에는
     *          `get()` 이 함수 지역 static 을 들고 있는 싱글턴이었는데, 그러면 수명이 아무에게도
     *          속하지 않아 종료 순서를 정할 수 없었습니다.
     */
    class SW_API FrameProfiler
    {
    public:
        /** @brief 등록할 수 있는 구간 수입니다. 넘으면 새 구간은 조용히 무시됩니다(측정이 실행을 막으면 안 되기 때문입니다). */
        static constexpr uint32 kMaxScope = 64;
        /** @brief 슬롯을 받지 못했을 때의 값입니다. */
        static constexpr uint32 kInvalidSlot = 0xFFFFFFFFu;
        /** @brief 분포 히스토그램에서 옥타브(2배 구간) 하나를 나누는 비트 수입니다. 3 이면 옥타브당 8 칸, 해상도는 약 9% 입니다. */
        static constexpr uint32 kSubBucketBit = 3;
        /** @brief 옥타브당 칸 수입니다. */
        static constexpr uint32 kSubBucketCount = 1u << kSubBucketBit;
        /** @brief 옥타브 안의 칸 번호를 뽑는 마스크입니다. */
        static constexpr uint32 kSubBucketMask = kSubBucketCount - 1;
        /** @brief 히스토그램 칸 수입니다. 64 옥타브(uint64 전체) × 옥타브당 칸 수. */
        static constexpr uint32 kBucketCount = 64u << kSubBucketBit;
        /** @brief 백분위의 최댓값입니다. */
        static constexpr uint32 kPercentMax = 100;

        FrameProfiler() = default;

        /**
         * @brief 이름을 슬롯에 등록하고 번호를 반환합니다. 같은 이름이면 같은 번호입니다.
         * @note 처음 한 번만 부르도록 매크로가 static 지역 변수에 캐시합니다.
         */
        uint32 registerScope( const utf8* pName );

        /** @brief 구간에 경과 나노초와 호출 1회를 더합니다. 스레드 안전합니다. */
        void addSample( uint32 slot, uint64 nanos );
        /** @brief 구간에 임의의 수를 더합니다(드로우 수 등). 시간이 아닌 카운터입니다. */
        void addCount( uint32 slot, uint64 count );

        /** @brief 측정 창이 열렸음을 표시합니다. 누적은 지우지 않습니다(endFrame 이 읽으면서 비웁니다). */
        void beginFrame();
        /** @brief 이번 프레임 누적을 읽어 비우면서 통계에 합칩니다. */
        void endFrame();

        /** @brief 켜져 있으면 true 입니다. 꺼져 있으면 스코프는 시계를 읽지 않습니다. */
        bool isEnabled() const { return _bEnabled.load( std::memory_order_relaxed ); }
        /** @brief 계측을 켜거나 끕니다. */
        void setEnabled( bool bEnabled ) { _bEnabled.store( bEnabled, std::memory_order_relaxed ); }

        /** @brief endFrame 이 불린 횟수입니다. */
        uint64 getFrameCount() const { return _frameCount.load( std::memory_order_relaxed ); }

        /**
         * @brief 구간의 프레임 값 분포에서 @p percent 백분위(나노초)를 반환합니다.
         * @details 평균 · 최소 · 최대만으로는 히치가 평균에 묻힙니다. `RT.BeginFrame` 평균 400 us 는 600 프레임
         *          중 40 개의 1~18 ms 였습니다. 값은 해당 칸의 **아래 끝**이라 표본보다 작거나 같고 한 칸(약 12%)
         *          안입니다. 표본이 없으면 0 입니다.
         */
        uint64 getPercentileNanos( uint32 slot, uint32 percent ) const;

        /** @brief 모은 통계를 로그로 남깁니다. avg · p50 · p99 · min · max · per_frame 순입니다. */
        void report( const utf8* pTitle ) const;
        /** @brief 통계와 프레임 수를 비웁니다. 워밍업 구간을 버릴 때 씁니다. */
        void reset();

    private:
        /** @brief 구간 하나의 누적치입니다. */
        struct Scope
        {
            /**
             * @brief 구간 이름입니다. 등록한 쪽이 넘긴 문자열 리터럴을 그대로 가리킵니다.
             * @details **원자적이어야 합니다.** 슬롯을 잡는 쪽은 아무 스레드나 될 수 있고(워커가
             *          자기 구간을 처음 만날 때 등록합니다), 같은 순간 다른 스레드가 중복을 찾느라
             *          이 칸을 읽습니다. 평범한 포인터였을 때 그 둘은 형식상 데이터 레이스였습니다.
             * @note 이름이 같은 구간을 두 스레드가 **동시에** 처음 등록하면 슬롯이 둘 생길 수
             *       있습니다(표에 같은 이름이 두 줄). 슬롯 번호는 호출 지점마다 함수 지역 static 에
             *       한 번만 담기므로 실제로는 같은 이름이 두 지점에서 동시에 처음 불릴 때뿐이고,
             *       그때도 결과는 줄이 나뉘는 것까지입니다. 막으려면 등록에 잠금이 필요한데,
             *       측정이 실행을 막지 않는다는 이 파일의 방침과 맞지 않습니다.
             */
            atomic<const utf8*> _pName{ nullptr };
            atomic<uint64>      _frameNanos{ 0 }; ///< 이번 프레임 누적 (endFrame 에서 비움)
            atomic<uint64>      _frameCalls{ 0 }; ///< 이번 프레임 호출/카운트
            uint64              _totalNanos{ 0 }; ///< 전체 프레임 누적
            uint64              _totalCalls{ 0 };
            uint64              _minNanos{ 0 }; ///< 프레임 단위 최소/최대
            uint64              _maxNanos{ 0 };
            uint64              _sampledFrames{ 0 };
            /**
             * @brief 프레임 값의 분포입니다. 옥타브마다 `kSubBucketCount` 칸이라 해상도는 약 ±9% 입니다.
             * @details `endFrame` 만 씁니다(게임 스레드 하나). 그래서 원자가 아니어도 됩니다. 구간 64 × 칸 512 × 4 바이트 = 128 KB.
             */
            uint32 _arrBucket[kBucketCount]{};
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

/**
 * @brief 프레임 계측(GPU 타임스탬프 · 보고표)이 이 빌드에 컴파일되는지 나타냅니다.
 * @details 계측은 Info 로그를 따라갑니다. 결과가 `SW_LOG_INFO` 로만 나가므로, 그것이 사라지는 빌드(Shipping)
 *          에서는 계측 자체도 사라져야 쓸모없는 비용이 남지 않습니다. 호출부는 "왜 Info 인가" 를 몰라도
 *          되도록 이 이름 하나만 봅니다.
 */
#define SW_PROFILE_COMPILED SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )

/** @brief 두 토큰을 붙입니다(매크로 확장 후). */
#define SW_PROFILE_CONCAT_INNER( a, b ) a##b
/** @brief 두 토큰을 붙입니다. */
#define SW_PROFILE_CONCAT( a, b ) SW_PROFILE_CONCAT_INNER( a, b )

/**
 * @brief 이 스코프의 CPU 시간을 name 구간에 누적합니다.
 * @details 슬롯 번호는 함수 지역 static 으로 한 번만 받습니다(C++11 이후 스레드 안전 초기화).
 */
#define SW_PROFILE_SCOPE( name )                                            \
    static const uint32 SW_PROFILE_CONCAT( swProfileSlot_, __LINE__ ) =     \
        ::sw::engine::getFrameProfiler().registerScope( name );             \
    ::sw::ScopedFrameProfile SW_PROFILE_CONCAT( swProfileScope_, __LINE__ ) \
    {                                                                       \
        SW_PROFILE_CONCAT( swProfileSlot_, __LINE__ )                       \
    }

/** @brief name 카운터에 value 를 더합니다(드로우 수 등). 시간이 아닙니다. */
#define SW_PROFILE_COUNT( name, value )                                                            \
    do                                                                                             \
    {                                                                                              \
        static const uint32 SW_PROFILE_CONCAT( swProfileCount_, __LINE__ ) =                       \
            ::sw::engine::getFrameProfiler().registerScope( name );                                \
        ::sw::engine::getFrameProfiler().addCount( SW_PROFILE_CONCAT( swProfileCount_, __LINE__ ), \
                                                   static_cast<uint64>( value ) );                 \
    } while ( false )
