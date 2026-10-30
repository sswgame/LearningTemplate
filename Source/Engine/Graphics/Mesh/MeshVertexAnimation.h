/**
 * @file MeshVertexAnimation.h
 * @brief 정점 애니메이션(VAT, Vertex Animation Texture)입니다. 스킨드 메시에 클립 하나를 미리 스키닝해 프레임 × 정점 테이블로 베이크한 것입니다.
 * @details 먼 군중은 CPU 포즈도 GPU 스키닝도 없이 이 테이블을 정점 셰이더가 인스턴스마다 다른 시각으로 읽어 그립니다(언리얼의 AnimToTexture,
 *          유니티의 VAT 셰이더에 해당합니다). 이름은 "텍스처" 이지만 이 엔진은 정점 셰이더가 구조 버퍼를 `SV_VertexID` 로 읽습니다(모프 풀과 같은 경로).
 *          그래서 테이블을 모프 결과 버퍼의 맨 앞 구간에 담습니다. 텍스처 바인딩이 새로 필요하지 않고 네 백엔드가 같은 경로를 탑니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/hashed_string.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"

namespace sw
{
    /**
     * @struct MeshVertexAnimationElement
     * @brief 정점 하나의 한 프레임입니다(8 B). 위치는 표의 경계(`MeshVertexAnimation::_boundsCenter` · `_boundsHalfExtent`)로 정규화한 `snorm16` 셋이고,
     *        법선은 옥타헤드럴 좌표 `snorm8` 둘입니다. GPU 가 단어(uint32) 둘로 그대로 읽습니다(리틀 엔디언: 단어0 = x | y << 16, 단어1 = z | u << 16 | v << 24).
     */
    struct MeshVertexAnimationElement
    {
        int16 _x{ 0 };
        int16 _y{ 0 };
        int16 _z{ 0 };
        int8  _normalU{ 0 };
        int8  _normalV{ 0 };
    };
    static_assert( sizeof( MeshVertexAnimationElement ) == 8 && alignof( MeshVertexAnimationElement ) == 2, "VAT 원소는 8 바이트이며 셰이더가 단어 둘로 읽는다" );

    /**
     * @struct MeshVertexAnimation
     * @brief 베이크한 테이블입니다. 원소 하나는 정점 하나의 한 프레임이며 위치 `snorm16` 셋 + 옥타헤드럴 법선 `snorm8` 둘(8 B)입니다(`MeshVertexAnimationElement`).
     * @details 프레임 우선 순서입니다(`[frame * vertexCount + vertex]`). 반복 클립은 마지막 프레임 뒤에 첫 프레임이 이어지고(테이블에는 한 번만 저장),
     *          반복하지 않는 클립은 마지막 프레임에 멈춥니다. 셰이더(binding.hlsli `InstanceData::loadAnimatedVertex`)가 두 프레임 사이를 선형 보간합니다.
     *          위치는 모든 프레임의 경계 상자 안으로 정규화합니다(단위 오차는 반 크기의 1/32767 — 1 m 메시면 약 0.015 mm, 같은 크기 float4 표를 버린 대가로 16 B → 8 B).
     *          쿠킹본(`.vat`, 리틀 엔디언)은 매직 `SWVA`, 버전, 프레임 수, 정점 수, 프레임율, 길이, 플래그(비트 0 반복, 비트 1 루트 고정), 경계 중심 float3, 경계 반 크기 float3,
     *          원소 테이블 순서입니다. 지금 버전만 읽습니다(버전이 다르면 다시 쿠킹합니다).
     */
    struct SW_API MeshVertexAnimation
    {
        /** @brief 쿠킹본 확장자입니다. */
        static constexpr string_view kExtension = ".vat";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `MeshVertexAnimation.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "MeshVertexAnimation", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };
        /** @brief 쿠킹본 형식 버전입니다(2: 원소 16 B float4 → 8 B 압축, 머리에 경계 추가). */
        static constexpr uint32 kVersion = 2;

        vector<MeshVertexAnimationElement> _listFrameVertex;
        float3                             _boundsCenter{ 0.0f, 0.0f, 0.0f };
        float3                             _boundsHalfExtent{ 0.0f, 0.0f, 0.0f };
        uint32                             _frameCount{ 0 };
        uint32                             _vertexCount{ 0 };
        float32                            _framesPerSecond{ 15.0f };
        float32                            _duration{ 0.0f };
        uint8                              _bLoop{ SW_TRUE };
        uint8                              _bAnchorRootMotion{ SW_FALSE }; ///< 베이크할 때 루트 모션 본을 시작 위치에 고정했습니다(같은 조건의 유닛만 씁니다)

        /** @brief 테이블이 비었으면 true 입니다. */
        bool isEmpty() const { return _frameCount == 0 || _vertexCount == 0; }
        /**
         * @brief 프레임 × 정점 순서의 위치 · 법선(길이 `frameCount * vertexCount`)을 경계로 정규화해 `_listFrameVertex` 를 채웁니다.
         * @details `_frameCount` · `_vertexCount` 를 먼저 채워야 합니다. 경계는 이 호출이 모든 프레임에서 구합니다. 길이가 맞지 않으면 오류를 남기고 false 를 반환하며 비웁니다.
         */
        [[nodiscard]] bool quantize( const vector<float3>& listPosition, const vector<float3>& listNormal );
        /** @brief 원소의 위치를 풉니다(셰이더 `VertexAnimation::computePosition` 과 같은 식). */
        float3 unpackPosition( const MeshVertexAnimationElement& element ) const;
        /** @brief 원소의 법선을 단위 벡터로 풉니다(셰이더 `VertexAnimation::loadNormal` 과 같은 식). */
        static float3 unpackNormal( const MeshVertexAnimationElement& element );
        /** @brief 시각 @p time(초)의 정점 위치입니다. 셰이더와 같은 프레임 보간이며 테스트와 디버그에 씁니다. */
        float3 samplePosition( float32 time, uint32 vertexIndex ) const;

        /** @brief 쿠킹본 바이트로 만듭니다. */
        void makeBytes( vector<uint8>& outBytes ) const;
        /** @brief 쿠킹본 바이트를 읽습니다. 형식, 버전, 길이가 맞지 않으면 오류를 남기고 false 를 반환하며 비웁니다. */
        [[nodiscard]] bool readFromBytes( const uint8* pData, size_t byteCount, string_view sourceLabel );
        /** @brief 파일로 씁니다(부모 폴더를 만듭니다). */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;
        /** @brief 리소스 경로(팩, 느슨한 파일, 절대 경로)의 쿠킹본을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 메시 경로와 클립 이름에 해당하는 쿠킹본 경로입니다(`a/knight.mesh` + `Idle` → `a/knight.idle.vat`). 메시 경로가 `.mesh` 가 아니면 빈 문자열입니다. */
        static string makeCookedPath( string_view meshPath, const hashed_string& clipName );
    };
} // namespace sw
