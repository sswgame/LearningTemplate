/**
 * @file vector.h
 * @brief 연속 배열 sw::vector 입니다(std::vector 와 인터페이스 호환). 검사 구성에서는 RaceDetectContext 로 동시 접근을 잡아내고, InlineAllocator 와 함께 쓰면 SBO 를 지원합니다.
 * @details 구현은 여기 한 자리 에서 구성(`SW_ENABLE_STL_CONTAINER`)으로 고른다: 기본은 자체 구현(`Native/NativeVector.h`), 켜면 std::vector 를 멤버로 든 같은 이름 · 같은 공개 API 의
 *          클래스(`Std/StdVector.h`)다. 형식 별칭이 아니라 클래스라 `vector<bool>` 금지 · 레이스 검사 · 범위 밖 계약(단언하고 자른다) · 옮긴 뒤 빈 목록이 두 구성에서 같다.
 *          std 짝이 없는 것 — 인라인 할당자(`small_vector`)는 std 구성에서도 자체 구현에 담고, `frame_vector` 는 아레나 할당자라 그대로 std::vector 에 들어간다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/BitwiseRelocatable.h"
#include "Core/Container/InlineAllocator.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @brief `vector` 의 원소 타입을 컴파일 시점에 거릅니다. `vector<bool>` 은 오류로 멈춥니다.
     * @details 불 목록은 한 가지 길로 모은다 — 비트 목록은 `DynamicBitset`(리플렉션이 값 하나로 다룬다), 주소가 필요한 불 원소(리플렉션 컨테이너)는 `vector<uint8>` 이다.
     *          std 구성의 저장은 std::vector 라 불 원소면 비트로 묶는 프록시가 되어 같은 이름이 구성마다 다른 의미가 된다 — 형식으로 막는다.
     */
    template <typename T>
    struct VectorElementCheck
    {
        static_assert( std::is_same_v<std::remove_cv_t<T>, bool> == false, "sw::vector<bool> is not allowed. Use DynamicBitset (bits) or vector<uint8> (addressable flags)." );
        using type = void;
    };

    /**
     * @brief @p T 가 입력 반복자(`std::iterator_traits<T>::iterator_category` 가 `std::input_iterator_tag` 로 바뀐다)면 참입니다 — 반복자 쌍을 받는 생성자 · `assign` · `insert` 의 제약.
     * @details `std::vector` 와 같은 조건이다(LegacyInputIterator). 예전 제약은 "정수가 아님" 하나라 같은 형식 둘의 중괄호 목록 `{ a, b }` 가 아무 `vector<U>` 로도
     *          (a, b) 반복자 쌍으로 바뀌어 `vector<A>` · `vector<B>` 오버로드 쌍이 모호했다(RenderGraph::addPass). C++17 의 `iterator_traits` 는 반복자가 아닌 형식에 비어 있다.
     */
    template <typename T, typename = void>
    struct IsInputIterator : std::false_type
    {
    };
    template <typename T>
    struct IsInputIterator<T, std::void_t<typename std::iterator_traits<T>::iterator_category>>
        : std::is_convertible<typename std::iterator_traits<T>::iterator_category, std::input_iterator_tag>
    {
    };
    template <typename T>
    inline constexpr bool kIsInputIterator = IsInputIterator<T>::value;
} // namespace sw

namespace sw
{
    template <typename Alloc, typename = void>
    struct has_inline_allocator : std::false_type
    {
    };
} // namespace sw

namespace sw
{
    template <typename Alloc>
    struct has_inline_allocator<Alloc, std::void_t<decltype( std::declval<Alloc&>().get_inline_buffer() )>> : std::true_type
    {
    };

    /**
     * @brief 원소를 바이트 단위로 통째로 옮겨도 되는 타입인지 판정합니다.
     * @details 복사와 소멸 어느 쪽에도 사용자 코드가 없어야 합니다. 둘 다일 때만 "원소마다 placement new" 루프를 `Memory::copy`
     *          한 번으로 바꿀 수 있습니다. 하나라도 아니면 생성자나 소멸자가 돌아야 하므로 루프가 맞습니다.
     * @note 실측: `GPUInstance`(96바이트) 20,000개를 옮기는 데 원소 루프는 329us, 바이트 복사는 ~130us 입니다. 게임 스레드가
     *       매 프레임 렌더 패킷에 스냅샷을 싣는 곳이라 그 차이가 그대로 프레임 시간입니다.
     */
    template <typename T>
    inline constexpr bool is_bitwise_copyable_v = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;
} // namespace sw

#if defined( SW_ENABLE_STL_CONTAINER )
    #include "Core/Container/Std/StdVector.h"
#else
    #include "Core/Container/Native/NativeVector.h"
#endif

namespace sw
{
    template <typename T, size_t N = 32>
    using small_vector = vector<T, InlineAllocator<T, N>>;

    /** @brief 버퍼 포인터와 두 수뿐이다. 인라인 버퍼를 든 것(`small_vector`)은 자기 안을 가리켜 바이트로 옮길 수 없고, std 구성의 std::vector 는 반복자 디버그면 대리 객체가 주소를 든다. */
    template <typename T, typename Allocator>
    struct is_bitwise_relocatable<vector<T, Allocator>>
        : std::bool_constant<kUsesStdContainers == false && has_inline_allocator<Allocator>::value == false && std::is_empty_v<Allocator>>
    {
    };
} // namespace sw
