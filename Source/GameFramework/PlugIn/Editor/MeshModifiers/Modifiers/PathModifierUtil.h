/**
 * @file PathModifierUtil.h
 * @brief 경로(스플라인) · 대상 메시를 읽는 모디파이어(Path Deform · World Path Deform · Rope Deform · Conform · Conform Multi)가 함께 쓰는 도우미입니다.
 * @details 외부 입력은 월드 좌표라서 모디파이어 공간으로 옮겨 쓴다. 입력 창구가 곡선의 위치와 접선만 주므로 경로 프레임은 회전 최소화 프레임
 *          (rotation-minimizing frame, 이중 반사법)으로 만든다 — 시작점 프레임만 접선과 고정 기준 축으로 정하고, 경로를 촘촘히 표본해 법선을 전파한다.
 *          평면 경로 · 직선 경로에서는 고정 기준 축 프레임과 같은 값이 나오고(평면 곡선의 회전 최소화 프레임은 평면 법선을 그대로 둔다),
 *          3D 경로에서 단면이 뒤집히지 않는다. 표본 표(`PathFrameCache`)는 모디파이어가 `applyToMesh` 에서 한 번 만들고(단일 스레드)
 *          정점마다 병렬로 읽기만 한다. 표가 비었으면(직접 `deformPoint` 를 부른 경우) 예전처럼 고정 기준 축 프레임을 쓴다.
 */
