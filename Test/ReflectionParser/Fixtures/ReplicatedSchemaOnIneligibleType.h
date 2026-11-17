// EXPECT-GEN: s_arrReplicatedField
// EXPECT-GEN: s_fixedCodec._pReplicated = &s_replicatedCodec;
// EXPECT-GEN: R=sw::fixture::ReplicatedSchemaHost|F=_health:int32:min=0:max=100:int|
// EXPECT-NOT-GEN: F=_listCache
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    // 타입 전체는 적격이 아니어도(길이 없는 vector) Replicated 칸만의 스키마는 생긴다 — 복제 스키마는 서술자 표의 Replicated 부분이다.
    REFLECT()
    struct ReplicatedSchemaHost
    {
        REFLECT_BODY();
        PROPERTY( Replicated, Min = 0, Max = 100 )
        int32 _health{};
        PROPERTY()
        vector<int32> _listCache;
    };
} // namespace sw::fixture
