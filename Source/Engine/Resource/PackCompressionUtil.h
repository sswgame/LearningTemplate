/**
 * @file PackCompressionUtil.h
 * @brief 팩의 압축 종류(`PackCompressionType`, 디스크 번호)를 압축 코덱 등록부의 종류(`CompressionCodecType`)로 잇는 표와 코덱 조회입니다.
 *
 * 두 enum 은 서로 다른 파일의 독립된 디스크 포맷이라 값이 다릅니다(팩 Zlib=2 · 스트림 LZ4=2). 숫자를 건너다니지 않고 **이 표 한 곳**에서
 * 잇습니다 — 어느 쪽 번호도 바뀌지 않습니다. 코덱 구현은 등록부가 들고 있으므로, 팩 리더와 팩을 쓰는 쪽은 코덱 클래스를 직접 만들지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Compression/ICompressionCodec.h"

#include "Engine/Resource/ResourcePackTypes.h"

namespace sw
{
    /**
     * @struct PackCompressionUtil
     * @brief 팩 압축 종류 ↔ 코덱 종류 표와, 그 종류의 코덱을 등록부에서 찾는 창구입니다.
     */
    struct SW_API PackCompressionUtil
    {
        /** @brief 표 한 줄입니다. */
        struct CodecMapping
        {
            PackCompressionType  _packType;
            CompressionCodecType _codecType;
        };

        /**
         * @brief 팩이 아는 코덱 종류입니다. `Custom` 은 팩을 쿠킹한 쪽이 정의하는 것이라 엔진이 모르므로 줄이 없습니다.
         * @details 팩 종류를 늘리면 여기 한 줄입니다. 빠뜨리면 `ResourcePackTest.EveryPackCodecRoundTrips` 가 리플렉션된
         *          `PackCompressionType` 의 모든 값을 돌며 짚습니다.
         */
        static constexpr CodecMapping kArrCodecMapping[] = {
            {PackCompressionType::None, CompressionCodecType::None},
            { PackCompressionType::RLE,  CompressionCodecType::RLE},
            {PackCompressionType::Zlib, CompressionCodecType::Zlib},
            { PackCompressionType::LZ4,  CompressionCodecType::LZ4},
            {PackCompressionType::Zstd, CompressionCodecType::Zstd},
        };

        /** @brief 팩 종류에 대응하는 코덱 종류를 찾습니다. 표에 없으면(`Custom` · 모르는 디스크 값) false 입니다. */
        [[nodiscard]] static bool findCodecType( PackCompressionType packType, CompressionCodecType& outCodecType );

        /**
         * @brief 팩 종류의 코덱을 찾습니다. 표에 없거나 어느 등록부에도 없으면 nullptr 입니다.
         * @details 활성 등록부(`CompressionCodecRegistry::getActive`)를 먼저 봅니다 — 엔진 · 모듈이 같은 종류를 다른 구현으로 바꿨으면 팩도
         *          그것을 씁니다. 활성 등록부가 없거나 그 종류가 빠졌으면(도구 · 테스트 호스트는 내장 둘만 올린다) 엔진이 제공하는 코덱을 모두
         *          올린 이 파일의 등록부를 씁니다(`EngineCompressionCodecUtil::registerAll`). 코덱은 상태가 없어 여러 스레드가 같이 씁니다.
         */
        static ICompressionCodec* findCodec( PackCompressionType packType );
    };
} // namespace sw
