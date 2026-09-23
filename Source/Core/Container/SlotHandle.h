/**
 * @file SlotHandle.h
 * @brief index · generation 으로 이루어진 불투명 핸들입니다. generation 0 은 무효입니다.
 * @note `SlotHandleTable` 의 슬롯을 가리키는 범용 핸들입니다. RHI 리소스 · 물리 바디 · 공간 분할 키가 이것을 씁니다.
 *       게임 오브젝트와는 관계가 없습니다. 예전 이름 `ObjectHandle` 은 `GameObject` 쪽 참조로 오해되기 쉬워 바꿨습니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @brief 슬롯 인덱스와 세대를 묶은 핸들입니다.
     * @details 파괴된 뒤 인덱스가 재사용돼도, 옛 핸들은 세대가 달라 무효입니다.
     *          packed 배치는 (generation << 32) | index 이며, 값 0 은 무효입니다.
     */
    class SlotHandle
    {
    public:
        /** @brief 무효 핸들(index 0, generation 0)을 만듭니다. */
        constexpr SlotHandle() noexcept = default;

        /** @brief 인덱스와 세대로 핸들을 만듭니다. generation 0 은 무효입니다. */
        static constexpr SlotHandle make( uint32 index, uint32 generation ) noexcept
        {
            SlotHandle handle;
            handle._index      = index;
            handle._generation = generation;
            return handle;
        }

        /** @brief packed uint64 에서 핸들을 복원합니다. */
        static constexpr SlotHandle fromPacked( uint64 packed ) noexcept { return make( static_cast<uint32>( packed ), static_cast<uint32>( packed >> 32 ) ); }

        /** @brief 슬롯 인덱스를 반환합니다. */
        [[nodiscard]] constexpr uint32 index() const noexcept { return _index; }
        /** @brief 세대를 반환합니다. 0 이면 무효입니다. */
        [[nodiscard]] constexpr uint32 generation() const noexcept { return _generation; }
        /** @brief (generation << 32) | index 를 반환합니다. */
        [[nodiscard]] constexpr uint64 packed() const noexcept { return ( static_cast<uint64>( _generation ) << 32 ) | static_cast<uint64>( _index ); }

        /** @brief generation 이 0 이 아니면 true 입니다. 테이블에서 아직 점유 중인지는 따로 확인해야 합니다. */
        [[nodiscard]] constexpr bool isValid() const noexcept { return _generation != 0; }
        /** @brief isValid() 와 같습니다. */
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

        friend constexpr bool operator==( SlotHandle lhs, SlotHandle rhs ) noexcept { return lhs._index == rhs._index && lhs._generation == rhs._generation; }
        friend constexpr bool operator!=( SlotHandle lhs, SlotHandle rhs ) noexcept { return ( lhs == rhs ) == false; }
        friend constexpr bool operator<( SlotHandle lhs, SlotHandle rhs ) noexcept { return lhs.packed() < rhs.packed(); }

        friend std::ostream& operator<<( std::ostream& os, SlotHandle handle )
        {
            os << handle._index << ':' << handle._generation;
            return os;
        }

    private:
        uint32 _index{ 0 };
        uint32 _generation{ 0 };
    };
} // namespace sw

template <>
struct std::hash<sw::SlotHandle>
{
    size_t operator()( sw::SlotHandle handle ) const noexcept { return std::hash<uint64>{}( handle.packed() ); }
};
