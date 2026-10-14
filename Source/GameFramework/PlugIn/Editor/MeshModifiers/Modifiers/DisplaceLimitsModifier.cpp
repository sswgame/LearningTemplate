/**
 * @file DisplaceLimitsModifier.cpp
 * @brief 앞선 변위 모디파이어가 낸 정점을 상자나 구 안으로 잘라 넣는 모디파이어입니다.
 * @details 모드 0 = 끔(항등), 1 = 상자(성분마다 [최소, 최대] 로 자름), 2 = 구(기즈모 중심에서 반경 밖이면 반경 위로 당김).
 *          범위는 모디파이어 공간 값이다. `fitToBounds` 는 최소 · 최대를 메시 상자로, 반경을 상자 대각선 절반으로 맞춘다.
 *          스택은 이전 결과만 넘기므로 "변위량"이 아니라 "결과 위치"를 자른다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 변위 범위 제한입니다. */
    class DisplaceLimitsModifier final : public MeshModifier
    {
    public:
        DisplaceLimitsModifier()
            : MeshModifier()
            , _modeParam{ addParam( MeshModifierParamDef{ "Mode", MeshModifierParamKind::Int, 0.0f, 2.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Range", "0 = off, 1 = clamp to box Min/Max, 2 = clamp to sphere Radius" ) ) }
            , _minParam{ addParam( MeshModifierParamDef{ "Min", MeshModifierParamKind::Vector3, -100000.0f, 100000.0f, float3{ -1.0f, -1.0f, -1.0f } }.withLayout( "Range", "Lower corner of the clamp box (Mode 1)", "m" ) ) }
            , _maxParam{ addParam( MeshModifierParamDef{ "Max", MeshModifierParamKind::Vector3, -100000.0f, 100000.0f, float3{ 1.0f, 1.0f, 1.0f } }.withLayout( "Range", "Upper corner of the clamp box (Mode 1)", "m" ) ) }
            , _radiusParam{ addParam( MeshModifierParamDef{ "Radius", MeshModifierParamKind::Float, 0.0f, 100000.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Range", "Clamp sphere radius around the origin (Mode 2)", "m" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Clamps vertices into a box or a sphere."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            (void)context;
            const int32 mode = getInt( _modeParam );
            if ( mode == 1 )
            {
                const float3& low  = getVector3( _minParam );
                const float3& high = getVector3( _maxParam );
                // 최소가 최대보다 크면 두 값을 바꿔 읽는다(빈 범위로 정점이 뒤집히지 않게)
                return float3{ MathUtil::clamp( point._position._x, MathUtil::min( low._x, high._x ), MathUtil::max( low._x, high._x ) ),
                               MathUtil::clamp( point._position._y, MathUtil::min( low._y, high._y ), MathUtil::max( low._y, high._y ) ),
                               MathUtil::clamp( point._position._z, MathUtil::min( low._z, high._z ), MathUtil::max( low._z, high._z ) ) };
            }
            if ( mode == 2 )
            {
                const float32 radius = getFloat( _radiusParam );
                const float32 length = point._position.getLength();
                if ( length <= radius || length < MathUtil::kEpsilon )
                    return point._position;
                return point._position * ( radius / length );
            }
            return point._position;
        }

    protected:
        void onFitToBounds( const MeshModifierBounds& modifierBounds ) override
        {
            if ( modifierBounds.isValid() == false )
                return;
            setVector3( _minParam, modifierBounds._min );
            setVector3( _maxParam, modifierBounds._max );
            setScalar( _radiusParam, modifierBounds.getSize().getLength() * 0.5f );
        }

    private:
        uint32 _modeParam;
        uint32 _minParam;
        uint32 _maxParam;
        uint32 _radiusParam;
    };

    SW_MESH_MODIFIER( DisplaceLimitsModifier, "displacelimits", "Displace Limits", "Surface" );
} // namespace sw::editor
