#include "pch.h"

#include "Editor/Common/Commands/Scene/SiblingOrderKey.h"

#include <limits>

namespace sw::editor
{
    optional<uint32> SiblingOrderKey::between( optional<uint32> previous, optional<uint32> next )
    {
        constexpr uint32 kMaxKey = std::numeric_limits<uint32>::max();
        if ( previous.has_value() == false && next.has_value() == false )
            return kAppendStep;
        if ( next.has_value() == false ) // 끝에 더한다 — 간격을 두고, `uint32` 끝이 가까우면 남은 틈의 가운데
        {
            const uint32 room = kMaxKey - *previous;
            if ( room == 0 )
                return nullopt;
            return *previous + ( room > kAppendStep ? kAppendStep : ( room + 1 ) / 2 );
        }
        if ( previous.has_value() == false ) // 앞에 더한다 — 0 아래 키는 없으므로 [0, next) 의 가운데
        {
            if ( *next == 0 )
                return nullopt;
            return *next > kAppendStep ? *next - kAppendStep : *next / 2;
        }
        if ( *next <= *previous || *next - *previous < 2 )
            return nullopt;
        return *previous + ( *next - *previous ) / 2;
    }

    void SiblingOrderKey::spread( uint32 count, vector<uint32>& outListKey )
    {
        outListKey.clear();
        outListKey.reserve( count );
        // 간격은 `kAppendStep` 이되, 형제가 아주 많으면 `uint32` 안에 다 들게 줄인다(마지막 키 뒤에도 틈을 남긴다).
        const uint64 widest = static_cast<uint64>( std::numeric_limits<uint32>::max() ) / ( static_cast<uint64>( count ) + 1 );
        const uint64 step   = widest < kAppendStep ? ( widest == 0 ? 1 : widest ) : kAppendStep;
        for ( uint32 index = 0; index < count; ++index )
            outListKey.push_back( static_cast<uint32>( step * ( static_cast<uint64>( index ) + 1 ) ) );
    }
} // namespace sw::editor
