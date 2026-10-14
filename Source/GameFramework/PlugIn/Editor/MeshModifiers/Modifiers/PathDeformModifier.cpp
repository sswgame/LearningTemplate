/**
 * @file PathDeformModifier.cpp
 * @brief 메시를 경로(스플라인) 모양을 따라 휘게 하는 모디파이어입니다. 경로는 자기 오브젝트 공간 좌표로 읽어 메시는 제자리에서 휜다.
 * @details 주축 좌표(상자 아래 끝이 0)를 경로 호 길이로 보고(늘임 배율 · 시작 오프셋 반영), 주축에 수직인 두 성분을 경로 프레임의 단면 좌표로 놓는다.
 *          길이 보존: 늘임 1 이면 주축 거리 = 경로 호 길이다. 경로 참조가 비었거나 표본에 실패하면 항등이다.
 *          월드에 놓인 경로를 그대로 따라가려면 World Path Deform 을 쓴다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/PathModifierUtil.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 경로 따라 휘기(경로 자기 공간)입니다. */
    class PathDeformModifier final : public MeshModifier
    {
    public:
        PathDeformModifier()
            : MeshModifier()
            , _pathParam{
                  addParam( MeshModifierParamDef{ "Path", MeshModifierParamKind::Reference, 0.0f, 0.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "Spline object" }
                                .withLayout( "Path", "Spline object the mesh follows, read in its own local space" )
                                .withRequiredComponent( "SplineComponent" ) )
        },
            _stretchParam{ addParam( MeshModifierParamDef{ "Stretch", MeshModifierParamKind::Float, 0.01f, 100.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Path", "Scale of the mesh length along the path" ) ) },
            _offsetParam{ addParam( MeshModifierParamDef{ "Offset", MeshModifierParamKind::Float, -1.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Path", "Slide the mesh along the path (fraction of path length)" ) ) },
            _rotateParam{ addParam( MeshModifierParamDef{ "Rotate", MeshModifierParamKind::Float, -360.0f, 360.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Orientation", "Roll of the mesh around the path", "deg" ) ) },
            _twistParam{ addParam( MeshModifierParamDef{ "Twist", MeshModifierParamKind::Float, -3600.0f, 3600.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Orientation", "Extra roll added along the path", "deg" ) ) },
            _loopParam{ addParam( MeshModifierParamDef{ "Loop", MeshModifierParamKind::Bool, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Path", "Wrap around when the mesh runs past the path ends" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Bends the mesh in place along the shape of a spline."; }

        /** @brief 경로의 회전 최소화 프레임 표를 한 번 만들고(단일 스레드) 기본 정점 루프를 돈다 — 정점 루프는 표를 읽기만 한다. */
        void applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const override
        {
            _frameCache.populate( context, getText( _pathParam ), PathModifierUtil::PathSpace::PathLocal );
            MeshModifier::applyToMesh( inoutMesh, context );
            _frameCache.clear();
        }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            float32 length = 0.0f;
            if ( PathModifierUtil::tryGetPathLength( context, getText( _pathParam ), length ) == false )
                return point._position;
            const uint32  axis     = context._axisIndex;
            const float32 height   = MeshModifierUtil::getComponent( point._position, axis ) - MeshModifierUtil::getComponent( context._bounds._min, axis );
            const float32 distance = getFloat( _offsetParam ) * length + height * getFloat( _stretchParam );
            float32       coordU   = MeshModifierUtil::getComponent( point._position, ( axis + 1 ) % 3 );
            float32       coordV   = MeshModifierUtil::getComponent( point._position, ( axis + 2 ) % 3 );
            const float32 fraction = MeshModifierUtil::getAxisFraction( point._position, context );
            PathModifierUtil::rotate2D( MathUtil::toRadian( getFloat( _rotateParam ) + getFloat( _twistParam ) * fraction ), coordU, coordV );
            float3 result{ 0.0f, 0.0f, 0.0f };
            if ( PathModifierUtil::tryPlaceOnPath( context, getText( _pathParam ), PathModifierUtil::PathSpace::PathLocal, length, distance, getBool( _loopParam ), coordU,
                                                   coordV, result, &_frameCache ) == false )
                result = point._position; // 경로를 못 얻으면 변형 없이 통과한다
            return result;
        }

    private:
        mutable PathModifierUtil::PathFrameCache _frameCache; ///< `applyToMesh` 동안만 채워지는 경로 프레임 표
        uint32                                   _pathParam;
        uint32                                   _stretchParam;
        uint32                                   _offsetParam;
        uint32                                   _rotateParam;
        uint32                                   _twistParam;
        uint32                                   _loopParam;
    };

    SW_MESH_MODIFIER( PathDeformModifier, "pathdeform", "Path Deform", "Path" );
} // namespace sw::editor
