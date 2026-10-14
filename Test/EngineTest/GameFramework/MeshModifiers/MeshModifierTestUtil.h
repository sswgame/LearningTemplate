/**
 * @file MeshModifierTestUtil.h
 * @brief 메시 모디파이어 테스트가 함께 쓰는 도우미 — 시험용 상자 메시, 비교, 결정적 매개변수 채우기, 가짜 외부 입력.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierStack.h"

namespace test
{
    /** @brief 모디파이어 테스트 도우미입니다. */
    struct MeshModifierTestUtil
    {
        /**
         * @brief 원점 중심 2 x 4 x 2 상자의 여섯 면을 @p subdivision 칸씩 나눈 삼각형 목록 메시를 만듭니다(법선 · UV · 색 포함).
         * @details 주축(Y)이 가장 길어 휘기 · 비틀기가 눈에 띈다.
         */
        static sw::editor::MeshModifierMesh makeBoxMesh( uint32 subdivision )
        {
            using sw::float3;
            sw::editor::MeshModifierMesh mesh{};
            const float3                 halfSize{ 1.0f, 2.0f, 1.0f };
            for ( uint32 face = 0; face < 6; ++face )
            {
                const uint32  axis  = face / 2;
                const float32 sign  = ( face % 2 ) == 0 ? 1.0f : -1.0f;
                const uint32  axisU = ( axis + 1 ) % 3;
                const uint32  axisV = ( axis + 2 ) % 3;
                float3        normal{ 0.0f, 0.0f, 0.0f };
                sw::editor::MeshModifierUtil::setComponent( normal, axis, sign );
                for ( uint32 cellU = 0; cellU < subdivision; ++cellU )
                {
                    for ( uint32 cellV = 0; cellV < subdivision; ++cellV )
                    {
                        float3 arrCorner[4];
                        for ( uint32 corner = 0; corner < 4; ++corner )
                        {
                            const float32 ratioU = static_cast<float32>( cellU + ( corner & 1u ) ) / static_cast<float32>( subdivision );
                            const float32 ratioV = static_cast<float32>( cellV + ( ( corner >> 1 ) & 1u ) ) / static_cast<float32>( subdivision );
                            float3        point{ 0.0f, 0.0f, 0.0f };
                            sw::editor::MeshModifierUtil::setComponent( point, axis, sign * sw::editor::MeshModifierUtil::getComponent( halfSize, axis ) );
                            sw::editor::MeshModifierUtil::setComponent( point, axisU, ( ratioU * 2.0f - 1.0f ) * sw::editor::MeshModifierUtil::getComponent( halfSize, axisU ) );
                            sw::editor::MeshModifierUtil::setComponent( point, axisV, ( ratioV * 2.0f - 1.0f ) * sw::editor::MeshModifierUtil::getComponent( halfSize, axisV ) );
                            arrCorner[corner] = point;
                        }
                        const uint32 arrOrder[6] = { 0, 1, 3, 0, 3, 2 };
                        for ( const uint32 order : arrOrder )
                        {
                            mesh._listPosition.push_back( arrCorner[order] );
                            mesh._listNormal.push_back( normal );
                            mesh._listUV.push_back( sw::float2{ static_cast<float32>( cellU ), static_cast<float32>( cellV ) } );
                            mesh._listColor.push_back( sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
                        }
                    }
                }
            }
            return mesh;
        }

        /** @brief 두 점 목록의 가장 큰 거리입니다. 길이가 다르면 무한대입니다. */
        static float32 computeMaxDistance( const sw::vector<sw::float3>& listA, const sw::vector<sw::float3>& listB )
        {
            if ( listA.size() != listB.size() )
                return sw::MathUtil::kMaxFloat;
            float32 maxDistance = 0.0f;
            for ( size_t index = 0; index < listA.size(); ++index )
            {
                maxDistance = sw::MathUtil::max( maxDistance, ( listA[index] - listB[index] ).getLength() );
            }
            return maxDistance;
        }

        /** @brief 모든 성분이 유한하면 true 입니다. */
        static bool isFinite( const sw::vector<sw::float3>& listPoint )
        {
            for ( const sw::float3& point : listPoint )
            {
                const bool bNonFinitePoint = sw::MathUtil::isFinite( point._x ) == false || sw::MathUtil::isFinite( point._y ) == false || sw::MathUtil::isFinite( point._z ) == false;
                if ( bNonFinitePoint )
                    return false;
            }
            return true;
        }

        /** @brief 두 점 목록이 비트까지 같으면 true 입니다. */
        static bool isBitwiseEqual( const sw::vector<sw::float3>& listA, const sw::vector<sw::float3>& listB )
        {
            if ( listA.size() != listB.size() )
                return false;
            for ( size_t index = 0; index < listA.size(); ++index )
            {
                const bool bSame = listA[index]._x == listB[index]._x && listA[index]._y == listB[index]._y && listA[index]._z == listB[index]._z;
                if ( bSame == false )
                    return false;
            }
            return true;
        }

        /**
         * @brief 보간이 섞인 곡선입니다: (0, 0) 선형 → (0.5, @p middle) 큐빅(자동 접선) → (1, 1) 상수. 저장 문서가 보간 · 접선을 보존하는지 보는 데 쓴다.
         */
        static sw::FloatCurve makeMixedCurve( float32 middle )
        {
            sw::FloatCurve curve{};
            (void)curve.addKey( 0.0f, 0.0f, sw::CurveInterpolation::Linear );
            (void)curve.addKey( 0.5f, middle, sw::CurveInterpolation::Cubic );
            (void)curve.addKey( 1.0f, 1.0f, sw::CurveInterpolation::Constant );
            return curve;
        }

        /**
         * @brief 모든 매개변수를 @p salt 로 정해지는 범위 안 값으로 채웁니다(같은 salt → 같은 값). 끝값을 피하려고 범위의 20~80 % 안에서 고릅니다.
         * @details 참조 · 경로는 비워 둔다(외부 입력 없이 항등). 수 목록은 항목 하나를 넣고, 곡선 목록은 보간이 섞인 곡선 둘을 넣는다.
         */
        static void fillParams( sw::editor::MeshModifier& modifier, uint32 salt )
        {
            using sw::editor::MeshModifierParamKind;
            for ( uint32 paramIndex = 0; paramIndex < modifier.getParamCount(); ++paramIndex )
            {
                const sw::editor::MeshModifierParamDef& def   = modifier.getParamDefs()[paramIndex];
                const uint32                            hash  = ( paramIndex + 1u ) * 2654435761u ^ ( salt * 40503u );
                const float32                           ratio = 0.2f + 0.6f * static_cast<float32>( hash % 1000u ) / 999.0f;
                const float32                           value = def._min + ( def._max - def._min ) * ratio;
                switch ( def._kind )
                {
                    case MeshModifierParamKind::Float:
                    case MeshModifierParamKind::Int:
                    case MeshModifierParamKind::Bool:
                    case MeshModifierParamKind::Axis:
                    {
                        modifier.setScalar( paramIndex, def._kind == MeshModifierParamKind::Axis ? static_cast<float32>( hash % 3u ) : value );
                        break;
                    }
                    case MeshModifierParamKind::Vector3:
                    {
                        modifier.setVector3( paramIndex, sw::float3{ value, def._min + ( def._max - def._min ) * 0.5f, value } );
                        break;
                    }
                    case MeshModifierParamKind::Curve:
                    {
                        sw::editor::MeshModifierParamValue curveValue{};
                        curveValue._curve = makeMixedCurve( ratio );
                        modifier.setParamValue( paramIndex, curveValue );
                        break;
                    }
                    case MeshModifierParamKind::CurveList:
                    {
                        sw::editor::MeshModifierParamValue listValue{};
                        listValue._listCurve.push_back( makeMixedCurve( ratio ) );
                        listValue._listCurve.push_back( makeMixedCurve( 1.0f - ratio ) );
                        modifier.setParamValue( paramIndex, listValue );
                        break;
                    }
                    case MeshModifierParamKind::ReferenceList:
                    {
                        break;
                    }
                    case MeshModifierParamKind::NumberList:
                    {
                        const sw::vector<float32> listNumber( def._stride, value );
                        modifier.setNumberList( paramIndex, listNumber );
                        break;
                    }
                    case MeshModifierParamKind::Reference:
                    case MeshModifierParamKind::AssetPath:
                    {
                        break;
                    }
                }
            }
            modifier.onParamsChanged();
        }

        /** @brief 이름의 스칼라 매개변수를 넣습니다. 이름이 없으면 false 입니다. */
        static bool setScalar( sw::editor::MeshModifier& modifier, sw::string_view name, float32 value )
        {
            uint32 paramIndex = 0;
            if ( modifier.tryGetParamIndex( name, paramIndex ) == false )
                return false;
            modifier.setScalar( paramIndex, value );
            modifier.onParamsChanged();
            return true;
        }

        /** @brief 모디파이어 하나를 메시 로컬 공간(단위 변환)으로 겁니다. */
        static void applyOne( const sw::editor::MeshModifier& modifier, sw::editor::MeshModifierMesh& inoutMesh )
        {
            const sw::editor::MeshModifierEvaluation evaluation{};
            modifier.apply( inoutMesh, evaluation );
        }
    };
} // namespace test

