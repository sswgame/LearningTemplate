/**
 * @file HashUtil.h
 * @brief 해시 · 씨앗을 섞는 상수(황금비 · splitmix64 · FNV-1a)와 도우미, 해시 컨테이너의 기본 해시 `DefaultHash` 입니다.
 * @details 같은 상수를 파일마다 16 진 리터럴로 다시 적지 않는다(`CheckWellKnownConstants` 가 막는다). 값을 바꾸면 프로세스 밖으로 나가는
 *          해시(네트워크 비교 · 쿠킹 산출물)가 갈라지므로 여기 값은 바꾸지 않는다.
 *          키 구조체마다 `XxxKeyHash` 구조체를 따로 만들지 않는다. 키에 `computeHash() const` 를 두면 `sw::unordered_map` · `sw::unordered_set`
 *          의 기본 해시(`DefaultHash`)가 그것을 부르고, 여러 칸을 섞는 본문은 `HashUtil::makeHash( _a, _b )` 한 줄이다.
 */
#pragma once
#include "Core/Common/AsciiUtil.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @class DefaultHash
     * @brief 해시 컨테이너의 기본 해시입니다. 키에 `computeHash() const` 가 있으면 그것을, 없으면 `std::hash<Key>` 를 씁니다.
     * @details `std::hash<Key>` 를 상속하므로 그 특수화가 가진 이종 키 오버로드가 그대로 남습니다.
     *          문자열 키(`sw::string` · `sw::wstring`)는 `Core/Container/string.h` 의 `DefaultHash` 특수화가 기준입니다. STL 구성에서는
     *          sw::string 이 std::string 이라 `std::hash` 로는 두 구성의 해시 · 이종 조회를 같게 할 수 없습니다.
     *          `computeHash` 쪽은 같은 키 타입만 받습니다. 이종 조회가 필요하면 그 키의 `DefaultHash` 특수화를 둡니다.
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

        /** @brief FNV-1a 32 비트의 한 걸음입니다 — @p hash 에 @p value 를 `xor` 한 뒤 소수를 곱합니다. 바이트가 아닌 값(헤더 · 칸 필드)을 하나씩 섞을 때 씁니다. */
        [[nodiscard]] static constexpr uint32 fnvStep32( const uint32 hash, const uint32 value ) noexcept { return ( hash ^ value ) * kFnvPrime32; }

        /** @brief FNV-1a 64 비트의 한 걸음입니다 — @p hash 에 @p value 를 `xor` 한 뒤 소수를 곱합니다. */
        [[nodiscard]] static constexpr uint64 fnvStep64( const uint64 hash, const uint64 value ) noexcept { return ( hash ^ value ) * kFnvPrime64; }

        /**
         * @brief FNV-1a 32 비트로 바이트열을 이어 섞습니다. 처음이면 @p hash 에 `kFnvOffset32` 를 넘기고, 이어 섞을 때는 앞 결과를 넘깁니다.
         * @details 바이트를 낮은 주소부터 하나씩 섞습니다. 패킷 체크섬 · 압축 체크섬이 쓰므로 순서와 상수를 바꾸지 않습니다.
         */
        [[nodiscard]] static uint32 appendBytes32( uint32 hash, const void* pData, const size_t byteCount ) noexcept
        {
            const uint8* pByte = static_cast<const uint8*>( pData );
            for ( size_t index = 0; index < byteCount; ++index )
            {
                hash = fnvStep32( hash, pByte[index] );
            }
            return hash;
        }

        /**
         * @brief FNV-1a 64 비트로 바이트열을 이어 섞습니다. 처음이면 @p hash 에 `kFnvOffset64` 를 넘기고, 이어 섞을 때는 앞 결과를 넘깁니다.
         * @details 바이트를 낮은 주소부터 하나씩 `xor` 한 뒤 소수를 곱합니다. 저장 · 네트워크로 나가는 상태 해시가 쓰므로 순서와 상수를 바꾸지 않습니다.
         *          객체의 바이트가 곧 해시 입력이라 패딩이 있는 구조체에는 쓰지 않습니다.
         */
        [[nodiscard]] static uint64 appendBytes64( uint64 hash, const void* pData, const size_t byteCount ) noexcept
        {
            const uint8* pByte = static_cast<const uint8*>( pData );
            for ( size_t index = 0; index < byteCount; ++index )
            {
                hash = fnvStep64( hash, pByte[index] );
            }
            return hash;
        }

        /** @brief 값 하나의 바이트를 `appendBytes64` 로 이어 섞습니다. 패딩이 없는 값(정수 · 실수 · 열거형)에만 씁니다. */
        template <typename T>
        [[nodiscard]] static uint64 appendValue64( const uint64 hash, const T& value ) noexcept
        {
            static_assert( std::is_trivially_copyable_v<T>, "바이트로 섞을 수 있는 값만 받는다" );
            return appendBytes64( hash, &value, sizeof( T ) );
        }

        /**
         * @brief 64 비트 FNV-1a 문자열 해시입니다(포인터 + 길이). 쿠킹 산출물 · 저장 · 네트워크의 문자열 키가 쓰므로 값을 바꾸지 않습니다.
         * @details **두 경로 모두 CharT 의 부호 없는 짝 타입을 거친다.** 부호 확장을 막기 위해서다. `char` 가 음수면(UTF-8 의 0x80
         *          이상 바이트) 0xFFFFFFFFFFFFFF80 같은 값이 섞여 들어가고, `char` 의 부호 여부는 구현 정의라서 **플랫폼이 바뀌면
         *          해시가 달라진다**(ARM 은 unsigned char 다).
         *          다만 `uint8` 로 고정하면 안 된다. 이 템플릿은 `utf16` 으로도 불리는데(`computeStringHash64( wstring )`), 그러면 넓은
         *          문자가 **하위 한 바이트로 잘려** 한글처럼 상위 바이트만 다른 문자들이 모두 같은 값으로 해시된다.
         *          @p bIgnoreCase 가 참이면 ASCII 대문자를 소문자로 바꿔 섞는다(기본값이 참이다 — 대소문자를 구분하려면 거짓을 넘긴다).
         */
        template <typename CharT>
        [[nodiscard]] static constexpr uint64 computeStringHash64( const CharT* pStr, const size_t length, const bool bIgnoreCase = true, const uint64 seed = kFnvOffset64 ) noexcept
        {
            uint64 hash = seed;
            for ( size_t charIndex = 0; charIndex < length; ++charIndex )
            {
                const uint64 c = static_cast<uint64>( static_cast<std::make_unsigned_t<CharT>>( bIgnoreCase ? AsciiUtil::toLower( pStr[charIndex] ) : pStr[charIndex] ) );
                hash           = fnvStep64( hash, c );
            }
            return hash;
        }

        /** @brief 64 비트 FNV-1a 문자열 해시입니다(string · string_view · fixed_string 등 `data()` · `size()` 를 가진 형식). */
        template <typename StringType, typename = std::enable_if_t<std::is_class_v<StringType>>>
        [[nodiscard]] static constexpr uint64 computeStringHash64( const StringType& str, const bool bIgnoreCase = true, const uint64 seed = kFnvOffset64 ) noexcept
        {
            return computeStringHash64( str.data(), str.size(), bIgnoreCase, seed );
        }

        /** @brief 32 비트 FNV-1a 문자열 해시입니다(포인터 + 길이). 64 비트 쪽과 **같은 규칙**이다(@p bIgnoreCase 경로 포함) — 다르면 같은 문자열이 두 경로에서 다르게 해시된다. */
        template <typename CharT>
        [[nodiscard]] static constexpr uint32 computeStringHash32( const CharT* pStr, const size_t length, const bool bIgnoreCase = true, const uint32 seed = kFnvOffset32 ) noexcept
        {
            uint32 hash = seed;
            for ( size_t charIndex = 0; charIndex < length; ++charIndex )
            {
                const uint32 c = static_cast<uint32>( static_cast<std::make_unsigned_t<CharT>>( bIgnoreCase ? AsciiUtil::toLower( pStr[charIndex] ) : pStr[charIndex] ) );
                hash           = fnvStep32( hash, c );
            }
            return hash;
        }

        /** @brief 32 비트 FNV-1a 문자열 해시입니다(string · string_view · fixed_string 등). */
        template <typename StringType, typename = std::enable_if_t<std::is_class_v<StringType>>>
        [[nodiscard]] static constexpr uint32 computeStringHash32( const StringType& str, const bool bIgnoreCase = true, const uint32 seed = kFnvOffset32 ) noexcept
        {
            return computeStringHash32( str.data(), str.size(), bIgnoreCase, seed );
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
