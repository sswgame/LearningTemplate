/**
 * @file RenderTargetImageFile.h
 * @brief GPU 에서 읽어 온 렌더 타깃 바이트를 이미지 파일(PNG 또는 PPM)로 씁니다. 트랜지언트, 임의 텍스처, Present 캡처 덤프가 함께 씁니다.
 * @details GPU 를 부르지 않는 순수 변환이라 테스트가 GPU 없이 부를 수 있습니다. 읽어 오는 쪽은 `FrameCapture` 입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @struct RenderTargetImageFile
     * @brief 읽어 온 픽셀을 RGBA8 로 풀고 파일로 쓰는 정적 함수 모음입니다.
     */
    struct SW_API RenderTargetImageFile
    {
        /**
         * @brief 읽어 온 픽셀을 빈틈없는 RGBA8(위 행부터, 알파 255)로 풉니다. PPM 과 PNG 가 같이 씁니다.
         *        단일 채널(깊이 읽기 레이아웃 `kRHIDepthReadbackFormat` = R32_FLOAT 는 [0,1] 을 0..255 로, R8 은 그대로)은 회색이고, 그 밖의 3 채널 미만 포맷이면 false 입니다.
         * @details **half 첨부를 바이트로 읽으면 안 됩니다.** HDR 첨부(R16G16B16A16_FLOAT)를 8비트로 가정하고 앞 세 바이트를 집으면
         *          가수 하위 바이트가 색이 되어 무의미한 이미지가 나옵니다(bytesPerPixel 은 8 이라 크기 검사도 통과합니다). 디퍼드 파이프라인은
         *          LitColor 부터 TAAColor 까지 넷이 이 포맷입니다. HDR 은 [0,1] 로 자르고 톤매핑은 하지 않습니다. "무엇이 들어 있나"를 보는 덤프입니다.
         */
        static bool unpackRGBA8( const vector<uint8>& bytes, const RHITextureMipSpan& layout, RHIFormat format, vector<uint8>& outRGBABytes );
        /** @brief 읽어 온 바이트를 파일로 씁니다. 확장자가 `.png` 면 PNG, 아니면 PPM(P6)입니다. */
        [[nodiscard]] static bool writeImageFile( const vector<uint8>& bytes, const RHITextureMipSpan& layout, RHIFormat format, string_view outFilePath );
    };
} // namespace sw
