/**
 * @file CompressionCodecType.h
 * @brief 압축 코덱의 식별자입니다. 코덱을 고르기만 하는 쪽(아카이브의 기본 인자 등)이 코덱 인터페이스(`ICompressionCodec.h`) 없이 씁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @enum CompressionCodecType
     * @brief 지원하는 압축 코덱의 식별자입니다.
     * @details 이 값은 디스크에 기록됩니다(`CompressionStream` 컨테이너 헤더의 `_codecType`). 기존 값을 바꾸면 예전에 쓴 스트림을
     *          읽지 못하므로, 새 코덱은 **뒤에 덧붙이기만** 합니다.
     *
     *          리소스 팩의 `PackCompressionType` 과는 일부러 값을 다르게 두었습니다. 둘은 서로 다른 파일의 독립된 포맷입니다.
     *          숫자를 맞춰 두면 `static_cast` 로 오가고 싶어지고, 그 순간부터 한쪽의 포맷 변경이 다른 쪽까지 끌고 갑니다.
     *          팩은 자기 enum 을 이 enum 으로 **표 한 곳**에서 옮겨(`PackCompressionUtil::kArrCodecMapping`) 같은 레지스트리에서 코덱을 찾습니다.
     */
    enum class CompressionCodecType : uint8
    {
        None   = 0,   ///< 무압축(그대로 복사)
        RLE    = 1,   ///< 빠른 바이트 단위 RLE
        LZ4    = 2,   ///< LZ4. 해제가 가장 빠르다(로딩 시간이 곧 해제 시간인 곳)
        Zstd   = 3,   ///< Zstandard. 결과가 가장 작다(배포물)
        Zlib   = 4,   ///< Deflate. 리소스 팩의 기본 코덱이다(`PackConfig`)
        Custom = 255, ///< 사용자 정의 코덱
    };
} // namespace sw
