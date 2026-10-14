/**
 * @file WorldPathDeformModifier.cpp
 * @brief 메시를 월드에 놓인 경로(스플라인) 위로 옮겨 따라 휘게 하는 모디파이어입니다. 경로 오브젝트의 월드 변환이 그대로 반영됩니다.
 * @details Path Deform 과 같은 사상(주축 좌표 → 호 길이, 수직 성분 → 단면)이되 경로 표본을 월드 → 모디파이어 공간으로 옮겨 쓴다.
 *          그래서 경로를 움직이면 메시도 따라간다. 경로 참조가 비었거나 표본에 실패하면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/PathModifierUtil.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 월드 경로 따라 휘기입니다. */
    class WorldPathDeformModifier final : public MeshModifier
    {
    public:
        WorldPathDeformModifier()
            : MeshModifier()
            , _pathParam{ addParam( MeshModifierParamDef{ "Path", MeshModifierParamKind::Reference, 0.0f, 0.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "Spline object" } ) }
            , _stretchParam{ addParam( MeshModifierParamDef{ "Stretch", MeshModifierParamKind::Float, 0.01f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
            , _distanceParam{ addParam( MeshModifierParamDef{ "Distance", MeshModifierParamKind::Float, -10000.0f, 10000.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _rotateParam{ addParam( MeshModifierParamDef{ "Rotate", MeshModifierParamKind::Float, -360.0f, 360.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _twistParam{ addParam( MeshModifierParamDef{ "Twist", MeshModifierParamKind::Float, -3600.0f, 3600.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _loopParam{ addParam( MeshModifierParamDef{ "Loop", MeshModifierParamKind::Bool, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
        {
        }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            float32 length = 0.0f;
            if ( PathModifierUtil::tryGetPathLength( context, getText( _pathParam ), length ) == false )
                return point._position;
            const uint32  axis     = context._axisIndex;
            const float32 height   = MeshModifierUtil::getComponent( point._position, axis ) - MeshModifierUtil::getComponent( context._bounds._min, axis );
            const float32 distance = getFloat( _distanceParam ) + height * getFloat( _stretchParam );
            float32       coordU   = MeshModifierUtil::getComponent( point._position, ( axis + 1 ) % 3 );
            float32       coordV   = MeshModifierUtil::getComponent( point._position, ( axis + 2 ) % 3 );
            const float32 fraction = MeshModifierUtil::getAxisFraction( point._position, context );
            PathModifierUtil::rotate2D( MathUtil::toRadian( getFloat( _rotateParam ) + getFloat( _twistParam ) * fraction ), coordU, coordV );
            float3 result{ 0.0f, 0.0f, 0.0f };
            if ( PathModifierUtil::tryPlaceOnPath( context, getText( _pathParam ), PathModifierUtil::PathSpace::World, length, distance, getBool( _loopParam ), coordU,
                                                   coordV, result )
                 == false )
                result = point._position; // 경로를 못 얻으면 변형 없이 통과한다
            return result;
        }

    private:
        uint32 _pathParam;
        uint32 _stretchParam;
        uint32 _distanceParam;
        uint32 _rotateParam;
        uint32 _twistParam;
        uint32 _loopParam;
    };

    SW_MESH_MODIFIER( WorldPathDeformModifier, "worldpathdeform", "World Path Deform", "Path" );
} // namespace sw::editor
