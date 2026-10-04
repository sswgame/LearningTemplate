/**
 * @file MeshVertexAnimation.h
 * @brief 정점 애니메이션(VAT, Vertex Animation Texture) — 스킨드 메시에 클립 하나를 미리 스키닝해 프레임 × 정점 표로 구운 것과 그 굽기입니다.
 * @details 먼 군중은 CPU 포즈도 GPU 스키닝도 없이 이 표를 정점 셰이더가 인스턴스마다 다른 시각으로 읽어 그립니다(언리얼 AnimToTexture ·
 *          유니티 VAT 셰이더의 자리). 이름은 "텍스처" 지만 이 엔진은 정점 셰이더가 구조버퍼를 `SV_VertexID` 로 읽으므로(모프 풀과 같은 길)
 *          표를 모프 결과 버퍼의 맨 앞 구간에 싣습니다 — 텍스처 바인딩이 새로 필요 없고 네 백엔드가 같은 길을 탑니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class AnimClip;
    class Mesh;
    class Skeleton;

    /**
     * @struct MeshVertexAnimation
     * @brief 구운 표입니다. 원소 하나 = 정점 하나의 한 프레임 = float4(위치 xyz, 노멀을 정수로 담은 w — `packNormal`).
     * @details 프레임 우선 순서입니다(`[frame * vertexCount + vertex]`). 반복 클립은 마지막 프레임 뒤에 첫 프레임이 이어지고(표에는 한 번만),
     *          반복하지 않는 클립은 마지막 프레임에 멈춥니다. 셰이더(binding.hlsli `swLoadAnimatedVertex`)가 두 프레임 사이를 선형 보간합니다.
     *          쿠킹본(`.vat`, 리틀 엔디언): 매직 `SWVA` · 버전 · 프레임 수 · 정점 수 · 프레임율 · 길이 · 플래그(비트 0 반복, 비트 1 루트 묶기) · float4 표.
     *          지금 판만 읽습니다(판이 다르면 다시 쿠킹한다).
     */
    struct SW_API MeshVertexAnimation
    {
        /** @brief 쿠킹본 확장자입니다. */
        static constexpr string_view kExtension = ".vat";
        /** @brief 쿠킹본 형식 판입니다. */
        static constexpr uint32 kVersion = 1;

        vector<float4> _listFrameVertex;
        uint32         _frameCount{ 0 };
        uint32         _vertexCount{ 0 };
        float32        _framesPerSecond{ 15.0f };
        float32        _duration{ 0.0f };
        uint8          _bLoop{ SW_TRUE };
        uint8          _bAnchorRootMotion{ SW_FALSE }; ///< 굽을 때 루트 모션 본을 시작 자리에 묶었다(같은 조건의 유닛만 쓴다)

        /** @brief 표가 비었는지입니다. */
        bool isEmpty() const { return _frameCount == 0 || _vertexCount == 0; }
        /** @brief 단위 노멀을 정수 하나(팔면체 12 + 12 비트)로 담은 실수입니다. 2^24 아래 정수라 float32 에 정확히 들어갑니다. */
        static float32 packNormal( const float3& normal );
        /** @brief `packNormal` 의 역입니다(셰이더 `swUnpackVertexAnimationNormal` 과 같은 식). */
        static float3 unpackNormal( float32 packed );
        /** @brief 시각 @p time(초)의 정점 위치입니다 — 셰이더와 같은 프레임 보간입니다(시험 · 디버그). */
        float3 samplePosition( float32 time, uint32 vertexIndex ) const;

        /** @brief 쿠킹본 바이트로 만듭니다. */
        void makeBytes( vector<uint8>& outBytes ) const;
        /** @brief 쿠킹본 바이트를 읽습니다. 형식 · 판 · 길이가 맞지 않으면 오류를 남기고 false 이며 비웁니다. */
        [[nodiscard]] bool readFromBytes( const uint8* pData, size_t byteCount, string_view sourceLabel );
        /** @brief 파일로 씁니다(부모 폴더를 만듭니다). */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief 리소스 경로(팩 · 느슨한 파일 · 절대 경로)의 쿠킹본을 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief 메시 경로와 클립 이름의 쿠킹본 경로입니다(`a/knight.mesh` + `Idle` → `a/knight.idle.vat`). 메시 경로가 `.mesh` 가 아니면 빈 문자열입니다. */
        static string makeCookedPath( string_view meshPath, const hashed_string& clipName );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MeshVertexAnimationBaker
     * @brief 스킨드 메시 + 스켈레톤 + 클립을 표로 굽습니다. 런타임이 처음 쓸 때 굽고(군중 시스템의 캐시), 같은 함수가 쿠킹의 자리입니다.
     * @details 프레임마다 클립을 샘플해(`AnimClip::samplePose` — 애니메이터와 같은 길) 모델 공간 · 팔레트를 구하고, 정점을 meshskin.hlsl 과
     *          같은 식(가중치 넷의 선형 섞기, 노멀 정규화)으로 CPU 에서 스키닝합니다.
     */
    struct SW_API MeshVertexAnimationBaker
    {
        /**
         * @brief 굽습니다. 메시에 스킨이 없거나 본 수가 스켈레톤과 다르면 false 입니다.
         * @param framesPerSecond 표의 프레임율(먼 군중이라 낮게 — 15 정도).
         * @param bAnchorRootMotion 클립의 루트 모션 트랙을 시작 자리에 묶습니다(루트 모션을 오브젝트가 맡는 애니메이터와 같은 모양).
         */
        [[nodiscard]] static bool bake( const Mesh& skinMesh, const Skeleton& skeleton, const AnimClip& clip, float32 framesPerSecond, bool bAnchorRootMotion,
                                        MeshVertexAnimation& outAnimation );
        /** @brief 팔레트로 메시 정점을 스키닝합니다(meshskin.hlsl 과 같은 식). 위치 · 노멀을 정점 순서로 냅니다. */
        static void skinVertices( const Mesh& skinMesh, const vector<float4x4>& listPalette, vector<float3>& outListPosition, vector<float3>& outListNormal );
    };
} // namespace sw
