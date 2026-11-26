// EXPECT-GEN: _count
// EXPECT-NOT-GEN: _listScratch
// EXPECT-TYPE: sw::fixture::StateDeclaredType
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class StateIO;
}

namespace sw::fixture
{
    // 받아들여지는 꼴 — 코덱이 싣는 멤버, 되살리지 않는 멤버, 선언이 필요 없는 멤버(포인터 · const · 공유 const · 남는 비트), 상태를 싣지 않는 형식의 멤버.
    REFLECT()
    struct StateDeclaredType
    {
        REFLECT_BODY();
        void serializeState( ::sw::StateIO& io );

        PROPERTY( RuntimeState )
        int32                        _count{ 0 };
        SW_CODEC_STATE vector<int32> _listEntry{};
        SW_TRANSIENT vector<int32>   _listScratch{};
        SW_TRANSIENT uint8           _bDirty   : 1;
        [[maybe_unused]] uint8       _reserved : 7;
        const int32*                 _pTable{ nullptr };
        const int32                  _limit{ 4 };
        shared_ptr<const int32>      _pShared{};
        const int32*                 _arrTable[2]{};
    };

    REFLECT()
    struct StatePlainType
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _value{ 0 };
        int32 _cache{ 0 };
    };
} // namespace sw::fixture
