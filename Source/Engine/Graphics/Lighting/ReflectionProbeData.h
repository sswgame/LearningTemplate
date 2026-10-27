/**
 * @file ReflectionProbeData.h
 * @brief 반사 프로브 하나의 베이크 결과(`.reflprobe`)입니다. 찍은 위치에서 본 둘레의 빛을 옥타헤드럴 사각형 하나에 접고, 거칠기 단계마다 GGX 로 미리 거른 것입니다(결정 기록 5-5 P4).
 * @details 언리얼 Reflection Capture 와 같은 자리입니다. 큐브 텍스처 대신 옥타헤드럴 2D 를 쓰는 까닭은 네 백엔드가 큐브 바인딩을 모두 지원하지 않기 때문입니다(결정 기록 5-12 D2-12).
 *          단계마다 크기가 같다(밉처럼 줄이지 않는다) — 셰이더가 텍셀을 라이트 프로브 버퍼(t13)에서 수로 읽으므로 단계마다 크기가 같으면 번호 식이 하나다.
 *
 *          파일(리틀 엔디언): 매직 `SWRP`, 버전, 찍은 위치 float3, 한 변 텍셀 수 uint32, 단계 수 uint32, 텍셀마다 float32 RGB(단계 → 행 → 열).
 *          단계 l 의 거칠기는 l / (단계 수 − 1) 이다(단계 0 은 거르지 않은 빛).
 *
 *          옥타헤드럴 사상은 +Y 를 가운데로 접는다 — `encodeOctahedral` · `decodeOctahedral` 이 셰이더 lighting.hlsli 의 `ReflectionProbe::encodeOctahedral` 과 같은 식이다
 *          (식을 고치면 두 곳을 함께 고친다. ReflectionProbeDataTest 가 C++ 쪽 왕복을 본다).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Resource/ResourceID.h"

namespace sw
{
    /** @brief `.reflprobe` 하나의 내용입니다. 파일 머리말을 참고하세요. */
    struct SW_API ReflectionProbeData
    {
        static constexpr uint32      kMagic     = FourCcUtil::make( "SWRP" );
        static constexpr uint32      kVersion   = 1;
        static constexpr string_view kExtension = ".reflprobe";
        /// @brief 한 변 텍셀 수의 범위 — 깨진 머리말이 큰 할당을 일으키지 않게 막는다.
        static constexpr uint32 kMinResolution = 4;
        static constexpr uint32 kMaxResolution = 256;
        /// @brief 거칠기 단계 수의 범위 — 단계가 하나면 거칠기를 나타낼 수 없다.
        static constexpr uint32 kMinLevelCount = 2;
        static constexpr uint32 kMaxLevelCount = 8;

        float3         _capturePosition{}; ///< 찍은 월드 위치
        uint32         _resolution{ 0 };   ///< 옥타헤드럴 한 변 텍셀 수
        uint32         _levelCount{ 0 };   ///< 거칠기 단계 수
        vector<float3> _listTexel;         ///< 선형 RGB 빛, 단계 → 행 → 열(`computeTexelIndex`)

        /** @brief 단계 @p level 의 텍셀 (@p x, @p y) 번호입니다. */
        uint32 computeTexelIndex( uint32 level, uint32 x, uint32 y ) const { return ( level * _resolution + y ) * _resolution + x; }
        /** @brief 단계 @p level 의 거칠기(0 ~ 1)입니다. */
        float32 computeLevelRoughness( uint32 level ) const;

        /** @brief 범위와 텍셀 수가 맞는지 봅니다. */
        [[nodiscard]] bool isValid( string* pOutError = nullptr ) const;
        /** @brief 비웁니다. */
        void clear();

        /** @brief 단계 @p level 을 방향 @p direction(길이 1)으로 쌍선형 샘플합니다. 사각형 가장자리는 자른다(접힌 이웃을 읽지 않는다 — 셰이더와 같다). */
        float3 sampleLevel( uint32 level, const float3& direction ) const;

        /** @brief 바이트로 만듭니다(리틀 엔디언). */
        void makeBytes( vector<uint8>& outBytes ) const;
        /** @brief 바이트를 읽습니다. 매직, 버전, 길이, 범위가 틀리면 false 이고 내용을 비웁니다. */
        [[nodiscard]] bool readFromBytes( const uint8* pData, size_t size, string* pOutError = nullptr );
        /** @brief 파일로 씁니다. 부모 폴더가 없으면 만듭니다. */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief 리소스 경로(또는 절대 경로)에서 읽습니다. */
        [[nodiscard]] bool loadFromResource( const ResourceID& resourceID, string* pOutError = nullptr );

        /** @brief 길이 1 의 방향을 옥타헤드럴 사각형의 UV([0, 1]²)로 접습니다(+Y 가 가운데). */
        static float2 encodeOctahedral( const float3& direction );
        /** @brief 옥타헤드럴 UV([0, 1]²)를 길이 1 의 방향으로 폅니다. */
        static float3 decodeOctahedral( const float2& uv );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ReflectionProbeFace
     * @brief 찍은 면 하나(90° 정사각 원근)입니다. 픽셀은 선형 RGB, 행 0 이 위입니다. 그린 카메라의 뷰-투영(행벡터 규약)으로 방향을 픽셀에 되짚습니다.
     * @details 면의 방향 규약(오른쪽 · 위)을 따로 적지 않는다 — 그린 카메라의 행렬을 그대로 들고 다녀서, 카메라 규약이 바뀌어도 접기가 따라간다.
     */
    struct SW_API ReflectionProbeFace
    {
        vector<float3> _listPixel; ///< 행 0 이 위
        uint32         _size{ 0 }; ///< 한 변 픽셀 수
        float4x4       _viewProj{};
        float3         _forward{}; ///< 그 카메라가 보는 방향(길이 1) — 방향마다 면을 고를 때 쓴다
    };
} // namespace sw

namespace sw
{
    /** @brief 큐브 면 여섯을 반사 프로브 데이터로 접고 거칠기 단계마다 거르는 CPU 계산입니다(베이크 도구가 GPU 로 찍은 면을 넘긴다). */
    struct SW_API ReflectionProbeFilter
    {
        /** @brief 큐브 면 수입니다. */
        static constexpr uint32 kFaceCount = 6;

        /**
         * @brief 면 여섯을 접어 단계 0 을 만들고, 단계 1 부터는 GGX 중요도 표본 @p sampleCount 개로 거릅니다(언리얼 · 카리스 2013 의 "N = V = R" 근사).
         * @param capturePosition 찍은 위치(면 카메라의 위치와 같다)
         * @return 면이 비었거나 크기가 다르거나 범위 밖이면 false 이고 이유를 @p pOutError 에 적습니다.
         */
        [[nodiscard]] static bool makeFromFaces( const float3& capturePosition, const ReflectionProbeFace* pArrFace, uint32 resolution, uint32 levelCount,
                                                 uint32 sampleCount, ReflectionProbeData& outData, string* pOutError = nullptr );
    };
} // namespace sw
