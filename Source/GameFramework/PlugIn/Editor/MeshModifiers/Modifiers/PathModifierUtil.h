/**
 * @file PathModifierUtil.h
 * @brief 경로(스플라인) · 대상 메시를 읽는 모디파이어(Path Deform · World Path Deform · Rope Deform · Conform · Conform Multi)가 함께 쓰는 도우미입니다.
 * @details 외부 입력은 월드 좌표라서 모디파이어 공간으로 옮겨 쓴다. 입력 창구가 곡선의 위치와 접선만 주므로 경로 프레임은 접선과 고정 기준 축으로 만든다
 *          (2D 경로 가정 — 접선이 기준 축과 나란해지면 다른 축으로 넘어간다).
 */
#pragma once
#include "Core/Math/MathUtil.h"

#include "Engine/Character/Fit/SurfaceBvh.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"

namespace sw::editor
{
    /** @brief 경로 · 표면 투영 도우미입니다. */
    struct PathModifierUtil
    {
        /** @brief 경로 표본을 모디파이어 공간으로 옮기는 방식입니다. */
        enum class PathSpace : uint8
        {
            PathLocal, ///< 경로 오브젝트 자신의 공간 좌표를 모디파이어 공간 좌표로 본다(메시는 제자리에서 경로 모양으로 휜다)
            World,     ///< 월드 좌표를 모디파이어 공간으로 옮긴다(메시가 월드의 경로 위로 간다)
        };

        /** @brief 경로 길이를 구합니다. 참조가 비었거나 입력이 없거나 길이가 0 이면 false 입니다. */
        [[nodiscard]] static bool tryGetPathLength( const MeshModifierContext& context, const string& reference, float32& outLength )
        {
            const IMeshModifierInputs* pInputs = context.getInputs();
            if ( pInputs == nullptr || reference.empty() )
                return false;
            if ( pInputs->tryGetCurveLength( reference, outLength ) == false )
                return false;
            return MathUtil::isFinite( outLength ) && outLength > MathUtil::kEpsilon;
        }

        /** @brief 경로를 비율 [0,1] 에서 표본해 모디파이어 공간 위치 · 단위 접선을 냅니다. */
        [[nodiscard]] static bool trySamplePath( const MeshModifierContext& context, const string& reference, PathSpace space, float32 fraction, float3& outPosition,
                                                 float3& outTangent )
        {
            const IMeshModifierInputs* pInputs = context.getInputs();
            if ( pInputs == nullptr || reference.empty() )
                return false;
            float3 worldPosition{ 0.0f, 0.0f, 0.0f };
            float3 worldTangent{ 0.0f, 1.0f, 0.0f };
            if ( pInputs->trySampleCurve( reference, MathUtil::saturate( fraction ), worldPosition, worldTangent ) == false )
                return false;
            float4x4 worldToSpace = context._worldToModifier;
            if ( space == PathSpace::PathLocal )
            {
                float4x4 pathWorld{};
                worldToSpace = pInputs->tryGetWorldMatrix( reference, pathWorld ) ? pathWorld.invert() : float4x4{};
            }
            outPosition = float3::transform( worldPosition, worldToSpace );
            outTangent  = MeshModifierUtil::normalizeOr( float3::transformVector( worldTangent, worldToSpace ), float3::UnitY );
            return true;
        }

        /**
         * @brief 접선에 수직인 두 축을 만듭니다. 접선이 주축이면 결과는 (주축 다음 축, 그다음 축)이라 곧은 경로에서 항등 프레임이 됩니다.
         */
        static void buildFrame( const float3& tangent, uint32 axisIndex, float3& outAxisU, float3& outAxisV )
        {
            float3 reference{ 0.0f, 0.0f, 0.0f };
            MeshModifierUtil::setComponent( reference, ( axisIndex + 2 ) % 3, 1.0f );
            float3 projected = reference - tangent * reference.dot( tangent );
            if ( projected.getLength() > 1e-3f )
            {
                outAxisV = projected.normalize();
                outAxisU = MeshModifierUtil::normalizeOr( outAxisV.cross( tangent ), float3::UnitX );
                return;
            }
            float3 fallback{ 0.0f, 0.0f, 0.0f };
            MeshModifierUtil::setComponent( fallback, ( axisIndex + 1 ) % 3, 1.0f );
            projected = fallback - tangent * fallback.dot( tangent );
            outAxisU  = MeshModifierUtil::normalizeOr( projected, float3::UnitX );
            outAxisV  = MeshModifierUtil::normalizeOr( tangent.cross( outAxisU ), float3::UnitZ );
        }

