/**
 * @file StreamingSourcePriority.h
 * @brief 스트리밍 우선순위 형식입니다 — 셀 로드(`SceneStreamingSubsystem`)와 텍스처 밉 스트리밍(`TextureCache`)이 같은 값으로 차례를 정합니다.
 * @details 정하는 곳은 스트리밍 소스(`StreamingSource`, `Engine/Scene/StreamingCellGrid.h`)다. 텍스처 캐시(그래픽 층 4)가 씬(5)을 모르므로 형식은 둘이 함께 보는 에셋 층에 둔다
 *          (에셋 스트리밍 큐 `AssetStreamingQueue` 곁). 이름은 `getEnumName` 으로 읽는다. 남은 일은 `docs/NEXT.md` "씬 스트리밍 (라)".
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    enum class AsyncIOPriority : uint8; // 값은 Core/File/AsyncFileIO.h

    /** @brief 스트리밍 소스의 우선순위입니다. 앞 값일수록 그 소스가 덮는 셀 · 그 셀에 놓인 메시의 텍스처를 먼저 읽습니다(언리얼 `EStreamingSourcePriority` 와 같은 다섯 단계 · 같은 차례). */
    ENUM()
    enum class StreamingSourcePriority : uint8
    {
        Highest,
        High,
        Normal,
        Low,
        Lowest,
    };

    /**
     * @brief 스트리밍 우선순위를 IO 큐 우선순위로 바꿉니다 — 셀 내용 읽기와 텍스처 밉 구간 읽기가 같은 큐에서 같은 규칙으로 앞선다.
     * @details Highest · High → High, Normal → Normal, Low · Lowest → Low. `Critical` 은 프레임이 기다리는 읽기 몫이라 쓰지 않는다(스트리밍은 기다리지 않는다).
     */
    [[nodiscard]] SW_API AsyncIOPriority toAsyncIOPriority( StreamingSourcePriority priority );
} // namespace sw
