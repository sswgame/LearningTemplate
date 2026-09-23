/**
 * @file EngineCompressionCodecUtil.h
 * @brief Engine 이 외부 라이브러리로 구현한 압축 코덱들의 **등록 목록**을 한곳에 둡니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Compression/ICompressionCodec.h"

namespace sw
{
    class CompressionCodecRegistry;

    /**
     * @struct EngineCompressionCodecUtil
     * @brief `Engine/Compression` 이 제공하는 코덱을 레지스트리에 붙입니다.
     * @details Core 는 압축 라이브러리에 의존하지 않으므로(`ReflectionParser` 가 Core 를 링크합니다) LZ4 · Zstd · Zlib 은
     *          Engine 이 가져와 붙입니다. **그 목록을 호출부에 두지 않습니다.** `EngineLoop::initialize` 가 손으로 둘만
     *          부르고 있었고, 그래서 `Zlib` 은 클래스도 열거값도 있는데 아무도 등록하지 않아 `CompressionStream` 에서 쓸
     *          수 없었습니다. (요청하면 무압축으로 떨어졌고, 다른 도구가 쓴 Zlib 스트림은 읽히지 않았습니다.)
     */
    struct SW_API EngineCompressionCodecUtil
    {
        /** @brief Engine 이 제공하는 코덱 타입입니다. 코덱을 늘리면 여기와 `registerAll` 만 고칩니다. */
        static constexpr CompressionCodecType kArrCodecType[] = {
            CompressionCodecType::LZ4,
            CompressionCodecType::Zstd,
            CompressionCodecType::Zlib,
        };

        /** @brief 위 목록의 코덱을 모두 등록합니다. 같은 타입이 이미 있으면 덮어씁니다. */
        static void registerAll( CompressionCodecRegistry& inoutRegistry );
    };
} // namespace sw
