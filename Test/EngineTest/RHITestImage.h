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
     * @brief 두 이미지를 화소마다 견준 결과입니다(`RHITestImage::compare`).
     * @details GPU 테스트가 두 판(켬 · 끔, 직렬 · 병렬, 백엔드 · 기준)을 견줄 때의 형식이다. 평균 색 · 평균 밝기의 차이는 조명 식 · 톤맵이 바뀔
     *          때마다 여유가 흔들리고(평균 (R − B) 차이 16 → 15 로 진 `PostProcessOffViewSkipsThePostChain`), 넓은 영역의 변화와 좁은 영역의 큰
     *          변화를 구분하지 못한다. "몇 화소가 얼마나 달라졌나" 는 바뀐 자리의 넓이를 직접 센다.
     */
    struct RHITestImageDifference
    {
        uint32 _pixelCount{ 0 };     ///< 견준 화소 수 — 0 이면 크기가 달랐거나 빈 이미지라 견주지 못했다
        uint32 _differingCount{ 0 }; ///< 색 거리(`RHITestImage::getColorDistance`)가 기준 이상인 화소 수
        uint32 _brighterCount{ 0 };  ///< 그 가운데 둘째 이미지가 더 밝은(R + G + B 가 큰) 화소 수
        uint32 _maxDistance{ 0 };    ///< 가장 큰 색 거리

        /** @brief 크기가 같은 두 이미지를 견줬는가. */
        bool isCompared() const { return _pixelCount > 0; }
        /** @brief 견줬고, 다른 화소가 전체의 @p percent % 이하인가. */
        bool isDifferingAtMost( uint32 percent ) const { return isCompared() && _differingCount * 100u <= _pixelCount * percent; }
        /** @brief 실패 메시지에 붙일 요약입니다. */
        sw::string describe() const;
    };
} // namespace test

namespace test
{
    /**
     * @brief 되읽은 이미지 한 장(`RenderPassGPUTest`).
     * @details `getPixel` 이 RGBA 로 준다 — 되읽기 · 행 포인터 계산 · BGRA 뒤집기를 케이스마다 손으로 들면, 백엔드마다 스왑체인 형식이
     *          달라 뒤집기를 한 곳이라도 빠뜨릴 때 그 백엔드에서만 빨강과 파랑이 바뀐다.
     *          반정밀도 첨부(`R16G16B16A16_FLOAT`)는 [0,1] 로 잘라 0~255 로 환산한다 — 두 판을 같은 규칙으로 비교하는 데 쓴다.
     */
    class RHITestImage
    {
    public:
        /** @brief 프레임 렌더러의 transient 첨부(예: `"SceneColor"`)를 되읽습니다. 실패하면 false 이고 이미지는 빈다. */
        [[nodiscard]] bool readTransient( sw::FrameRenderer& renderer, sw::string_view attachmentName );
        /**
         * @brief 프레임 렌더러의 Present 캡처(화면에 나간 그림, `constant::kBackBufferFormat`)를 되읽습니다. 캡처가 꺼져 있거나 실패하면 false 이고 이미지는 빈다.
         * @details Present 캡처를 바이트 배열로 들고 채널마다 견주면 알파 · 행 끝 여백까지 세고 형식을 케이스가 알아야 한다. 두 판은 이 이미지로 읽어
         *          `compare` 로 견준다.
         */
        [[nodiscard]] bool readPresent( sw::FrameRenderer& renderer );
        /**
         * @brief 이미 가진 바이트를 이미지로 삼습니다 — 되읽기 없이 해석 규칙(BGRA 뒤집기 · 반정밀도)을 테스트할 때.
         * @details 네 백엔드가 `SceneColor` 를 전부 RGBA8 로 되읽는 기계에서는 BGRA 뒤집기를 틀려도 GPU 케이스가 하나도 지지
         *          않는다. 그 규칙은 `RHITestImageTest` 가 GPU 없이 지킨다.
         */
        void assign( sw::vector<uint8> bytes, const sw::RHITextureMipSpan& layout, sw::RHIFormat format );

        /** @brief 픽셀 하나(형식과 무관하게 RGBA). 범위 밖이면 0 이다. */
        Rgba8 getPixel( uint32 x, uint32 y ) const;
        /** @brief 픽셀 하나의 **저장된 그대로의** 바이트(`getBytesPerPixel` 개) — 형식을 풀지 않고 값이 같은지만 볼 때. 범위 밖이면 널. */
        const uint8* getRawPixel( uint32 x, uint32 y ) const;
        /** @brief 저장 형식의 픽셀당 바이트 수. */
        uint32 getBytesPerPixel() const { return sw::getRHIFormatBytesPerPixel( _format ); }

        uint32        getWidth() const { return _layout._width; }
        uint32        getHeight() const { return _layout._height; }
        uint32        getPixelCount() const { return _layout._width * _layout._height; }
        sw::RHIFormat getFormat() const { return _format; }

        /**
         * @brief 첫 화소(배경)와 색(R · G · B)이 다른 화소 수입니다. 0 이면 배경뿐이라 두 판을 견줘도 뜻이 없다.
         * @details "같은 그림" 의 허용을 화면 전체가 아니라 이 수에 비례해 둔다 — 그려진 것이 화면의 1 % 도 안 되는 그림(입자)에 화면 1 % 의
         *          허용을 주면 그려진 것이 모두 달라도 통과한다.
         */
        uint32 countForegroundPixels() const;

        /** @brief 두 색의 거리(R · G · B 차이의 절댓값 합). */
        static uint32 getColorDistance( const Rgba8& lhs, const Rgba8& rhs );

        /**
         * @brief 두 이미지를 화소마다 견줍니다 — 색 거리가 @p minDistance 이상인 화소를 다르다고 셉니다. 크기가 다르면 `_pixelCount` 가 0 입니다.
         * @details @p minDistance 는 같은 경로의 반올림 흔들림(1~2)을 넘을 만큼만 둔다. 두 판을 견주는 판정은 평균이 아니라 이 결과로 한다.
         */
        static RHITestImageDifference compare( const RHITestImage& lhs, const RHITestImage& rhs, uint32 minDistance );
        /** @brief `compare` 를 사각형 [@p left, @p right) × [@p top, @p bottom) 안에서만 합니다. 사각형이 이미지 밖으로 나가면 `_pixelCount` 가 0 입니다. */
        static RHITestImageDifference compareRegion( const RHITestImage& lhs, const RHITestImage& rhs, uint32 minDistance, uint32 left, uint32 top, uint32 right,
                                                     uint32 bottom );

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
