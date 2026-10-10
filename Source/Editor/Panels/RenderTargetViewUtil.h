/**
 * @file RenderTargetViewUtil.h
 * @brief 렌더 타깃 창의 보기 계산입니다 — CPU 로 읽은 픽셀을 값으로 풀고, 채널 · 깊이 선형화를 거쳐 미리보기 RGBA8 을 만듭니다(ImGui 없음, EditorTest 가 본다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw::editor
{
    /** @brief 미리보기에 보일 채널입니다. 한 채널만 고르면 그 값을 회색으로 보입니다. */
    enum class RenderTargetChannel : uint8
    {
        RGBA = 0, ///< 색 그대로(알파는 무시)
        Red,
        Green,
        Blue,
        Alpha,
        Count,
    };

    /** @brief 미리보기 계산의 입력입니다. */
    struct RenderTargetViewSettings
    {
        float32             _nearPlane{ 0.1f };                    ///< 깊이 선형화의 가까운 면(월드 단위)
        float32             _farPlane{ 100.0f };                   ///< 깊이 선형화의 먼 면(월드 단위)
        RenderTargetChannel _channel{ RenderTargetChannel::RGBA }; ///< 보일 채널
        uint8               _bLinearizeDepth{ SW_TRUE };           ///< 깊이를 원근 투영으로 보고 거리로 되돌린다
        uint8               _bAutoDepthRange{ SW_TRUE };           ///< 깊이의 회색 범위를 화면에 있는 값(배경 제외)의 최소 ~ 최대로 맞춘다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 미리보기 이미지와 그때 쓴 깊이 범위입니다. */
    struct RenderTargetPreviewImage
    {
        vector<uint8> _byte; ///< RGBA8, 행은 빈틈없이
        uint32        _width{ 0 };
        uint32        _height{ 0 };
        float32       _depthLow{ 0.0f };  ///< 회색 0 이 뜻하는 깊이(선형화했으면 거리)
        float32       _depthHigh{ 1.0f }; ///< 회색 1 이 뜻하는 깊이
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct RenderTargetViewUtil
     * @brief 렌더 타깃 창이 쓰는 계산 모음입니다(언리얼 Buffer Visualization · 유니티 Frame Debugger 의 타깃 보기).
     * @details 입력 바이트는 `IRHIResourceFactory::readbackTexture2D` 의 배치입니다. 깊이 타깃은 그 함수가 `kRHIDepthReadbackFormat`
     *          (0..1 float32)으로 채우므로 @p bDepth 로 알립니다.
     */
    struct RenderTargetViewUtil
    {
        /** @brief CPU 로 읽은 값을 풀 수 있는 포맷이면 true 입니다(블록 압축 · Unknown 은 아님). 깊이는 언제나 true 입니다. */
        static bool isDecodableFormat( RHIFormat format, bool bDepth );
        /** @brief 포맷이 가진 채널 수입니다(깊이는 1). 픽셀 값 줄에 몇 개를 적을지 정합니다. */
        static uint32 getChannelCount( RHIFormat format, bool bDepth );

        /**
         * @brief 픽셀 (@p x, @p y) 의 값을 float4 로 풉니다. 없는 채널은 0, 없는 알파는 1 입니다.
         * @return 포맷을 풀 수 없거나 좌표 · 바이트가 범위 밖이면 false.
         */
        [[nodiscard]] static bool readPixel( const vector<uint8>& byte, const RHITextureMipSpan& layout, RHIFormat format, bool bDepth, uint32 x, uint32 y,
                                             float4& outValue );

        /** @brief 원근 투영 깊이(0 = 가까운 면, 1 = 먼 면)를 시점에서의 거리로 되돌립니다. 면 값이 잘못됐으면 깊이를 그대로 돌려줍니다. */
        static float32 linearizeDepth( float32 depth, float32 nearPlane, float32 farPlane );

        /**
         * @brief 읽은 픽셀로 미리보기 RGBA8 을 만듭니다. 깊이는 선형화 · 범위를 거쳐 회색, 한 채널은 회색, RGBA 는 [0,1] 로 자른 색입니다.
         * @details 자동 범위는 지운 값(깊이 1 = 배경)을 빼고 잡습니다 — 배경까지 넣으면 하늘이 범위를 먹어 장면이 한 색으로 뭉갠다. 배경은 흰색입니다.
         * @return 포맷을 풀 수 없거나 바이트가 모자라면 false.
         */
        [[nodiscard]] static bool makePreviewImage( const vector<uint8>& byte, const RHITextureMipSpan& layout, RHIFormat format, bool bDepth,
                                                    const RenderTargetViewSettings& settings, RenderTargetPreviewImage& outImage );

        /**
         * @brief 미리보기 안의 점(@p localPos, 그린 그림의 왼쪽 위 기준 화면 픽셀)이 가리키는 텍셀을 구합니다.
         * @return 점이 그림 밖이거나 크기가 0 이면 false.
         */
        [[nodiscard]] static bool mapPreviewToTexel( const float2& localPos, const float2& drawSize, uint32 width, uint32 height, int2& outTexel );

        /** @brief 채널 단추에 적을 이름입니다("RGBA" · "R" · "G" · "B" · "A"). */
        static const utf8* getChannelLabel( RenderTargetChannel channel );
    };
} // namespace sw::editor