        /**
         * @brief 경로 위 호 길이 @p distance 자리에 단면 좌표 (u, v) 를 놓습니다. 경로 밖은 끝 접선을 따라 곧게 잇습니다.
         * @return 표본에 실패하면 false(호출자는 원래 점을 둔다).
         */
        [[nodiscard]] static bool tryPlaceOnPath( const MeshModifierContext& context, const string& reference, PathSpace space, float32 length, float32 distance,
                                                  bool bLoop, float32 coordU, float32 coordV, float3& outPosition )
        {
            float32 along = distance;
            if ( bLoop )
            {
                along = along - MathUtil::floor( along / length ) * length;
            }
            const float32 clamped = MathUtil::clamp( along, 0.0f, length );
            float3        position{ 0.0f, 0.0f, 0.0f };
            float3        tangent{ 0.0f, 1.0f, 0.0f };
            if ( trySamplePath( context, reference, space, clamped / length, position, tangent ) == false )
                return false;
            float3 axisU{ 1.0f, 0.0f, 0.0f };
            float3 axisV{ 0.0f, 0.0f, 1.0f };
            buildFrame( tangent, context._axisIndex, axisU, axisV );
            outPosition = position + tangent * ( along - clamped ) + axisU * coordU + axisV * coordV;
            return true;
        }

        /** @brief (u, v) 를 각 @p radian 만큼 돌립니다. */
        static void rotate2D( float32 radian, float32& inoutU, float32& inoutV )
        {
            const float32 cosine = MathUtil::cos( radian );
            const float32 sine   = MathUtil::sin( radian );
            const float32 u      = inoutU * cosine - inoutV * sine;
            const float32 v      = inoutU * sine + inoutV * cosine;
            inoutU               = u;
            inoutV               = v;
        }

        /** @brief 참조 메시의 삼각형을 모디파이어 공간으로 모아 @p inoutList 뒤에 붙입니다. 못 찾으면 false 입니다. */
        [[nodiscard]] static bool tryAppendTriangles( const MeshModifierContext& context, const string& reference, vector<float3>& inoutList )
        {
            const IMeshModifierInputs* pInputs = context.getInputs();
            if ( pInputs == nullptr || reference.empty() )
                return false;
            vector<float3> listWorld;
            if ( pInputs->tryGetMeshTriangles( reference, listWorld ) == false )
                return false;
            const size_t count = listWorld.size() - listWorld.size() % 3;
            for ( size_t index = 0; index < count; ++index )
            {
                inoutList.push_back( float3::transform( listWorld[index], context._worldToModifier ) );
            }
            return count > 0;
        }

        /** @brief 점에서 삼각형 위 가장 가까운 점입니다(무게중심 영역 판별). */
        static float3 findClosestOnTriangle( const float3& point, const float3& a, const float3& b, const float3& c )
        {
            const float3  ab = b - a;
            const float3  ac = c - a;
            const float3  ap = point - a;
            const float32 d1 = ab.dot( ap );
            const float32 d2 = ac.dot( ap );
            if ( d1 <= 0.0f && d2 <= 0.0f )
                return a;
            const float3  bp = point - b;
            const float32 d3 = ab.dot( bp );
            const float32 d4 = ac.dot( bp );
            if ( d3 >= 0.0f && d4 <= d3 )
                return b;
            const float32 vc = d1 * d4 - d3 * d2;
            if ( vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f )
                return a + ab * ( d1 / ( d1 - d3 ) );
            const float3  cp = point - c;
            const float32 d5 = ab.dot( cp );
            const float32 d6 = ac.dot( cp );
            if ( d6 >= 0.0f && d5 <= d6 )
                return c;
            const float32 vb = d5 * d2 - d1 * d6;
            if ( vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f )
                return a + ac * ( d2 / ( d2 - d6 ) );
            const float32 va = d3 * d6 - d5 * d4;
            if ( va <= 0.0f && ( d4 - d3 ) >= 0.0f && ( d5 - d6 ) >= 0.0f )
                return b + ( c - b ) * ( ( d4 - d3 ) / ( ( d4 - d3 ) + ( d5 - d6 ) ) );
            const float32 denominator = va + vb + vc;
            if ( MathUtil::abs( denominator ) < MathUtil::kEpsilon )
                return a;
            const float32 inverse = 1.0f / denominator;
            return a + ab * ( vb * inverse ) + ac * ( vc * inverse );
        }