namespace test
{
    /** @brief 이름 하나의 오브젝트 변환만 아는 가짜 외부 입력입니다. */
    class FakeMeshModifierInputs final : public sw::editor::IMeshModifierInputs
    {
    public:
        FakeMeshModifierInputs()
            : _name{}
            , _matrix{}
        {
        }

        void setObject( sw::string_view name, const sw::float4x4& matrix )
        {
            _name   = sw::string( name );
            _matrix = matrix;
        }

        [[nodiscard]] bool tryGetWorldMatrix( sw::string_view reference, sw::float4x4& outMatrix ) const override
        {
            if ( reference != sw::string_view( _name ) )
                return false;
            outMatrix = _matrix;
            return true;
        }

        [[nodiscard]] bool tryGetCurveLength( sw::string_view, float32& ) const override { return false; }
        [[nodiscard]] bool trySampleCurve( sw::string_view, float32, sw::float3&, sw::float3& ) const override { return false; }
        [[nodiscard]] bool tryGetMeshTriangles( sw::string_view, sw::vector<sw::float3>& ) const override { return false; }
        [[nodiscard]] bool trySampleTexture( sw::string_view, const sw::float2&, sw::float4& ) const override { return false; }

    private:
        sw::string   _name;
        sw::float4x4 _matrix;
    };
} // namespace test
