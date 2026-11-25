// EXPECT-ERROR: SW_CODEC_STATE on 'sw::fixture::CodecStateWithoutCodecType::_listEntry'
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    // 싣는 함수가 없는데 코덱이 싣는다고 적으면 아무도 싣지 않는다.
    REFLECT()
    struct CodecStateWithoutCodecType
    {
        REFLECT_BODY();
        PROPERTY( RuntimeState )
        int32                        _count{ 0 };
        SW_CODEC_STATE vector<int32> _listEntry{};
    };
} // namespace sw::fixture
