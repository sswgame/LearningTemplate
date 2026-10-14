/**
 * @file RolledModifier.cpp
 * @brief 주축의 시작 위치 위쪽을 반경 r 의 원통에 두루마리처럼 말아 올리는 모디파이어입니다. 말리는 길이 = 회전수 · 2πr 이고 그 너머는 끝 접선을 따라 곧게 이어집니다.
 * @details 시작 위치 아래는 그대로다. 말리는 쪽은 주축을 도는 각(도)으로 정한다. 회전수 0 이거나 반경 0 이면 항등이다.
 *          한 바퀴를 넘으면 겹이 같은 원통에 겹친다(두께로 반경을 줄이지 않는다).
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 두루마리 말기입니다. */
    class RolledModifier final : public MeshModifier
    {
    public:
        RolledModifier()
            : MeshModifier()
            , _radiusParam{
                  addParam( MeshModifierParamDef{ "Radius", MeshModifierParamKind::Float, 0.0f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }
                                .withLayout( "Roll", "Radius of the roll cylinder (0 = no change)", "m" ) )
        },
            _startParam{ addParam( MeshModifierParamDef{ "Start", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Roll", "Where rolling begins along the main axis (0 = bottom, 1 = top)" ) ) },
            _turnsParam{ addParam( MeshModifierParamDef{ "Turns", MeshModifierParamKind::Float, 0.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Roll", "Number of full turns rolled up (0 = no change)" ) ) },
            _directionParam{ addParam( MeshModifierParamDef{ "Direction", MeshModifierParamKind::Float, -180.0f, 180.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Roll", "Side the mesh rolls toward, around the main axis", "deg" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Rolls the mesh up around a cylinder like a scroll, starting at a point along the main axis."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 radius = getFloat( _radiusParam );
            const float32 turns  = getFloat( _turnsParam );
            if ( radius < MathUtil::kEpsilon || turns < MathUtil::kEpsilon )
                return point._position;
            const uint32  axis   = context._axisIndex;
            const uint32  axisU  = ( axis + 1 ) % 3;
            const uint32  axisV  = ( axis + 2 ) % 3;
            const float32 start  = MeshModifierUtil::getComponent( context._bounds._min, axis ) + getFloat( _startParam ) * MeshModifierUtil::getAxisLength( context );
            const float32 height = MeshModifierUtil::getComponent( point._position, axis );
            if ( height <= start )
                return point._position;

            const float32 direction = MathUtil::toRadian( getFloat( _directionParam ) );
            const float32 cosDir    = MathUtil::cos( direction );
            const float32 sinDir    = MathUtil::sin( direction );
            const float32 coordU    = MeshModifierUtil::getComponent( point._position, axisU );
            const float32 coordV    = MeshModifierUtil::getComponent( point._position, axisV );
            const float32 rollX     = coordU * cosDir + coordV * sinDir; // 말리는 방향 성분
            const float32 sideY     = -coordU * sinDir + coordV * cosDir;

            const float32 arc    = height - start;
            const float32 rolled = MathUtil::min( arc, turns * MathUtil::kTwoPi * radius );
            const float32 extra  = arc - rolled; // 다 말린 뒤는 끝 접선을 따라 곧게
            const float32 phi    = rolled / radius;
            const float32 cosPhi = MathUtil::cos( phi );
            const float32 sinPhi = MathUtil::sin( phi );
            const float32 newX   = radius - ( radius - rollX ) * cosPhi + extra * sinPhi;
            const float32 newH   = start + ( radius - rollX ) * sinPhi + extra * cosPhi;

            float3 result = point._position;
            MeshModifierUtil::setComponent( result, axis, newH );
            MeshModifierUtil::setComponent( result, axisU, newX * cosDir - sideY * sinDir );
            MeshModifierUtil::setComponent( result, axisV, newX * sinDir + sideY * cosDir );
            return result;
        }

    private:
        uint32 _radiusParam;
        uint32 _startParam;
        uint32 _turnsParam;
        uint32 _directionParam;
    };

    SW_MESH_MODIFIER( RolledModifier, "rolled", "Rolled", "Deform" );
} // namespace sw::editor
