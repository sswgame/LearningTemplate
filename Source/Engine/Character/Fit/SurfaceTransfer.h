/**
 * @file SurfaceTransfer.h
 * @brief 몸 → 장비 전이(가장 가까운 삼각형의 무게중심 좌표 + 법선 방향 거리)입니다. 체형 모프 델타와 스킨 가중치를 옮기고, 표면 소켓이 체형을 따르게 합니다.
 * @details 장비는 기본 체형에 한 번 만들고, 쿠킹 때 정점마다 몸의 가장 가까운 삼각형을 찾아 묶습니다(`bindPoints`). 그 묶음으로 몸의 모프
 *          델타 · 스킨 가중치를 장비로 옮기면(`transferMorphs` · `transferSkinWeights`) 런타임은 몸과 같은 체형 가중치를 장비에도 겁니다.
 *          참고: 언리얼 Mutable Skin Weights 전이, Character Creator 스마트 핏, Daz 오토핏.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct AppearanceGeometry;

    class SurfaceBvh;

    /** @brief 한 점이 원본 표면에 묶인 자리입니다. */
    struct SurfaceBinding
    {
        uint32  _triangle{ 0 };        ///< 원본 삼각형 번호
        float32 _baryU{ 0.0f };        ///< 무게중심 좌표(두 번째 정점 몫)
        float32 _baryV{ 0.0f };        ///< 무게중심 좌표(세 번째 정점 몫)
        float32 _normalOffset{ 0.0f }; ///< 보간 법선 방향 거리
        uint8   _bBound{ SW_FALSE };   ///< 거리 안에서 표면을 찾았는가
    };
} // namespace sw

namespace sw
{
    /** @brief 표면 전이의 함수 모음입니다(전부 static). */
    struct SW_API SurfaceTransferUtil
    {
        /**
         * @brief 점마다 원본(@p source)의 가장 가까운 삼각형을 찾아 묶습니다(@p maxDistance 밖은 묶지 않음). @p sourceBvh 는 원본 하나로 지은 것입니다.
         */
        static void bindPoints( const AppearanceGeometry& source, const SurfaceBvh& sourceBvh, vector_reference<const float3> listPoint, float32 maxDistance,
                                vector<SurfaceBinding>& outListBinding );
        /** @brief 묶음의 자리를 원본(@p source — 모프를 건 원본이어도 같은 위상)의 지금 위치로 계산합니다. */
        static float3 evaluatePoint( const AppearanceGeometry& source, const SurfaceBinding& binding );
        /**
         * @brief 원본의 모프 대상을 장비로 옮깁니다(같은 이름은 바꿈). 묶이지 않은 정점의 델타는 0 입니다.
         * @param listBinding @p inoutTarget 의 정점마다 하나입니다.
         */
        static void transferMorphs( const AppearanceGeometry& source, vector_reference<const SurfaceBinding> listBinding, AppearanceGeometry& inoutTarget );
        /** @brief 원본의 스킨 가중치를 장비로 옮깁니다 — 세 정점 가중치를 무게중심으로 섞어 큰 네 개를 남기고 합을 1 로 맞춥니다. */
        static void transferSkinWeights( const AppearanceGeometry& source, vector_reference<const SurfaceBinding> listBinding, AppearanceGeometry& inoutTarget );
    };
} // namespace sw