        /** @brief 반직선과 삼각형(양면)의 교차 거리입니다. 맞지 않으면 false 입니다. */
        [[nodiscard]] static bool tryIntersectRay( const float3& origin, const float3& direction, const float3& a, const float3& b, const float3& c, float32& outDistance )
        {
            const float3  edge1       = b - a;
            const float3  edge2       = c - a;
            const float3  pvec        = direction.cross( edge2 );
            const float32 determinant = edge1.dot( pvec );
            if ( MathUtil::abs( determinant ) < 1e-9f )
                return false;
            const float32 inverse = 1.0f / determinant;
            const float3  tvec    = origin - a;
            const float32 u       = tvec.dot( pvec ) * inverse;
            if ( u < 0.0f || u > 1.0f )
                return false;
            const float3  qvec = tvec.cross( edge1 );
            const float32 v    = direction.dot( qvec ) * inverse;
            if ( v < 0.0f || u + v > 1.0f )
                return false;
            outDistance = edge2.dot( qvec ) * inverse;
            return outDistance >= 0.0f;
        }

        /** @brief 표면 투영 방식입니다. */
        enum class ConformMode : int32
        {
            Closest = 0, ///< 가장 가까운 표면 점
            Axis    = 1, ///< 주축 음의 방향 반직선
            Normal  = 2, ///< 정점 법선 음의 방향 반직선
        };

        /**
         * @brief 정점 하나를 삼각형 목록 표면으로 옮깁니다. 표면 법선 쪽(원래 점이 있던 쪽)으로 @p offset 만큼 띄웁니다.
         * @return 최대 거리 안에서 표면을 찾았으면 true 입니다.
         */
        [[nodiscard]] static bool tryConformPoint( const vector<float3>& listTriangle, const float3& point, const float3& rayDirection, ConformMode mode, float32 offset,
                                                   float32 maxDistance, float3& outPosition )
        {
            float32 bestDistance = MathUtil::kMaxFloat;
            float3  bestPoint{ 0.0f, 0.0f, 0.0f };
            float3  bestNormal{ 0.0f, 1.0f, 0.0f };
            for ( size_t index = 0; index + 2 < listTriangle.size(); index += 3 )
            {
                const float3& a = listTriangle[index];
                const float3& b = listTriangle[index + 1];
                const float3& c = listTriangle[index + 2];
                float3        candidate{ 0.0f, 0.0f, 0.0f };
                float32       distance = 0.0f;
                if ( mode == ConformMode::Closest )
                {
                    candidate = findClosestOnTriangle( point, a, b, c );
                    distance  = ( candidate - point ).getLength();
                }
                else
                {
                    if ( tryIntersectRay( point, rayDirection, a, b, c, distance ) == false )
                        continue;
                    candidate = point + rayDirection * distance;
                }
                if ( distance < bestDistance )
                {
                    bestDistance = distance;
                    bestPoint    = candidate;
                    bestNormal   = ( b - a ).cross( c - a );
                }
            }
            if ( bestDistance > maxDistance || bestDistance == MathUtil::kMaxFloat )
                return false;
            float3 normal = MeshModifierUtil::normalizeOr( bestNormal, float3::UnitY );
            if ( normal.dot( point - bestPoint ) < 0.0f )
                normal = normal * -1.0f;
            outPosition = bestPoint + normal * offset;
            return true;
        }

