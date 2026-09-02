/**
 * @file ObjectHandle.h
 * @brief index|generation 불투명 핸들. generation 0은 무효입니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @brief 슬롯 인덱스와 세대를 묶은 핸들입니다.
     * @details 파괴 후 인덱스를 재사용해도 옛 핸들은 세대가 달라 무효입니다.
     *          packed 레이아웃은 (generation << 32) | index 이며, 값 0은 무효입니다.
     */
    class ObjectHandle
    {
    public:
        /** @brief 무효 핸들(index 0, generation 0)을 만듭니다. */
        constexpr ObjectHandle() noexcept = default;

        /** @brief 인덱스와 세대로 핸들을 만듭니다. generation 0은 무효입니다. */
        static constexpr ObjectHandle make( uint32 index, uint32 generation ) noexcept
        {
            ObjectHandle handle;
            handle._index      = index;
            handle._generation = generation;
            return handle;
        }

        /** @brief packed uint64에서 핸들을 복원합니다. */
        static constexpr ObjectHandle fromPacked( uint64 packed ) noexcept { return make( static_cast<uint32>( packed ), static_cast<uint32>( packed >> 32 ) ); }

        /** @brief 슬롯 인덱스를 반환합니다. */
        [[nodiscard]] constexpr uint32 index() const noexcept { return _index; }
        /** @brief 세대를 반환합니다. 0이면 무효입니다. */
        [[nodiscard]] constexpr uint32 generation() const noexcept { return _generation; }
        /** @brief (generation << 32) | index 를 반환합니다. */
        [[nodiscard]] constexpr uint64 packed() const noexcept { return ( static_cast<uint64>( _generation ) << 32 ) | static_cast<uint64>( _index ); }

        /** @brief generation이 0이 아니면 true입니다. 테이블 점유 여부는 별도 확인입니다. */
        [[nodiscard]] constexpr bool isValid() const noexcept { return _generation != 0; }
        /** @brief isValid()와 같습니다. */
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

        friend constexpr bool operator==( ObjectHandle lhs, ObjectHandle rhs ) noexcept { return lhs._index == rhs._index && lhs._generation == rhs._generation; }
        friend constexpr bool operator!=( ObjectHandle lhs, ObjectHandle rhs ) noexcept { return ( lhs == rhs ) == false; }
        friend constexpr bool operator<( ObjectHandle lhs, ObjectHandle rhs ) noexcept { return lhs.packed() < rhs.packed(); }

        friend std::ostream& operator<<( std::ostream& os, ObjectHandle handle )
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
struct std::hash<sw::ObjectHandle>
{
    size_t operator()( sw::ObjectHandle handle ) const noexcept { return std::hash<uint64>{}( handle.packed() ); }
};
