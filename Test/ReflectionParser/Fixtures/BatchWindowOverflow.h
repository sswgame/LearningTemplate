// BATCH-WITH: Batch/BatchOtherHeader.h
// EXPECT-GEN: struct Registrar<sw::fixture::BatchLongRecord>
// EXPECT-GEN: _parentFQN          = ::sw::hashed_string( "sw::fixture::BatchOtherBase" )
// EXPECT-GEN: "_flags"
// EXPECT-GEN: "sw::fixture::BatchLongFlags", "ParserFixture", kArrEntry, {}, nullptr, nullptr, true }
// EXPECT-META: TooltipEnd
// EXPECT-META: \"인용\"
// EXPECT-META: 512 바이트보다 멀게
// EXPECT-TYPE: sw::fixture::BatchLongRecord
// EXPECT-TYPE: sw::fixture::BatchLongFlags
// EXPECT-TYPE: sw::fixture::BatchOtherBase
// EXPECT-TYPE: sw::fixture::BatchOtherFlags
// 매크로 인자가 길어 형식 선언과 512 바이트 넘게 떨어져도 그 형식의 REFLECT · ENUM 이다(텍스트 창으로 찾던 시절에는 REFLECT 는 엉뚱한 오류, ENUM 은 조용히 빠졌다).
// 부모와 칸의 열거형은 짝 헤더(다른 헤더)에 있다 — 묶음 번역 단위 안에서 찾는다. 설명의 한글 · 따옴표는 libclang 이 다시 이스케이프한 표식 철자를 원본 바이트로 푸는 길을 본다.
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "Batch/BatchOtherHeader.h"

namespace sw::fixture
{
    ENUM( Flags
          /*
             창 넘김 창 넘김 창 넘김 창 넘김 창 넘김 창 넘김
             창 넘김 창 넘김 창 넘김 창 넘김 창 넘김 창 넘김
             창 넘김 창 넘김 창 넘김 창 넘김 창 넘김 창 넘김
             창 넘김 창 넘김 창 넘김 창 넘김 창 넘김 창 넘김
             창 넘김 창 넘김 창 넘김 창 넘김 창 넘김 창 넘김
             창 넘김 창 넘김 창 넘김 창 넘김 창 넘김 창 넘김
             창 넘김 창 넘김 창 넘김 창 넘김 창 넘김 창 넘김
             창 넘김 창 넘김 창 넘김 창 넘김 창 넘김 창 넘김
             창 넘김 창 넘김 창 넘김 창 넘김 창 넘김 창 넘김
             창 넘김 창 넘김 창 넘김 창 넘김 창 넘김 창 넘김
          */
    )
    enum class BatchLongFlags : uint8
    {
        None  = 0,
        Left  = 1 << 0,
        Right = 1 << 1
    };

    REFLECT( Category = "Fixture", Tooltip = "이 설명은 매크로와 형식 선언 사이를 512 바이트보다 멀게 벌린다 — 이 설명은 매크로와 형식 선언 사이를 512 바이트보다 멀게 벌린다 — 이 설명은 매크로와 형식 선언 사이를 512 바이트보다 멀게 벌린다 — 이 설명은 매크로와 형식 선언 사이를 512 바이트보다 멀게 벌린다 — 이 설명은 매크로와 형식 선언 사이를 512 바이트보다 멀게 벌린다 — 이 설명은 매크로와 형식 선언 사이를 512 바이트보다 멀게 벌린다 — \"인용\" TooltipEnd" )
    struct BatchLongRecord : public BatchOtherBase
    {
        REFLECT_BODY();
        PROPERTY()
        BatchOtherFlags _flags{ BatchOtherFlags::None };
        PROPERTY()
        BatchLongFlags _sides{ BatchLongFlags::None };
    };
} // namespace sw::fixture
