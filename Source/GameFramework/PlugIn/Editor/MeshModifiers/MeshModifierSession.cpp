/**
 * @file MeshModifierSession.cpp
 * @brief 메시 모디파이어 편집 세션을 구현합니다 — 대상 읽기, 되돌리기, CPU 미리보기, 사이드카 저장, 굽기, 씬 입력.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/MeshModifierSession.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourcePaths.h"

#include "GameFramework/Base/World/Spline/SplineComponent.h"
#include "GameFramework/Base/World/Spline/SplinePath.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierTextureSampler.h"

namespace sw::editor
{
    namespace
    {
        struct MeshModifierSessionInternal
        {
            static GameObject* findObjectByName( string_view name )
            {
                GameObjectManager* pManager = getActiveObjectManager();
                if ( pManager == nullptr || name.empty() )
                    return nullptr;
                return pManager->findGameObjectByName( hashed_string( name ) );
            }

            static float4x4 getWorldMatrix( const GameObject& object )
            {
                const SceneComponent* pScene = object.getComponent<SceneComponent>();
                return pScene != nullptr ? pScene->getWorldMatrix() : float4x4{};
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    MeshModifierSession::MeshModifierSession()
        : _stack{}
        , _previewMesh{}
        , _stagePreviewMesh{}
        , _sourceData{}
        , _listUndo{}
        , _listRedo{}
        , _meshPath{}
        , _sourceBounds{}
        , _target{}
        , _previewTime{ 0.0f }
        , _selectedIndex{ -1 }
        , _inputBounds{}
        , _stagePreviewIndex{ -1 }
        , _inputBoundsIndex{ -1 }
        , _selectedControlPoint{ -1 }
        , _mirrorAxis{ 0 }
        , _bPreviewDirty{ true }
        , _bStagePreviewDirty{ true }
        , _bInputBoundsDirty{ true }
        , _bMirrorEditing{ false }
    {
    }

    bool MeshModifierSession::setTarget( GameObject* pObject )
    {
        setMeshModifierTaskManager( editor::getService<TaskManager>() ); // 변형 적용(미리보기 · 굽기)을 병렬로 돌린다
        clearTarget();
        if ( pObject == nullptr )
            return false;
        const MeshComponent* pMesh = pObject->getComponent<MeshComponent>();
        if ( pMesh == nullptr || MeshAssetFormat::isMeshAssetPath( pMesh->getMeshID() ) == false )
            return false;
        if ( MeshAssetFormat::loadFromResource( pMesh->getMeshID(), _sourceData ) == false )
        {
            SW_LOG_WARNING( "Mesh modifier target '%#' could not be read", pMesh->getMeshID().c_str() );
            return false;
        }
        _meshPath = pMesh->getMeshID();
        _target   = pObject->getHandle();

        vector<float3> listPosition;
        listPosition.reserve( _sourceData._listVertex.size() );
        for ( const RHIVertex& vertex : _sourceData._listVertex )
        {
            listPosition.push_back( float3{ vertex._arrPosition } );
        }
        _sourceBounds = MeshModifierBounds::makeFromPoints( listPosition );
        (void)loadSidecar(); // 사이드카가 없으면 빈 스택으로 시작한다
        _bPreviewDirty = true;
        return true;
    }

    void MeshModifierSession::clearTarget()
    {
        _stack.clear();
        _previewMesh      = MeshModifierMesh{};
        _stagePreviewMesh = MeshModifierMesh{};
        _sourceData       = MeshAssetData{};
        _meshPath.clear();
        _sourceBounds         = MeshModifierBounds{};
        _target               = GameObjectHandle{};
        _selectedIndex        = -1;
        _stagePreviewIndex    = -1;
        _inputBounds          = MeshModifierBounds{};
        _inputBoundsIndex     = -1;
        _selectedControlPoint = -1;
        _listUndo.clear();
        _listRedo.clear();
        _bPreviewDirty      = true;
        _bStagePreviewDirty = true;
        _bInputBoundsDirty  = true;
    }

    GameObject* MeshModifierSession::findTarget() const
    {
        return _target.isValid() ? findGameObject( _target ) : nullptr;
    }

    void MeshModifierSession::recordUndo()
    {
        if ( _listUndo.size() >= kMaxUndoCount )
            _listUndo.erase( _listUndo.begin() );
        _listUndo.push_back( _stack.makeJSONText( -1 ) );
        _listRedo.clear();
    }

    bool MeshModifierSession::undo()
    {
        if ( _listUndo.empty() )
            return false;
        _listRedo.push_back( _stack.makeJSONText( -1 ) );
        const string text = std::move( _listUndo.back() );
        _listUndo.pop_back();
        (void)_stack.readJSONText( text ); // 스스로 만든 글이라 실패하지 않는다. 실패해도 빈 스택이다
        _selectedIndex = MathUtil::min( _selectedIndex, _stack.getModifierCount() - 1 );
        _bPreviewDirty = true;
        return true;
    }

    bool MeshModifierSession::redo()
    {
        if ( _listRedo.empty() )
            return false;
        _listUndo.push_back( _stack.makeJSONText( -1 ) );
        const string text = std::move( _listRedo.back() );
        _listRedo.pop_back();
        (void)_stack.readJSONText( text ); // 스스로 만든 글이라 실패하지 않는다
        _selectedIndex = MathUtil::min( _selectedIndex, _stack.getModifierCount() - 1 );
        _bPreviewDirty = true;
        return true;
    }

    void MeshModifierSession::refreshPreview()
    {
        if ( _bPreviewDirty == false || hasTarget() == false )
            return;
        _bPreviewDirty      = false;
        _bStagePreviewDirty = true; // 단계 미리보기도 같은 스택 상태를 따라간다
        _bInputBoundsDirty  = true;
        makeSourceMesh( _previewMesh );
        MeshModifierEvaluation evaluation{};
        evaluation._meshToWorld = computeMeshToWorld();
        evaluation._pInputs     = this;
        evaluation._time        = _previewTime;
        _stack.apply( _previewMesh, evaluation );
    }

    void MeshModifierSession::makePreviewSegments( vector<float3>& outListSegmentPoint ) const
    {
        makeSegments( _previewMesh, outListSegmentPoint );
    }

    void MeshModifierSession::makeStagePreviewSegments( vector<float3>& outListSegmentPoint )
    {
        outListSegmentPoint.clear();
        refreshPreview();
        const int32 lastIndex = _selectedIndex;
        if ( hasTarget() == false || lastIndex < 0 || lastIndex + 1 >= _stack.getModifierCount() )
            return;
        if ( _bStagePreviewDirty || _stagePreviewIndex != lastIndex )
        {
            _bStagePreviewDirty = false;
            _stagePreviewIndex  = lastIndex;
            makeSourceMesh( _stagePreviewMesh );
            MeshModifierEvaluation evaluation{};
            evaluation._meshToWorld = computeMeshToWorld();
            evaluation._pInputs     = this;
            evaluation._time        = _previewTime;
            // 스택의 apply 와 같은 순서로 고른 모디파이어까지만 건다. 법선 재계산은 위치에 영향이 없어 건너뛴다(선만 그린다)
            for ( int32 index = 0; index <= lastIndex; ++index )
            {
                const MeshModifier* pModifier = _stack.findModifier( index );
                if ( pModifier != nullptr )
                    pModifier->apply( _stagePreviewMesh, evaluation );
            }
        }
        makeSegments( _stagePreviewMesh, outListSegmentPoint );
    }

    const MeshModifierBounds& MeshModifierSession::getSelectedModifierInputBounds()
    {
        refreshPreview();
        const int32 selectedIndex = _selectedIndex;
        if ( hasTarget() == false || selectedIndex <= 0 || selectedIndex >= _stack.getModifierCount() )
            return _sourceBounds; // 첫 모디파이어의 입력은 원본 그대로다
        if ( _bInputBoundsDirty || _inputBoundsIndex != selectedIndex )
        {
            _bInputBoundsDirty    = false;
            _inputBoundsIndex     = selectedIndex;
            const int32 lastIndex = selectedIndex - 1;
            if ( _bStagePreviewDirty == false && _stagePreviewIndex == lastIndex && _stagePreviewMesh._listPosition.empty() == false )
            {
                // 단계 미리보기 캐시가 바로 앞 모디파이어까지 건 결과면 그대로 쓴다
                _inputBounds = MeshModifierBounds::makeFromPoints( _stagePreviewMesh._listPosition );
            }
            else
            {
                // 스택의 apply 와 같은 순서로 앞 모디파이어까지만 건다(꺼진 것은 apply 가 건너뛴다)
                MeshModifierMesh mesh;
                makeSourceMesh( mesh );
                MeshModifierEvaluation evaluation{};
                evaluation._meshToWorld = computeMeshToWorld();
                evaluation._pInputs     = this;
                evaluation._time        = _previewTime;
                for ( int32 index = 0; index <= lastIndex; ++index )
                {
                    const MeshModifier* pModifier = _stack.findModifier( index );
                    if ( pModifier != nullptr )
                        pModifier->apply( mesh, evaluation );
                }
                _inputBounds = MeshModifierBounds::makeFromPoints( mesh._listPosition );
            }
        }
        return _inputBounds.isValid() ? _inputBounds : _sourceBounds;
    }

    void MeshModifierSession::makeSegments( const MeshModifierMesh& mesh, vector<float3>& outListSegmentPoint ) const
    {
        outListSegmentPoint.clear();
        const size_t triangleCount = mesh._listPosition.size() / 3;
        if ( triangleCount == 0 )
            return;
        const size_t   stride      = ( triangleCount + kMaxPreviewTriangleCount - 1 ) / kMaxPreviewTriangleCount;
        const float4x4 meshToWorld = computeMeshToWorld();
        outListSegmentPoint.reserve( ( triangleCount / stride + 1 ) * 6 );
        for ( size_t triangle = 0; triangle < triangleCount; triangle += stride )
        {
            const float3 pointA = float3::transform( mesh._listPosition[triangle * 3], meshToWorld );
            const float3 pointB = float3::transform( mesh._listPosition[triangle * 3 + 1], meshToWorld );
            const float3 pointC = float3::transform( mesh._listPosition[triangle * 3 + 2], meshToWorld );
            outListSegmentPoint.push_back( pointA );
            outListSegmentPoint.push_back( pointB );
            outListSegmentPoint.push_back( pointB );
            outListSegmentPoint.push_back( pointC );
            outListSegmentPoint.push_back( pointC );
            outListSegmentPoint.push_back( pointA );
        }
    }

    float4x4 MeshModifierSession::computeMeshToWorld() const
    {
        const GameObject* pObject = findTarget();
        if ( pObject == nullptr )
            return float4x4{};
        const MeshComponent* pMesh = pObject->getComponent<MeshComponent>();
        return pMesh != nullptr ? pMesh->getWorldMatrix() : float4x4{};
    }

    bool MeshModifierSession::saveSidecar() const
    {
        if ( hasTarget() == false )
            return false;
        const string path = ResourcePaths::getWritePath( MeshModifierStack::makeSidecarPath( _meshPath ) );
        if ( _stack.saveFile( path ) == false )
        {
            SW_LOG_ERROR( "Failed to write mesh modifier stack %#", path.c_str() );
            return false;
        }
        SW_LOG_INFO( "Mesh modifier stack saved to %#", path.c_str() );
        return true;
    }

    bool MeshModifierSession::loadSidecar()
    {
        if ( hasTarget() == false )
            return false;
        const string path = ResourcePaths::getResourcePath( MeshModifierStack::makeSidecarPath( _meshPath ) );
        if ( path.empty() )
            return false;
        if ( _stack.loadFile( path ) == false )
        {
            SW_LOG_WARNING( "Mesh modifier stack %# could not be read", path.c_str() );
            return false;
        }
        _selectedIndex = _stack.getModifierCount() > 0 ? 0 : -1;
        _bPreviewDirty = true;
        return true;
    }

    bool MeshModifierSession::bake( string* pOutPath ) const
    {
        if ( hasTarget() == false || _sourceData._listVertex.empty() )
            return false;
        MeshModifierMesh mesh{};
        makeSourceMesh( mesh );
        MeshModifierEvaluation evaluation{};
        evaluation._meshToWorld = computeMeshToWorld();
        evaluation._pInputs     = this;
        evaluation._time        = _previewTime;
        _stack.apply( mesh, evaluation );

        MeshAssetData baked{};
        baked._listVertex     = _sourceData._listVertex;
        baked._listSkinVertex = _sourceData._listSkinVertex;
        baked._skinBoneCount  = _sourceData._skinBoneCount;
        if ( _sourceData._listMorphTarget.empty() == false )
            SW_LOG_WARNING( "Mesh '%#' has %# morph targets - they are dropped from the baked mesh", _meshPath.c_str(), _sourceData._listMorphTarget.size() );
        for ( size_t index = 0; index < baked._listVertex.size(); ++index )
        {
            RHIVertex&    vertex   = baked._listVertex[index];
            const float3& position = mesh._listPosition[index];
            const float3& normal   = mesh._listNormal[index];
            const float2& uv       = mesh._listUV[index];
            const float4& color    = mesh._listColor[index];
            vertex._arrPosition[0] = position._x;
            vertex._arrPosition[1] = position._y;
            vertex._arrPosition[2] = position._z;
            vertex._arrNormal[0]   = normal._x;
            vertex._arrNormal[1]   = normal._y;
            vertex._arrNormal[2]   = normal._z;
            vertex._arrUv[0]       = uv._x;
            vertex._arrUv[1]       = uv._y;
            vertex._arrColor[0]    = color._x;
            vertex._arrColor[1]    = color._y;
            vertex._arrColor[2]    = color._z;
            vertex._arrColor[3]    = color._w;
        }

        const string resourceID = MeshModifierStack::makeBakedMeshPath( _meshPath );
        const string path       = ResourcePaths::getWritePath( resourceID );
        if ( MeshAssetFormat::saveToFile( path, baked ) == false )
        {
            SW_LOG_ERROR( "Failed to write baked mesh %#", path.c_str() );
            return false;
        }
        SW_LOG_INFO( "Baked mesh written to %#", path.c_str() );
        if ( pOutPath != nullptr )
            *pOutPath = resourceID;
        return true;
    }

    void MeshModifierSession::setPreviewTime( float32 time )
    {
        if ( MathUtil::isFinite( time ) == false || time == _previewTime )
            return;
        _previewTime   = time;
        _bPreviewDirty = true;
    }

    bool MeshModifierSession::tryGetWorldMatrix( string_view reference, float4x4& outMatrix ) const
    {
        const GameObject* pObject = MeshModifierSessionInternal::findObjectByName( reference );
        if ( pObject == nullptr )
            return false;
        outMatrix = MeshModifierSessionInternal::getWorldMatrix( *pObject );
        return true;
    }

    bool MeshModifierSession::tryGetCurveLength( string_view reference, float32& outLength ) const
    {
        const GameObject*      pObject = MeshModifierSessionInternal::findObjectByName( reference );
        const SplineComponent* pSpline = pObject != nullptr ? pObject->getComponent<SplineComponent>() : nullptr;
        if ( pSpline == nullptr || pSpline->getLocalPath().isValid() == false )
            return false;
        outLength = pSpline->getLocalPath().getLength(); // 오브젝트 스케일은 반영하지 않는다(편집 미리보기 근사)
        return true;
    }

    bool MeshModifierSession::trySampleCurve( string_view reference, float32 fraction, float3& outPosition, float3& outTangent ) const
    {
        const GameObject*      pObject = MeshModifierSessionInternal::findObjectByName( reference );
        const SplineComponent* pSpline = pObject != nullptr ? pObject->getComponent<SplineComponent>() : nullptr;
        if ( pSpline == nullptr || pSpline->getLocalPath().isValid() == false )
            return false;
        // 에디터에서는 월드 경로가 얼려 있지 않을 수 있어 로컬 경로를 오브젝트 월드 변환으로 옮긴다
        const SplineSample sample = pSpline->getLocalPath().sampleAtFraction( MathUtil::saturate( fraction ) );
        const float4x4     world  = MeshModifierSessionInternal::getWorldMatrix( *pObject );
        outPosition               = float3::transform( sample._position, world );
        outTangent                = MeshModifierUtil::normalizeOr( float3::transformVector( sample._tangent, world ), sample._tangent );
        return true;
    }

    bool MeshModifierSession::tryGetMeshTriangles( string_view reference, vector<float3>& outListPosition ) const
    {
        outListPosition.clear();
        const GameObject*    pObject = MeshModifierSessionInternal::findObjectByName( reference );
        const MeshComponent* pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr || MeshAssetFormat::isMeshAssetPath( pMesh->getMeshID() ) == false )
            return false;
        vector<RHIVertex> listVertex;
        if ( MeshAssetFormat::loadFromResource( pMesh->getMeshID(), listVertex ) == false )
            return false;
        const float4x4 world = pMesh->getWorldMatrix();
        outListPosition.reserve( listVertex.size() );
        for ( const RHIVertex& vertex : listVertex )
        {
            outListPosition.push_back( float3::transform( float3{ vertex._arrPosition }, world ) );
        }
        return true;
    }

    bool MeshModifierSession::trySampleTexture( string_view assetPath, const float2& uv, float4& outColor ) const
    {
        // CPU 표본기가 이미지를 읽어 캐시한다(파일 시간으로 갱신하므로 여기서 캐시를 비우지 않는다). 못 읽으면 false 라 모디파이어가 항등이 된다
        return MeshModifierTextureSampler::trySample( assetPath, uv, outColor );
    }

    void MeshModifierSession::makeSourceMesh( MeshModifierMesh& outMesh ) const
    {
        const size_t count = _sourceData._listVertex.size();
        outMesh._listPosition.resize( count );
        outMesh._listNormal.resize( count );
        outMesh._listUV.resize( count );
        outMesh._listColor.resize( count );
        outMesh._listWeight.clear();
        for ( size_t index = 0; index < count; ++index )
        {
            const RHIVertex& vertex      = _sourceData._listVertex[index];
            outMesh._listPosition[index] = float3{ vertex._arrPosition };
            outMesh._listNormal[index]   = float3{ vertex._arrNormal };
            outMesh._listUV[index]       = float2{ vertex._arrUv[0], vertex._arrUv[1] };
            outMesh._listColor[index]    = float4{ vertex._arrColor[0], vertex._arrColor[1], vertex._arrColor[2], vertex._arrColor[3] };
        }
    }

    SW_EDITOR_SUBSYSTEM( MeshModifierSession, 9400 );
} // namespace sw::editor
