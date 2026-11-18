/**
 * @file FFD2DModifier.cpp
 * @brief 스프라이트 사각형 메시(XY 평면)에 2 차원 격자 자유 형태 변형(베른슈타인 FFD)을 거는 모디파이어입니다. Z 는 그대로 둡니다.
 * @details 스프라이트 메시(`SlicedSpriteDef`)는 XY 평면에 폭 · 높이로 놓인 사각형이라, 3 차원 격자(`LatticeModifier`) 대신 XY 만 보는 단순판을 따로 둔다.
 *          격자 상자 = 입력 정점 상자의 XY. 정규 좌표 (s, t) ∈ [0,1]^2 에서 변형 = (s, t) + Σ_i Σ_j B(i; s) · B(j; t) · D_ij,
 *          D_ij 는 제어점 (i, j) 의 이동량(상자 크기 비율)이다. 베른슈타인 기저는 선형 정밀도가 있어 이동량이 모두 0 이면 정확히 항등이다.
 *          이동량은 `Offsets` 목록에 제어점 순서(i + 해상도X · j)로 dx dy 두 개씩 둔다. 모자란 항목은 0, 넘치는 항목은 버린다 —
 *          해상도를 바꾸면 같은 목록을 새 격자 순서로 다시 읽는다. 상자 폭이나 높이가 0 이면 그 축은 그대로 둔다.
 *          해상도 범위와 베른슈타인 기저는 3 차원 격자의 것을 그대로 쓴다(`LatticeModifier::kMinResolution` · `kMaxResolution` · `computeBernstein`).
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Modifiers/LatticeModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief XY 평면 2D 격자 FFD 입니다. */
    class FFD2DModifier final : public MeshModifier
    {
    public:
        /** @brief Offsets 표의 열 — 제어점 하나의 dx · dy(stride 2, 상자 크기 비율). */
        static constexpr MeshModifierListColumn kArrColumn[] = {
            {"dX", MeshModifierListColumnKind::Float},
            {"dY", MeshModifierListColumnKind::Float},
        };

        FFD2DModifier()
            : MeshModifier()
            , _resolutionXParam{
                  addParam( MeshModifierParamDef{ "Resolution X", MeshModifierParamKind::Int, 2.0f, 8.0f, float3{ 3.0f, 0.0f, 0.0f } }
                                .withLayout( "Lattice", "Control point count along X" ) )
        },
            _resolutionYParam{ addParam( MeshModifierParamDef{ "Resolution Y", MeshModifierParamKind::Int, 2.0f, 8.0f, float3{ 3.0f, 0.0f, 0.0f } }.withLayout( "Lattice", "Control point count along Y" ) ) },
            _offsetsParam{ addParam( MeshModifierParamDef{ "Offsets", MeshModifierParamKind::NumberList, -2.0f, 2.0f, float3{ 0.0f, 0.0f, 0.0f }, 2, "dx dy per control point (i + resX * j), box-size ratio" }.withColumns( kArrColumn ).withLayout( "Control Points", "Control point offsets as a ratio of the box size" ) ) }
        {
        }

        const utf8* getDescription() const override { return "2D free-form lattice deform on the XY plane for sprite meshes; Z is left unchanged."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const vector<float32>& listOffset = getNumberList( _offsetsParam );
            if ( listOffset.empty() )
                return point._position;
            const float3 size  = context._bounds.getSize();
            const bool   bUseX = context._bounds.isValid() && size._x > MathUtil::kEpsilon;
            const bool   bUseY = context._bounds.isValid() && size._y > MathUtil::kEpsilon;
            if ( bUseX == false && bUseY == false )
                return point._position;
            const int32   countX = MathUtil::clamp( getInt( _resolutionXParam ), LatticeModifier::kMinResolution, LatticeModifier::kMaxResolution );
            const int32   countY = MathUtil::clamp( getInt( _resolutionYParam ), LatticeModifier::kMinResolution, LatticeModifier::kMaxResolution );
            const float32 coordS = bUseX ? ( point._position._x - context._bounds._min._x ) / size._x : 0.0f;
            const float32 coordT = bUseY ? ( point._position._y - context._bounds._min._y ) / size._y : 0.0f;

            float32 arrWeightX[LatticeModifier::kMaxResolution];
            float32 arrWeightY[LatticeModifier::kMaxResolution];
            LatticeModifier::computeBernstein( countX - 1, coordS, arrWeightX );
            LatticeModifier::computeBernstein( countY - 1, coordT, arrWeightY );

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
