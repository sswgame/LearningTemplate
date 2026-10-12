/**
 * @file MeshVertexAnimationBaker.h
 * @brief 정점 애니메이션 표(`MeshVertexAnimation`) 굽기 — 스킨드 메시 + 스켈레톤 + 클립을 프레임 × 정점 표로 미리 스키닝합니다.
 * @details 표 형식은 Graphics(`MeshVertexAnimation`)가, 포즈 샘플은 Animation 이 맡습니다. 둘을 다 아는 컴포넌트 층(Object/SceneAnimation)에 둡니다 —
 *          언리얼 AnimToTexture 가 애니메이션과 렌더 모듈을 함께 드는 플러그인인 것과 같은 자리입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct MeshVertexAnimation;

    class AnimationClip;
    class Mesh;
    class Skeleton;
} // namespace sw

namespace sw
{
    /**
     * @struct MeshVertexAnimationBaker
     * @brief 스킨드 메시 + 스켈레톤 + 클립을 표로 굽습니다. 런타임이 처음 쓸 때 굽고(군중 시스템의 캐시), 같은 함수가 쿠킹의 자리입니다.
     * @details 프레임마다 클립을 샘플해(`AnimationClip::samplePose` — 애니메이터와 같은 길) 모델 공간 · 팔레트를 구하고, 정점을 meshskin.hlsl 과
     *          같은 식(가중치 넷의 선형 섞기, 노멀 정규화)으로 CPU 에서 스키닝합니다.
     */
    struct SW_API MeshVertexAnimationBaker
    {
        /**
         * @brief 굽습니다. 메시에 스킨이 없거나 본 수가 스켈레톤과 다르면 false 입니다.
         * @param framesPerSecond 표의 프레임율(먼 군중이라 낮게 — 15 정도).
         * @param bAnchorRootMotion 클립의 루트 모션 트랙을 시작 자리에 묶습니다(루트 모션을 오브젝트가 맡는 애니메이터와 같은 모양).
         */
        [[nodiscard]] static bool bake( const Mesh& skinMesh, const Skeleton& skeleton, const AnimationClip& clip, float32 framesPerSecond, bool bAnchorRootMotion,
                                        MeshVertexAnimation& outAnimation );
        /** @brief 팔레트로 메시 정점을 스키닝합니다(meshskin.hlsl 과 같은 식). 위치 · 노멀을 정점 순서로 냅니다. */
        static void skinVertices( const Mesh& skinMesh, const vector<float4x4>& listPalette, vector<float3>& outListPosition, vector<float3>& outListNormal );
    };
} // namespace sw
