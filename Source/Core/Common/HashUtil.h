/**
 * @file HashUtil.h
 * @brief 해시 · 씨앗을 섞는 상수(황금비 · splitmix64 · FNV-1a)와 도우미, 해시 컨테이너의 기본 해시 `DefaultHash` 입니다.
 * @details 같은 상수를 파일마다 16 진 리터럴로 다시 적지 않는다(`CheckWellKnownConstants` 가 막는다). 값을 바꾸면 프로세스 밖으로 나가는
 *          해시(네트워크 비교 · 쿠킹 산출물)가 갈라지므로 여기 값은 바꾸지 않는다.
 *          키 구조체마다 `XxxKeyHash` 구조체를 따로 만들지 않는다. 키에 `computeHash() const` 를 두면 `sw::unordered_map` · `sw::unordered_set`
 *          의 기본 해시(`DefaultHash`)가 그것을 부르고, 여러 칸을 섞는 본문은 `HashUtil::makeHash( _a, _b )` 한 줄이다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @class DefaultHash
     * @brief 해시 컨테이너의 기본 해시입니다. 키에 `computeHash() const` 가 있으면 그것을, 없으면 `std::hash<Key>` 를 씁니다.
     * @details `std::hash<Key>` 를 상속하므로 그 특수화가 가진 이종 키 오버로드(`std::hash<sw::string>` 의 `string_view`)가 그대로 남습니다.
     *          `computeHash` 쪽은 같은 키 타입만 받습니다 — 이종 조회가 필요하면 `std::hash` 특수화를 둡니다.
     */
    template <typename Key, typename = void>
    struct DefaultHash : std::hash<Key>
    {
    };
} // namespace sw

namespace sw
{
    /** @brief `computeHash() const` 가 정수를 돌려주는 키의 기본 해시입니다. */
    template <typename Key>
    struct DefaultHash<Key, std::enable_if_t<std::is_integral_v<decltype( std::declval<const Key&>().computeHash() )>>>
    {
        /** @brief 키의 `computeHash()` 를 돌려줍니다. */
        [[nodiscard]] size_t operator()( const Key& key ) const noexcept( noexcept( key.computeHash() ) ) { return static_cast<size_t>( key.computeHash() ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct HashUtil
     * @brief 해시 섞기 상수와 함수입니다(전부 static).
     */
    struct HashUtil
    {
        /** @brief 2^64 / φ 입니다. 피보나치 해싱 · splitmix64 걸음 · 기본 씨앗이 씁니다. */
        static constexpr uint64 kGoldenRatio64 = 0x9E3779B97F4A7C15ull;
        /** @brief 2^32 / φ 입니다. 32 비트 씨앗 · 32 비트 결합이 씁니다. */
        static constexpr uint32 kGoldenRatio32 = 0x9E3779B9u;
        /** @brief splitmix64 마무리의 첫 곱수입니다. */
        static constexpr uint64 kSplitMixMultiplier0 = 0xBF58476D1CE4E5B9ull;
        /** @brief splitmix64 마무리의 둘째 곱수입니다. */
        static constexpr uint64 kSplitMixMultiplier1 = 0x94D049BB133111EBull;
        /** @brief FNV-1a 32 비트 기저값입니다. */
        static constexpr uint32 kFnvOffset32 = 2166136261u;
        /** @brief FNV-1a 32 비트 소수입니다. */
        static constexpr uint32 kFnvPrime32 = 16777619u;
        /** @brief FNV-1a 64 비트 기저값입니다. */
        static constexpr uint64 kFnvOffset64 = 14695981039346656037ull;
        /** @brief FNV-1a 64 비트 소수입니다. */
        static constexpr uint64 kFnvPrime64 = 1099511628211ull;

        /** @brief splitmix64 의 마무리 섞기입니다. 비트를 고르게 퍼뜨립니다. */
        [[nodiscard]] static constexpr uint64 mix64( uint64 value ) noexcept
        {
            value = ( value ^ ( value >> 30 ) ) * kSplitMixMultiplier0;
            value = ( value ^ ( value >> 27 ) ) * kSplitMixMultiplier1;
            return value ^ ( value >> 31 );
        }

        /** @brief splitmix64 수열의 다음 값입니다 — 상태에 황금비를 더하고 마무리 섞기를 합니다. 씨앗 0 도 쓸 수 있습니다(고정점이 없다). */
        [[nodiscard]] static constexpr uint64 splitMix64Next( uint64& inoutState ) noexcept
        {
            inoutState += kGoldenRatio64;
            return mix64( inoutState );
        }

        /** @brief @p seed 에 @p value 를 섞습니다(boost `hash_combine` 의 64 비트 판). */
        [[nodiscard]] static constexpr uint64 combine( const uint64 seed, const uint64 value ) noexcept
        {
            return seed ^ ( value + kGoldenRatio64 + ( seed << 6 ) + ( seed >> 2 ) );
        }

        /**
         * @brief 여러 값의 해시를 차례로 `combine` 해 하나로 만듭니다. 키 구조체의 `computeHash()` 본문입니다(`return HashUtil::makeHash( _a, _b );`).
         * @details 값마다 `DefaultHash` 를 씁니다(정수 · 열거형 · 문자열 · `computeHash()` 를 가진 키가 섞여도 됩니다). 프로세스 안에서만 쓰는
         *          해시입니다 — `std::hash` 는 구현마다 다르므로 파일 · 네트워크로 나가는 해시에는 쓰지 않습니다. 인자가 없으면 컴파일되지 않습니다.
         */
        template <typename TFirst, typename... TRest>
        [[nodiscard]] static uint64 makeHash( const TFirst& first, const TRest&... rest )
        {
            uint64 hash = combine( 0, static_cast<uint64>( DefaultHash<TFirst>{}( first ) ) );
            ( ( hash = combine( hash, static_cast<uint64>( DefaultHash<TRest>{}( rest ) ) ) ), ... );
            return hash;
        }
    };
} // namespace sw