        /**
         * @brief BVH(`SurfaceBvh`)로 한 점을 표면에 맞춥니다 — `tryConformPoint` 의 전수 탐색과 같은 규칙이고 질의가 대상 삼각형 수에 로그로 늘어난다.
         * @details `SurfaceBvh` 의 질의는 멤버 스크래치를 쓰므로 한 인스턴스를 한 스레드에서만 묻는다(`conformMesh` 는 단일 스레드 루프다).
         */
        [[nodiscard]] static bool tryConformPointBvh( const SurfaceBvh& bvh, const float3& point, const float3& rayDirection, ConformMode mode, float32 offset,
                                                      float32 maxDistance, float3& outPosition )
        {
            float3 bestPoint{ 0.0f, 0.0f, 0.0f };
            float3 bestNormal{ 0.0f, 1.0f, 0.0f };
            if ( mode == ConformMode::Closest )
            {
                GeometryClosestPoint closest{};
                if ( bvh.findClosestPoint( point, maxDistance, closest ) == false )
                    return false;
                bestPoint  = closest._point;
                bestNormal = closest._normal;
            }
            else
            {
                GeometryRayHit hit{};
                if ( bvh.findNearestHit( point, rayDirection, maxDistance, hit ) == false )
                    return false;
                bestPoint  = point + rayDirection * hit._distance;
                bestNormal = hit._normal;
            }
            float3 normal = MeshModifierUtil::normalizeOr( bestNormal, float3::UnitY );
            if ( normal.dot( point - bestPoint ) < 0.0f )
                normal = normal * -1.0f;
            outPosition = bestPoint + normal * offset;
            return true;
        }

        /**
         * @brief 메시 전체를 표면으로 옮깁니다(세기 × 선택 가중치로 섞음). 법선이 있으면 면 법선으로 다시 만듭니다.
         * @details 대상 삼각형으로 BVH 를 한 번 만들고 정점마다 묻는다(정점 수 × 삼각형 수 전수 탐색이 아니다).
         */
        static void conformMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context, const vector<float3>& listTriangle, ConformMode mode, float32 offset,
                                 float32 maxDistance, float32 amount )
        {
            if ( listTriangle.size() < 3 || amount <= 0.0f )
                return;
            // 삼각형 목록(정점 3 개씩)을 인덱스 0..n-1 의 표면 하나로 넣는다.
            vector<uint32> listIndex;
            listIndex.reserve( listTriangle.size() );
            for ( uint32 index = 0; index < static_cast<uint32>( listTriangle.size() ); ++index )
                listIndex.push_back( index );
            SurfaceBvh bvh;
            bvh.addSurface( 0, listTriangle, listIndex );
            bvh.build();

            float3 axisDirection{ 0.0f, 0.0f, 0.0f };
            MeshModifierUtil::setComponent( axisDirection, context._axisIndex, -1.0f );
            bool bMoved = false;
            for ( size_t index = 0; index < inoutMesh.getVertexCount(); ++index )
            {
                const float3& point     = inoutMesh._listPosition[index];
                float3        direction = axisDirection;
                if ( mode == ConformMode::Normal )
                {
                    direction = inoutMesh.hasNormal() ? MeshModifierUtil::normalizeOr( inoutMesh._listNormal[index] * -1.0f, axisDirection ) : axisDirection;
                }
                float3 target{ 0.0f, 0.0f, 0.0f };
                if ( tryConformPointBvh( bvh, point, direction, mode, offset, maxDistance, target ) == false )
                    continue;
                const float32 blend               = MathUtil::saturate( amount * inoutMesh.getWeight( index ) );
                inoutMesh._listPosition[index] = float3::lerp( point, target, blend );
                bMoved                         = true;
            }
            if ( bMoved && inoutMesh.hasNormal() )
            {
                MeshModifierUtil::recomputeFaceNormals( inoutMesh._listPosition, inoutMesh._listNormal );
            }
        }
    };
} // namespace sw::editor
