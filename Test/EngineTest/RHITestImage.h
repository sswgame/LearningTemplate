/**
 * @file Test/EngineTest/RHITestImage.h
 * @brief GPU 에서 되읽은 이미지 — 형식(RGBA8 · BGRA8 · RGBA16F)과 무관하게 픽셀을 R · G · B · A 로 읽는다.
 */
#pragma once
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class FrameRenderer;
} // namespace sw

namespace test
{
    /** @brief 되읽은 픽셀 하나 — 저장 형식과 무관하게 0~255 의 R · G · B · A. */
    struct Rgba8
    {
        uint8 _r{ 0 };
        uint8 _g{ 0 };
        uint8 _b{ 0 };
        uint8 _a{ 0 };
    };
} // namespace test

namespace test
{
    /**
     * @brief 되읽은 이미지 한 장(`RenderPassGpuTest`).
     * @details `getPixel` 이 RGBA 로 준다 — 되읽기 · 행 포인터 계산 · BGRA 뒤집기를 케이스마다 손으로 들면, 백엔드마다 스왑체인 형식이
     *          달라 뒤집기를 한 곳이라도 빠뜨릴 때 그 백엔드에서만 빨강과 파랑이 바뀐다.
     *          반정밀도 첨부(`R16G16B16A16_FLOAT`)는 [0,1] 로 잘라 0~255 로 환산한다 — 두 판을 같은 규칙으로 견주는 데 쓴다.
     */
    class RHITestImage
    {
    public:
        /** @brief 프레임 렌더러의 transient 첨부(예: `"SceneColor"`)를 되읽습니다. 실패하면 false 이고 이미지는 빈다. */
        bool readTransient( sw::FrameRenderer& renderer, sw::string_view attachmentName );
        /**
         * @brief 이미 가진 바이트를 이미지로 삼습니다 — 되읽기 없이 해석 규칙(BGRA 뒤집기 · 반정밀도)을 시험할 때.
         * @details 네 백엔드가 `SceneColor` 를 전부 RGBA8 로 되읽는 기계에서는 BGRA 뒤집기를 틀려도 GPU 케이스가 하나도 지지
         *          않는다. 그 규칙은 `RHITestImageTest` 가 GPU 없이 지킨다.
         */
        void assign( sw::vector<uint8> bytes, const sw::RHITextureMipSpan& layout, sw::RHIFormat format );

        /** @brief 픽셀 하나(형식과 무관하게 RGBA). 범위 밖이면 0 이다. */
        Rgba8 getPixel( uint32 x, uint32 y ) const;
        /** @brief 픽셀 하나의 **저장된 그대로의** 바이트(`getBytesPerPixel` 개) — 형식을 풀지 않고 값이 같은지만 볼 때. 범위 밖이면 널. */
        const uint8* getRawPixel( uint32 x, uint32 y ) const;
        /** @brief 저장 형식의 픽셀당 바이트 수. */
        uint32 getBytesPerPixel() const { return sw::getRhiFormatBytesPerPixel( _format ); }

        uint32        getWidth() const { return _layout._width; }
        uint32        getHeight() const { return _layout._height; }
        uint32        getPixelCount() const { return _layout._width * _layout._height; }
        sw::RHIFormat getFormat() const { return _format; }

        /** @brief 두 색의 거리(R · G · B 차이의 절댓값 합). */
        static uint32 getColorDistance( const Rgba8& lhs, const Rgba8& rhs );

        /**
         * @brief 기본 클리어 색(0.02, 0.02, 0.05)이 톤매핑을 지나 놓이는 자리인가 — 아무것도 그려지지 않은 배경.
         * @details 그 색은 패스 리소스 · 톤매핑을 거쳐 대략 (31, 38, 46) 근처에 떨어진다. 백엔드마다 반올림이 조금씩 달라
         *          칸으로 본다(R 22~40 · G 28~48 · B 36~56).
         */
        static bool isDefaultClearBackground( const Rgba8& pixel );

    private:
        sw::vector<uint8>     _bytes;
        sw::RHITextureMipSpan _layout{};
        sw::RHIFormat         _format{ sw::RHIFormat::R8G8B8A8_UNORM };
    };
} // namespace test