#pragma once
#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/ConformSurface.h"
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
        static void makeFrame( const float3& tangent, uint32 axisIndex, float3& outAxisU, float3& outAxisV )
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
         * @brief 이중 반사법 한 걸음 — (위치, 접선, 축 V) 프레임을 다음 (위치, 접선) 으로 옮깁니다. 축 U 는 V × 접선입니다.
         * @details Wang 외(2008) "Computation of Rotation Minimizing Frames". 첫 반사는 두 점을 잇는 선분에 수직인 평면, 둘째 반사는 접선을 맞추는 평면이다.
         *          두 점이 겹치거나 접선이 같으면 그 반사를 건너뛴다. 끝에 V 를 접선에 수직으로 다시 맞춘다(반올림 누적 방지).
         */
        static void transportFrame( const float3& fromPosition, const float3& fromTangent, const float3& fromAxisV, const float3& toPosition, const float3& toTangent,
                                    float3& outAxisU, float3& outAxisV )
        {
            constexpr float32 kMinSquared = 1e-12f;
            float3            axisV       = fromAxisV;
            float3            tangent     = fromTangent;
            const float3      chord       = toPosition - fromPosition;
            const float32     chordSq     = chord.dot( chord );
            if ( chordSq > kMinSquared )
            {
                axisV   = axisV - chord * ( 2.0f * chord.dot( axisV ) / chordSq );
                tangent = tangent - chord * ( 2.0f * chord.dot( tangent ) / chordSq );
            }
            const float3  turn   = toTangent - tangent;
            const float32 turnSq = turn.dot( turn );
            if ( turnSq > kMinSquared )
                axisV = axisV - turn * ( 2.0f * turn.dot( axisV ) / turnSq );
            outAxisV = MeshModifierUtil::normalizeOr( axisV - toTangent * axisV.dot( toTangent ), fromAxisV );
            outAxisU = MeshModifierUtil::normalizeOr( outAxisV.cross( toTangent ), float3::UnitX );
        }

        /**
         * @brief 표본 (위치, 단위 접선) 목록을 따라 회전 최소화 프레임을 전파합니다(순수 함수). 첫 프레임은 `makeFrame` 입니다.
         * @details 결과 목록 크기는 입력과 같다. 위치 · 접선 목록 크기가 다르면 짧은 쪽까지만 쓴다.
         */
        static void computeRotationMinimizingFrames( const vector<float3>& listPosition, const vector<float3>& listTangent, uint32 axisIndex, vector<float3>& outListAxisU,
                                                     vector<float3>& outListAxisV )
        {
            const size_t count = MathUtil::min( listPosition.size(), listTangent.size() );
            outListAxisU.assign( count, float3{ 1.0f, 0.0f, 0.0f } );
            outListAxisV.assign( count, float3{ 0.0f, 0.0f, 1.0f } );
            if ( count == 0 )
                return;
            makeFrame( listTangent[0], axisIndex, outListAxisU[0], outListAxisV[0] );
            for ( size_t index = 1; index < count; ++index )
            {
                transportFrame( listPosition[index - 1], listTangent[index - 1], outListAxisV[index - 1], listPosition[index], listTangent[index], outListAxisU[index],
                                outListAxisV[index] );
            }
        }

        /** @brief 경로를 같은 비율 간격으로 표본한 회전 최소화 프레임 표입니다. 모디파이어가 `applyToMesh` 마다 다시 만듭니다. */
        struct PathFrameCache
        {
            static constexpr uint32 kSegmentCount = 128; ///< 표본 구간 수(표본은 하나 더)

            vector<float3> _listPosition;
            vector<float3> _listTangent;
            vector<float3> _listAxisU;
            vector<float3> _listAxisV;

            bool isEmpty() const { return _listAxisV.empty(); }

            void clear()
            {
                _listPosition.clear();
                _listTangent.clear();
                _listAxisU.clear();
                _listAxisV.clear();
            }

            /** @brief 경로를 표본해 표를 만듭니다. 표본에 하나라도 실패하면 표를 비웁니다(고정 기준 축 프레임으로 돌아간다). */
            void populate( const MeshModifierContext& context, const string& reference, PathSpace space )
            {
                clear();
                _listPosition.reserve( kSegmentCount + 1 );
                _listTangent.reserve( kSegmentCount + 1 );
                for ( uint32 index = 0; index <= kSegmentCount; ++index )
                {
                    float3        position{ 0.0f, 0.0f, 0.0f };
                    float3        tangent{ 0.0f, 1.0f, 0.0f };
                    const float32 fraction = static_cast<float32>( index ) / static_cast<float32>( kSegmentCount );
                    if ( trySamplePath( context, reference, space, fraction, position, tangent ) == false )
                    {
                        clear();
                        return;
                    }
                    _listPosition.push_back( position );
                    _listTangent.push_back( tangent );
                }
                computeRotationMinimizingFrames( _listPosition, _listTangent, context._axisIndex, _listAxisU, _listAxisV );
            }

            /** @brief 비율 @p fraction 의 (위치, 접선) 에 맞는 프레임을 바로 앞 표본에서 한 걸음 옮겨 냅니다. */
            void findFrame( float32 fraction, const float3& position, const float3& tangent, float3& outAxisU, float3& outAxisV ) const
            {
                const float32 scaled = MathUtil::saturate( fraction ) * static_cast<float32>( kSegmentCount );
                const size_t  index  = MathUtil::min( static_cast<size_t>( MathUtil::max( MathUtil::floor( scaled ), 0.0f ) ), _listAxisV.size() - 1 );
                transportFrame( _listPosition[index], _listTangent[index], _listAxisV[index], position, tangent, outAxisU, outAxisV );
            }
        };

        /**
         * @brief 경로 위 호 길이 @p distance 자리에 단면 좌표 (u, v) 를 놓습니다. 경로 밖은 끝 접선을 따라 곧게 잇습니다.
         * @param pFrameCache 회전 최소화 프레임 표. 없거나 비었으면 고정 기준 축 프레임(`makeFrame`)을 쓴다.
         * @return 표본에 실패하면 false(호출자는 원래 점을 둔다).
         */
        [[nodiscard]] static bool tryPlaceOnPath( const MeshModifierContext& context, const string& reference, PathSpace space, float32 length, float32 distance,
                                                  bool bLoop, float32 coordU, float32 coordV, float3& outPosition, const PathFrameCache* pFrameCache = nullptr )
        {
            float32 along = distance;
            if ( bLoop )
                along = along - MathUtil::floor( along / length ) * length;
            const float32 clamped = MathUtil::clamp( along, 0.0f, length );
            float3        position{ 0.0f, 0.0f, 0.0f };
            float3        tangent{ 0.0f, 1.0f, 0.0f };
            if ( trySamplePath( context, reference, space, clamped / length, position, tangent ) == false )
                return false;
            float3 axisU{ 1.0f, 0.0f, 0.0f };
            float3 axisV{ 0.0f, 0.0f, 1.0f };
            if ( pFrameCache != nullptr && pFrameCache->isEmpty() == false )
                pFrameCache->findFrame( clamped / length, position, tangent, axisU, axisV );
            else
                makeFrame( tangent, context._axisIndex, axisU, axisV );
            outPosition = position + tangent * ( along - clamped ) + axisU * coordU + axisV * coordV;
            return true;
        }

        /**
         * @brief 경로를 GPU 프로그램에 싣습니다(`MeshModifier::packGPUInputs` 의 경로 모양).
         * @details 앞에 float4 하나(x = 얻었다 1 · 못 얻었다 0, y = 경로 길이)가 붙고, 얻었을 때만 표본이 뒤따른다. 표본 하나 = float4 셋(위치 · 단위 접선 · 단면 축 V, xyz),
         *          `PathFrameCache::kSegmentCount + 1` 개 - CPU 가 `tryPlaceOnPath` 에서 쓰는 같은 회전 최소화 프레임 표다. 셰이더는 호 길이 비율로 이웃 표본을 선형 보간하고
         *          접선에 V 를 다시 직교화한다(CPU 는 스플라인을 정확히 표본하고 바로 앞 표본에서 한 걸음 옮긴다 - 128 구간이라 차이는 곡률이 큰 곳에서 표본 간격 제곱 정도다).
         * @return 경로를 못 얻으면 "못 얻었다" 한 칸만 싣고 false 입니다.
         */
        static bool packGPUPathTable( const MeshModifierContext& context, const string& reference, PathSpace space, vector<float4>& outListValue )
        {
            static_assert( PathFrameCache::kSegmentCount == MeshModifier::kGPUPathSegmentCount, "GPU 경로 표본 수와 프레임 표의 구간 수가 같아야 한다" );
            const size_t start = outListValue.size();
            outListValue.push_back( float4{ 0.0f, 0.0f, 0.0f, 0.0f } );
            float32 length = 0.0f;
            if ( tryGetPathLength( context, reference, length ) == false )
                return false;
            PathFrameCache cache;
            cache.populate( context, reference, space );
            if ( cache.isEmpty() )
                return false;
            outListValue[start] = float4{ 1.0f, length, 0.0f, 0.0f };
            for ( size_t index = 0; index < cache._listPosition.size(); ++index )
            {
                outListValue.push_back( float4{ cache._listPosition[index]._x, cache._listPosition[index]._y, cache._listPosition[index]._z, 0.0f } );
                outListValue.push_back( float4{ cache._listTangent[index]._x, cache._listTangent[index]._y, cache._listTangent[index]._z, 0.0f } );
                outListValue.push_back( float4{ cache._listAxisV[index]._x, cache._listAxisV[index]._y, cache._listAxisV[index]._z, 0.0f } );
            }
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

        /** @brief GPU 표면 맞추기가 싣는 삼각형 수의 상한입니다(셰이더 전수 탐색이라 정점 × 삼각형으로 늘어난다). */
        static constexpr uint32 kGPUConformTriangleLimit = 4096;

        /**
         * @brief 모디파이어 공간 삼각형 목록을 GPU 프로그램에 싣습니다: float4(얻음, 삼각형 수) 뒤에 삼각형마다 float4 셋(a · b · c).
         * @return 상한을 넘으면 false 입니다(프로그램을 만들지 않는다). 삼각형이 없으면 "못 얻었다" 한 칸만 싣고 true 입니다(셰이더가 항등으로 둔다 - CPU 와 같다).
         */
        [[nodiscard]] static bool packGPUTriangleTable( const vector<float3>& listTriangle, vector<float4>& outListValue )
        {
            const size_t triangleCount = listTriangle.size() / 3;
            if ( triangleCount > kGPUConformTriangleLimit )
                return false;
            outListValue.push_back( float4{ triangleCount > 0 ? 1.0f : 0.0f, static_cast<float32>( triangleCount ), 0.0f, 0.0f } );
            for ( size_t index = 0; index < triangleCount * 3; ++index )
            {
                outListValue.push_back( float4{ listTriangle[index]._x, listTriangle[index]._y, listTriangle[index]._z, 0.0f } );
            }
            return true;
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
            const float32 vc              = d1 * d4 - d3 * d2;
            const bool    bInEdgeRegionAB = vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f;
            if ( bInEdgeRegionAB )
                return a + ab * ( d1 / ( d1 - d3 ) );
            const float3  cp = point - c;
            const float32 d5 = ab.dot( cp );
            const float32 d6 = ac.dot( cp );
            if ( d6 >= 0.0f && d5 <= d6 )
                return c;
            const float32 vb              = d5 * d2 - d1 * d6;
            const bool    bInEdgeRegionAC = vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f;
            if ( bInEdgeRegionAC )
                return a + ac * ( d2 / ( d2 - d6 ) );
            const float32 va              = d3 * d6 - d5 * d4;
            const bool    bInEdgeRegionBC = va <= 0.0f && ( d4 - d3 ) >= 0.0f && ( d5 - d6 ) >= 0.0f;
            if ( bInEdgeRegionBC )
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
         * @brief BVH(`ConformSurface`)로 한 점을 표면에 맞춥니다 — `tryConformPoint` 의 전수 탐색과 같은 규칙이고 질의가 대상 삼각형 수에 로그로 늘어난다.
         * @details 질의가 상태 없는 const 라서 스레드마다 @p inoutListScratch 만 따로 가지면 여러 스레드가 같은 표면을 물을 수 있다.
         */
        [[nodiscard]] static bool tryConformPointBvh( const ConformSurface& surface, const float3& point, const float3& rayDirection, ConformMode mode, float32 offset,
                                                      float32 maxDistance, vector<SlotHandle>& inoutListScratch, float3& outPosition )
        {
            float3 bestPoint{ 0.0f, 0.0f, 0.0f };
            float3 bestNormal{ 0.0f, 1.0f, 0.0f };
            if ( mode == ConformMode::Closest )
            {
                GeometryClosestPoint closest{};
                if ( surface.findClosestPoint( point, maxDistance, inoutListScratch, closest ) == false )
                    return false;
                bestPoint  = closest._point;
                bestNormal = closest._normal;
            }
            else
            {
                GeometryRayHit hit{};
                if ( surface.findNearestHit( point, rayDirection, maxDistance, inoutListScratch, hit ) == false )
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
         * @brief `conformMesh` 의 병렬 일입니다. 정점끼리 서로 읽지 않고 덩어리가 자기 정점만 쓰므로 직렬과 결과가 같다.
         * @details 워커가 건드리는 쓰기 대상(위치 · 덩어리 표시)은 `sw::vector::operator[]` 가 아니라 생 포인터로 가진다 - Debug 데이터 레이스 검출기는
         *          벡터 안의 서로 다른 칸 쓰기도 한 벡터의 동시 쓰기로 본다(`TransformJob` 과 같은 이유). 후보 버퍼는 덩어리 안에서 가진다.
         */
        struct ConformJob
        {
            const ConformSurface* _pSurface{ nullptr };
            float3*               _pPosition{ nullptr };   ///< 제자리에서 옮긴다
            const float3*         _pNormal{ nullptr };     ///< 없으면 nullptr(법선 방식은 주축 방향으로 대신한다)
            const float32*        _pWeight{ nullptr };     ///< 선택 가중치. 없으면 nullptr(모두 1)
            uint8*                _pChunkMoved{ nullptr }; ///< 덩어리마다 하나라도 옮겼는지(워커가 공유 플래그를 쓰지 않게 덩어리 칸에 쓴다)
            size_t                _vertexCount{ 0 };
            ConformMode           _mode{ ConformMode::Closest };
            float3                _axisDirection{ 0.0f, 0.0f, 0.0f };
            float32               _offset{ 0.0f };
            float32               _maxDistance{ 0.0f };
            float32               _amount{ 0.0f };

            void run( uint32 startChunk, uint32 endChunk )
            {
                const size_t       first = static_cast<size_t>( startChunk ) * MeshModifierUtil::kChunkVertexCount;
                const size_t       last  = MathUtil::min( static_cast<size_t>( endChunk ) * MeshModifierUtil::kChunkVertexCount, _vertexCount );
                vector<SlotHandle> listScratch;
                for ( size_t index = first; index < last; ++index )
                {
                    const float3 point     = _pPosition[index];
                    float3       direction = _axisDirection;
                    if ( _mode == ConformMode::Normal && _pNormal != nullptr )
                        direction = MeshModifierUtil::normalizeOr( _pNormal[index] * -1.0f, _axisDirection );
                    float3 target{ 0.0f, 0.0f, 0.0f };
                    if ( tryConformPointBvh( *_pSurface, point, direction, _mode, _offset, _maxDistance, listScratch, target ) == false )
                        continue;
                    const float32 weight                                      = _pWeight != nullptr ? _pWeight[index] : 1.0f;
                    _pPosition[index]                                         = float3::lerp( point, target, MathUtil::saturate( _amount * weight ) );
                    _pChunkMoved[index / MeshModifierUtil::kChunkVertexCount] = 1;
                }
            }
        };

        /**
         * @brief 메시 전체를 표면으로 옮깁니다(세기 × 선택 가중치로 섞음). 법선이 있으면 면 법선으로 다시 만듭니다.
         * @details 대상 삼각형은 @p inoutSurface 가 BVH 로 가지고(같은 목록이면 지난번 BVH 를 쓴다), 정점은 덩어리로 나눠 병렬로 묻는다
         *          (정점 수 × 삼각형 수 전수 탐색이 아니다). 작업 관리자가 없으면 현재 스레드가 한 번에 돈다.
         * @return BVH 를 새로 만들었으면 true 입니다(캐시 적중 확인용).
         */
        static bool conformMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context, const vector<float3>& listTriangle, ConformMode mode, float32 offset,
                                 float32 maxDistance, float32 amount, ConformSurface& inoutSurface )
        {
            if ( listTriangle.size() < 3 || amount <= 0.0f )
                return false;
            const bool bRebuilt = inoutSurface.assign( listTriangle );

            const uint32  chunkCount = MeshModifierUtil::computeChunkCount( inoutMesh.getVertexCount() );
            vector<uint8> listChunkMoved( chunkCount, 0 );
            ConformJob    job{};
            job._pSurface    = &inoutSurface;
            job._pPosition   = inoutMesh._listPosition.data();
            job._pNormal     = inoutMesh.hasNormal() ? inoutMesh._listNormal.data() : nullptr;
            job._pWeight     = inoutMesh.hasWeight() ? inoutMesh._listWeight.data() : nullptr;
            job._pChunkMoved = listChunkMoved.data();
            job._vertexCount = inoutMesh.getVertexCount();
            job._mode        = mode;
            MeshModifierUtil::setComponent( job._axisDirection, context._axisIndex, -1.0f );
            job._offset      = offset;
            job._maxDistance = maxDistance;
            job._amount      = amount;
            MeshModifierUtil::runBlocks( context.getTaskManager(), chunkCount, 2, SW_DELEGATE_METHOD( ParallelBlockDelegate, &ConformJob::run, &job ) );

            bool bMoved = false;
            for ( const uint8 chunkMoved : listChunkMoved )
            {
                bMoved = bMoved || chunkMoved != 0;
            }
            if ( bMoved && inoutMesh.hasNormal() )
                MeshModifierUtil::recomputeFaceNormals( inoutMesh._listPosition, inoutMesh._listNormal );
            return bRebuilt;
        }
    };
} // namespace sw::editor
