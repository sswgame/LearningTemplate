/**
 * @file FFD2DModifier.cpp
 * @brief 스프라이트 사각형 메시(XY 평면)에 2 차원 격자 자유 형태 변형(베른슈타인 FFD)을 거는 모디파이어입니다. Z 는 그대로 둡니다.
 * @details 스프라이트 메시(`SpriteMeshBuilder`)는 XY 평면에 폭 · 높이로 놓인 사각형이라, 3 차원 격자(`LatticeModifier`) 대신 XY 만 보는 단순판을 따로 둔다.
 *          격자 상자 = 입력 정점 상자의 XY. 정규 좌표 (s, t) ∈ [0,1]^2 에서 변형 = (s, t) + Σ_i Σ_j B(i; s) · B(j; t) · D_ij,
 *          D_ij 는 제어점 (i, j) 의 이동량(상자 크기 비율)이다. 베른슈타인 기저는 선형 정밀도가 있어 이동량이 모두 0 이면 정확히 항등이다.
 *          이동량은 `Offsets` 목록에 제어점 순서(i + 해상도X · j)로 dx dy 두 개씩 둔다. 모자란 항목은 0, 넘치는 항목은 버린다 —
 *          해상도를 바꾸면 같은 목록을 새 격자 순서로 다시 읽는다. 상자 폭이나 높이가 0 이면 그 축은 그대로 둔다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct FFD2DModifierInternal
        {
            static constexpr int32 kMinResolution = 2;
            static constexpr int32 kMaxResolution = 8;

            /** @brief 차수 @p degree 의 베른슈타인 기저 값 @p degree + 1 개를 채웁니다(드 카스텔조 방식). */
            static void computeBernstein( int32 degree, float32 t, float32* pOutWeight )
            {
                pOutWeight[0]           = 1.0f;
                const float32 oneMinusT = 1.0f - t;
                for ( int32 order = 1; order <= degree; ++order )
                {
                    float32 carry = 0.0f;
                    for ( int32 index = 0; index < order; ++index )
                    {
                        const float32 value = pOutWeight[index];
                        pOutWeight[index]   = carry + oneMinusT * value;
                        carry               = t * value;
                    }
                    pOutWeight[order] = carry;
                }
            }
        };
    } // namespace

    /** @brief XY 평면 2D 격자 FFD 입니다. */
    class FFD2DModifier final : public MeshModifier
    {
    public:
        FFD2DModifier()
            : MeshModifier()
            , _resolutionXParam{ addParam( MeshModifierParamDef{ "Resolution X", MeshModifierParamKind::Int, 2.0f, 8.0f, float3{ 3.0f, 0.0f, 0.0f } } ) }
            , _resolutionYParam{ addParam( MeshModifierParamDef{ "Resolution Y", MeshModifierParamKind::Int, 2.0f, 8.0f, float3{ 3.0f, 0.0f, 0.0f } } ) }
            , _offsetsParam{ addParam( MeshModifierParamDef{ "Offsets", MeshModifierParamKind::NumberList, -2.0f, 2.0f, float3{ 0.0f, 0.0f, 0.0f }, 2,
                                                             "dx dy per control point (i + resX * j), box-size ratio" } ) }
        {
        }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const vector<float32>& listOffset = getNumberList( _offsetsParam );
            if ( listOffset.empty() )
                return point._position;
            const float3  size = context._bounds.getSize();
            const bool    bUseX = context._bounds.isValid() && size._x > MathUtil::kEpsilon;
            const bool    bUseY = context._bounds.isValid() && size._y > MathUtil::kEpsilon;
            if ( bUseX == false && bUseY == false )
                return point._position;
            const int32   countX = MathUtil::clamp( getInt( _resolutionXParam ), FFD2DModifierInternal::kMinResolution, FFD2DModifierInternal::kMaxResolution );
            const int32   countY = MathUtil::clamp( getInt( _resolutionYParam ), FFD2DModifierInternal::kMinResolution, FFD2DModifierInternal::kMaxResolution );
            const float32 coordS = bUseX ? ( point._position._x - context._bounds._min._x ) / size._x : 0.0f;
            const float32 coordT = bUseY ? ( point._position._y - context._bounds._min._y ) / size._y : 0.0f;

            float32 arrWeightX[FFD2DModifierInternal::kMaxResolution];
            float32 arrWeightY[FFD2DModifierInternal::kMaxResolution];
            FFD2DModifierInternal::computeBernstein( countX - 1, coordS, arrWeightX );
            FFD2DModifierInternal::computeBernstein( countY - 1, coordT, arrWeightY );

            const size_t pointCount = listOffset.size() / 2;
            float32      deltaS     = 0.0f;
            float32      deltaT     = 0.0f;
            for ( int32 indexY = 0; indexY < countY; ++indexY )
            {
                for ( int32 indexX = 0; indexX < countX; ++indexX )
                {
                    const size_t controlIndex = static_cast<size_t>( indexX + countX * indexY );
                    if ( controlIndex >= pointCount )
                        continue;
                    const float32 weight = arrWeightX[indexX] * arrWeightY[indexY];
                    deltaS += listOffset[controlIndex * 2] * weight;
                    deltaT += listOffset[controlIndex * 2 + 1] * weight;
                }
            }
            return float3{ bUseX ? point._position._x + deltaS * size._x : point._position._x, bUseY ? point._position._y + deltaT * size._y : point._position._y,
                           point._position._z };
        }

    private:
        uint32 _resolutionXParam;
        uint32 _resolutionYParam;
        uint32 _offsetsParam;
    };

    SW_MESH_MODIFIER( FFD2DModifier, "ffd2d", "FFD 2D (Sprite)", "Lattice" );
} // namespace sw::editor
