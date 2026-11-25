/**
 * @file ReflectionProbeAsset.h
 * @brief 반사 프로브 하나의 베이크 결과(`.reflprobe`)입니다. 찍은 위치에서 본 둘레의 빛을 옥타헤드럴 사각형 하나에 접고, 거칠기 단계마다 GGX 로 미리 거른 것입니다.
 * @details 언리얼 Reflection Capture 와 같은 자리입니다. 큐브 텍스처 대신 옥타헤드럴 2D 를 쓰는 까닭은 네 백엔드가 큐브 바인딩을 모두 지원하지 않기 때문입니다.
 *          단계마다 크기가 같다(밉처럼 줄이지 않는다) — 셰이더가 텍셀을 라이트 프로브 버퍼(t13)에서 수로 읽으므로 단계마다 크기가 같으면 번호 식이 하나다.
 *
 *          파일(리틀 엔디언): 매직 `SWRP`, 버전, 찍은 위치 float3, 한 변 텍셀 수 uint32, 단계 수 uint32, 텍셀마다 float32 RGB(단계 → 행 → 열).
 *          단계 l 의 거칠기는 l / (단계 수 − 1) 이다(단계 0 은 거르지 않은 빛).
 *
 *          옥타헤드럴 사상은 +Y 를 가운데로 접는다 — `encodeOctahedral` · `decodeOctahedral` 이 셰이더 lighting.hlsli 의 `ReflectionProbe::encodeOctahedral` 과 같은 식이다
 *          (식을 고치면 두 곳을 함께 고친다. ReflectionProbeAssetTest 가 C++ 쪽 왕복을 본다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Content/AssetID.h"

namespace sw
{
    struct ReflectionProbeFace;

    /**
     * @struct OctahedralRadianceMap
     * @brief GGX 거르기의 원본입니다 — 옥타헤드럴 빛 한 장(밉 0)과 그것을 2×2 로 줄여 간 밉 사슬입니다(`.radiancemap` 은 밉 0 만 든다).
     * @details filtered importance sampling(Colbert-Krivanek, GPU Gems 3 20 장 · Karis 2013)의 원본 밉 사슬이다 — 표본마다 pdf 가 덮는 입체각에 맞는 밉을 읽어
     *          적은 표본으로도 잡음 없이 거른다(`ReflectionProbeAsset::filterRoughnessLevels`). 언리얼 · 유니티의 큐브맵 밉 사슬 자리이고, 큐브 대신 옥타헤드럴인 까닭은
     *          `ReflectionProbeAsset` 과 같다. 옥타헤드럴 텍셀 (x, y) 의 자식은 한 단계 위 밉의 (2x .. 2x+1, 2y .. 2y+1) 이라 상자 평균이 같은 방향 묶음이다.
     *
     *          파일(리틀 엔디언): 매직 `SWRM`, 버전, 한 변 텍셀 수 uint32, 텍셀마다 float32 RGB(행 → 열). HDRI 하늘의 원본이고 `Scripts/dev/make_sky_hdri_prefabs.py` 가 쓴다.
     */
    struct SW_API OctahedralRadianceMap
    {
        static constexpr string_view kExtension = ".radiancemap";
        /// @brief 밉 0 한 변 텍셀 수의 범위 — 깨진 머리말이 큰 할당을 일으키지 않게 막는다.
        static constexpr uint32 kMinResolution = 4;
        static constexpr uint32 kMaxResolution = 1024;

        /** @brief 밉 하나입니다. */
        struct Mip
        {
            uint32         _resolution{ 0 }; ///< 한 변 텍셀 수
            vector<float3> _listTexel;       ///< 선형 RGB 빛, 행 → 열
        };
        vector<Mip> _listMip; ///< 밉 0 이 원본, 뒤로 갈수록 한 변이 반

        /** @brief 비웁니다. */
        void clear() { _listMip.clear(); }
        /** @brief 밉 수입니다(비었으면 0). */
        uint32 getMipCount() const { return static_cast<uint32>( _listMip.size() ); }
        /** @brief 밉 0 의 한 변 텍셀 수입니다(비었으면 0). */
        uint32 getResolution() const { return _listMip.empty() ? 0u : _listMip[0]._resolution; }

        /** @brief 밉 0 을 @p listTexel(한 변 @p resolution, 행 → 열)로 정하고 밉 사슬을 비웁니다. 크기가 범위 밖이거나 텍셀 수가 틀리면 그 까닭이고 비운다. */
        [[nodiscard]] expected<void> assignBase( uint32 resolution, vector<float3>&& listTexel );
        /**
         * @brief 밉 0 을 2×2 입체각 가중 평균으로 줄여 밉을 @p mipCount 개까지 만듭니다(밉 0 포함). 한 변이 홀수이거나 1 이면 거기서 멈춘다 — 텍스처 밉 수가 크기로 막히는 것과 같다.
         * @return 밉 0 이 없거나 @p mipCount 가 0 이면 그 까닭입니다.
         */
        [[nodiscard]] expected<void> buildMips( uint32 mipCount );
        /** @brief 방향 @p direction(길이 1)을 밉 @p lod(소수 — 두 밉 사이를 섞는다, 범위 밖은 자른다)로 쌍선형 샘플합니다. 비었으면 0 입니다. */
        float3 sample( const float3& direction, float32 lod ) const;
        /** @brief 한 변 @p resolution 의 텍셀 (@p x, @p y) 중심이 가리키는 방향(길이 1)입니다. */
        static float3 computeTexelDirection( uint32 resolution, uint32 x, uint32 y );

        /** @brief 밉 0 을 바이트로 만듭니다(리틀 엔디언). */
        void makeBytes( vector<uint8>& outBytes ) const;
        /** @brief 바이트를 밉 0 으로 읽습니다(밉 사슬은 `buildMips`). 매직, 버전, 길이, 범위가 틀리면 그 까닭이고 비운다. */
        [[nodiscard]] expected<void> readFromBytes( const uint8* pData, size_t size );
        /** @brief 리소스 경로에서 읽습니다. 못 읽거나 틀리면 그 까닭입니다. */
        [[nodiscard]] expected<void> loadFromAsset( const AssetID& assetID );
    };

    /** @brief `.reflprobe` 하나의 내용입니다. 파일 머리말을 참고하세요. */
    struct SW_API ReflectionProbeAsset
    {
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

        /** @brief 범위와 텍셀 수가 맞는지 봅니다. 틀리면 그 까닭입니다. */
        [[nodiscard]] expected<void> validate() const;
        /** @brief 비웁니다. */
        void clear();

        /** @brief 단계 @p level 을 방향 @p direction(길이 1)으로 쌍선형 샘플합니다. 사각형 가장자리는 자른다(접힌 이웃을 읽지 않는다 — 셰이더와 같다). */
        float3 sampleLevel( uint32 level, const float3& direction ) const;

        /**
         * @brief 한 변 @p resolution · 단계 @p levelCount 의 텍셀을 0 으로 잡습니다. 범위 밖이면 그 까닭이고 내용을 비웁니다.
         * @details 단계 0 을 채우는 쪽이 둘이다 — 찍은 면(`makeFromFaces`)과 식으로 세는 하늘(`SkyLightData::make`). 둘 다 이것으로 잡고 `computeTexelDirection` 으로 단계 0 을 채운 뒤
         *          `filterRoughnessLevels` 로 거른다(하늘은 원본 `OctahedralRadianceMap` 을 따로 만들어 넘긴다 — HDRI 원본이 이 데이터보다 크다).
         */
        [[nodiscard]] expected<void> allocate( const float3& capturePosition, uint32 resolution, uint32 levelCount );
        /** @brief 텍셀 (@p x, @p y) 의 중심이 가리키는 방향(길이 1)입니다 — 단계마다 같다. */
        float3 computeTexelDirection( uint32 x, uint32 y ) const;
        /**
         * @brief 원본 @p source 를 걸러 모든 단계를 채웁니다 — 반사 프로브 베이크와 스카이 라이트가 함께 쓰는 거르기 하나입니다.
         * @details 단계 0 은 텍셀 중심 방향의 원본(원본이 이 데이터보다 크면 크기가 맞는 밉)이다. 단계 1 부터는 GGX 중요도 표본 @p sampleCount 개의 filtered importance
         *          sampling 이다(언리얼 · 카리스 2013 의 "N = V = R" 근사, 밉 고르기는 Colbert-Krivanek): 표본마다 pdf 가 덮는 입체각 1 / (표본 수 × pdf) 와 원본 텍셀의
         *          입체각 4π / 한 변² 의 비로 밉을 고른다. 원본 밉이 하나면 밉 0 만 읽는 보통 중요도 표본이다(기준 적분 — 표본을 늘리면 수렴한다).
         *          텍셀 행마다 독립이라 작업 풀로 나눈다(`engine::runParallel` — 서비스가 없는 테스트 · 도구는 부른 스레드가 돈다).
         * @return 크기가 범위 밖이거나 텍셀 수가 맞지 않거나 원본이 비었거나 표본이 0 이면 그 까닭입니다.
         */
        [[nodiscard]] expected<void> filterRoughnessLevels( const OctahedralRadianceMap& source, uint32 sampleCount );
        /**
         * @brief 채워 둔 단계 0 을 원본으로 삼아(밉 @p mipCount 개까지 — `OctahedralRadianceMap::buildMips`) 단계 1 부터를 거릅니다.
         * @return 위의 거르기가 거절하는 입력이거나 @p mipCount 가 0 이면 그 까닭입니다.
         */
        [[nodiscard]] expected<void> filterRoughnessLevels( uint32 sampleCount, uint32 mipCount );

        /** @brief 바이트로 만듭니다(리틀 엔디언). */
        void makeBytes( vector<uint8>& outBytes ) const;
        /** @brief 바이트를 읽습니다. 매직, 버전, 길이, 범위가 틀리면 그 까닭이고 내용을 비웁니다. */
        [[nodiscard]] expected<void> readFromBytes( const uint8* pData, size_t size );
        /** @brief 파일로 씁니다. 부모 폴더가 없으면 만듭니다. */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;
        /** @brief 리소스 경로(또는 절대 경로)에서 읽습니다. 못 읽거나 틀리면 그 까닭입니다. */
        [[nodiscard]] expected<void> loadFromAsset( const AssetID& assetID );

        /** @brief 길이 1 의 방향을 옥타헤드럴 사각형의 UV([0, 1]²)로 접습니다(+Y 가 가운데). */
        static float2 encodeOctahedral( const float3& direction );
        /** @brief 옥타헤드럴 UV([0, 1]²)를 길이 1 의 방향으로 폅니다. */
        static float3 decodeOctahedral( const float2& uv );
        /**
         * @brief 시차 보정입니다. 상자 안의 @p worldPosition 에서 반사 방향 @p reflectDirection(길이 1)으로 상자 벽에 닿는 점을 찍은 위치 @p capturePosition 에서 보는 방향(길이 1)을 돌려줍니다.
         * @details 셰이더 `LightData::sampleReflectionProbe`(lighting.hlsli)와 같은 식이다. 프로브는 찍은 곳에서 본 둘레이므로 그대로 읽으면 찍은 곳에서 먼 표면일수록 가까운 벽의 반사가 어긋난다.
         */
        static float3 computeParallaxDirection( const float3& worldPosition, const float3& reflectDirection, const float3& boxCenter, const float3& boxHalfExtent, const float3& capturePosition );

        // 큐브 면 여섯을 반사 프로브 데이터로 접고 거칠기 단계마다 거르는 CPU 계산입니다(베이크 도구가 GPU 로 찍은 면을 넘긴다). */
        /** @brief 큐브 면 수입니다. */
        static constexpr uint32 kFaceCount = 6;

        /**
         * @brief 면 여섯을 접어 단계 0 을 만들고, 단계 1 부터는 단계 0 의 밉 @p mipCount 개를 원본으로 GGX 표본 @p sampleCount 개로 거릅니다(`filterRoughnessLevels`).
         * @param capturePosition 찍은 위치(면 카메라의 위치와 같다)
         * @return 면이 비었거나 크기가 다르거나 범위 밖이면 그 까닭입니다.
         */
        [[nodiscard]] static expected<void> makeFromFaces( const float3& capturePosition, const ReflectionProbeFace* pArrFace, uint32 resolution, uint32 levelCount,
                                                           uint32 sampleCount, uint32 mipCount, ReflectionProbeAsset& outData );
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
