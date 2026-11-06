/**
 * @file INavMeshDebug.h
 * @brief 내비메시를 눈으로 보는 쪽 인터페이스입니다. 선과 삼각형을 낼 뿐 내비메시를 바꾸지 않습니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
    class IPhysicsDebugRenderer;

    /** @brief 디버그 그리기 입력을 냅니다. */
    class INavMeshDebug
    {
    public:
        virtual ~INavMeshDebug() = default;

        /** @brief 폴리곤 테두리와 오프 메시 링크(활 모양, 막힌 링크는 빨강)를 선으로 냅니다. */
        virtual void drawDebug( IPhysicsDebugRenderer& renderer, const float4& color ) const = 0;
        /**
         * @brief 걷는 면을 삼각형으로 냅니다(높이 디테일까지 반영하므로 바닥에 붙습니다). 삼각형마다 점 셋을 @p outListCorner 에, 그 폴리곤의 영역 번호를
         *        @p outListArea 에 하나씩 추가합니다. 그리는 쪽(디버그 메시)이 색을 고릅니다.
         */
        virtual void collectDebugTriangles( vector<float3>& outListCorner, vector<uint8>& outListArea ) const = 0;
    };
} // namespace sw
