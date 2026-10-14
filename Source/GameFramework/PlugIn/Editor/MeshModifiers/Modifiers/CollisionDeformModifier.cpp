/**
 * @file CollisionDeformModifier.cpp
 * @brief 정적 충돌 도형(구 · 상자 · 평면) 목록 안으로 파고든 정점을 도형 겉면 밖으로 밀어내는 모디파이어입니다.
 * @details 실행 중 물리 충돌체를 따라가는 대신, 에디터 굽기용으로 도형을 매개변수 목록에 적는다. 도형은 모디파이어 공간(기즈모 기준) 좌표다.
 *          항목 하나 = 종류(0 구 · 1 상자 · 2 평면) · 중심 xyz · 크기 xyz(구는 x 가 반지름, 상자는 반 크기, 평면은 법선 방향).
 *          `Margin` 만큼 겉면을 부풀려 그 밖으로 밀고, `Softness` 는 밀어내는 양을 줄인다(0 = 겉면까지, 1 = 거의 그대로).
 *          목록이 비면 항등이다. 도형은 목록 순서대로 한 번씩 건다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct CollisionDeformModifierInternal
        {
            static constexpr uint32 kStride = 7;

            /** @brief 구 밖으로 밉니다. 중심과 겹친 점은 +Y 로 밉니다. */
            static float3 pushOutOfSphere( const float3& point, const float3& center, float32 radius )
            {
                const float3  offset   = point - center;
                const float32 distance = offset.getLength();
                if ( distance >= radius )
                    return point;
                return center + MeshModifierUtil::normalizeOr( offset, float3::UnitY ) * radius;
            }

            /** @brief 축 정렬 상자 밖으로, 가장 가까운 면 쪽으로 밉니다. */
            static float3 pushOutOfBox( const float3& point, const float3& center, const float3& halfSize )
            {
                const float3 local = point - center;
                float32      bestDepth = MathUtil::kMaxFloat;
                uint32       bestAxis  = 0;
                for ( uint32 axis = 0; axis < 3; ++axis )
                {
                    const float32 depth = MeshModifierUtil::getComponent( halfSize, axis ) - MathUtil::abs( MeshModifierUtil::getComponent( local, axis ) );
                    if ( depth <= 0.0f )
                        return point; // 한 축이라도 밖이면 상자 밖이다
                    if ( depth < bestDepth )
                    {
                        bestDepth = depth;
                        bestAxis  = axis;
                    }
                }
                float3        result = point;
                const float32 value  = MeshModifierUtil::getComponent( local, bestAxis );
                const float32 sign   = value >= 0.0f ? 1.0f : -1.0f;
                MeshModifierUtil::setComponent( result, bestAxis, MeshModifierUtil::getComponent( center, bestAxis ) + sign * MeshModifierUtil::getComponent( halfSize, bestAxis ) );
                return result;
            }

            /** @brief 평면의 뒤쪽(법선 반대편)에 있는 점을 평면 위로 올립니다. */
            static float3 pushOutOfPlane( const float3& point, const float3& center, const float3& normalDirection, float32 margin )
            {
                const float3  normal   = MeshModifierUtil::normalizeOr( normalDirection, float3::UnitY );
                const float32 distance = ( point - center ).dot( normal );
                if ( distance >= margin )
                    return point;
                return point + normal * ( margin - distance );
            }
        };
    } // namespace

    /** @brief 정적 충돌 도형으로 정점을 밀어냅니다. */
    class CollisionDeformModifier final : public MeshModifier
    {
    public:
        /** @brief Colliders 표의 열 — 종류 · 중심 xyz · 크기 xyz(stride 7). */
        static constexpr MeshModifierListColumn kArrColumn[] = {
            { "Type", MeshModifierListColumnKind::Combo, "Sphere\0Box\0Plane\0" },
            { "Center", MeshModifierListColumnKind::Vector3 },
            { "Size", MeshModifierListColumnKind::Vector3 },
        };

        CollisionDeformModifier()
            : MeshModifier()
            , _collidersParam{ addParam( MeshModifierParamDef{ "Colliders", MeshModifierParamKind::NumberList, -1000.0f, 1000.0f, float3{ 0.0f, 0.0f, 0.0f },
                                                               CollisionDeformModifierInternal::kStride,
                                                               "type(0 sphere 1 box 2 plane) cx cy cz sx sy sz" }
                                                               .withColumns( kArrColumn )
                                                               .withLayout( "Colliders", "Static collider shapes in modifier space (sphere uses size x as radius)" ) ) }
            , _marginParam{ addParam( MeshModifierParamDef{ "Margin", MeshModifierParamKind::Float, 0.0f, 10.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Response", "Extra distance kept outside each collider surface", "m" ) ) }
            , _softnessParam{ addParam( MeshModifierParamDef{ "Softness", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Response", "Reduces the push-out (0 = to the surface, 1 = almost none)" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Pushes vertices inside static sphere, box or plane colliders back out to the surface."; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            (void)context;
            const vector<float32>& listNumber = getNumberList( _collidersParam );
            const size_t           count      = listNumber.size() / CollisionDeformModifierInternal::kStride;
            if ( count == 0 )
                return point._position;
            const float32 margin   = getFloat( _marginParam );
            const float32 keep     = MathUtil::saturate( 1.0f - getFloat( _softnessParam ) );
            float3        position = point._position;
            for ( size_t index = 0; index < count; ++index )
            {
                const float32* pItem = listNumber.data() + index * CollisionDeformModifierInternal::kStride;
                const int32    kind  = MathUtil::clamp( static_cast<int32>( MathUtil::round( pItem[0] ) ), 0, 2 );
                const float3   center{ pItem[1], pItem[2], pItem[3] };
                const float3   size{ pItem[4], pItem[5], pItem[6] };
                float3         target = position;
                if ( kind == 0 )
                {
                    target = CollisionDeformModifierInternal::pushOutOfSphere( position, center, MathUtil::abs( size._x ) + margin );
                }
                else if ( kind == 1 )
                {
                    const float3 halfSize{ MathUtil::abs( size._x ) + margin, MathUtil::abs( size._y ) + margin, MathUtil::abs( size._z ) + margin };
                    target = CollisionDeformModifierInternal::pushOutOfBox( position, center, halfSize );
                }
                else
                {
                    target = CollisionDeformModifierInternal::pushOutOfPlane( position, center, size, margin );
                }
                position = position + ( target - position ) * keep;
            }
            return position;
        }

    private:
        uint32 _collidersParam;
        uint32 _marginParam;
        uint32 _softnessParam;
    };

    SW_MESH_MODIFIER( CollisionDeformModifier, "collisiondeform", "Collision Deform", "Data" );
} // namespace sw::editor
