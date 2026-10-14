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

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourcePaths.h"

#include "GameFramework/Base/World/Spline/SplineComponent.h"
#include "GameFramework/Base/World/Spline/SplinePath.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/DeformableModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierGPUProgram.h"
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

            /** @brief 원본 정점 @p listSource 에 스택 결과 @p mesh 의 위치 · 법선 · UV · 색을 덮은 정점을 만듭니다(굽기와 CPU 미리보기가 같이 쓴다). */
            static vector<RHIVertex> makeVertices( const vector<RHIVertex>& listSource, const MeshModifierMesh& mesh )
            {
                vector<RHIVertex> listVertex = listSource;
                for ( size_t index = 0; index < listVertex.size(); ++index )
                {
                    RHIVertex&    vertex   = listVertex[index];
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
                return listVertex;
            }
        };

        /** @brief 시나리오(`meshmodifiers.scenario.xml`)가 읽는 탐침입니다. 세션이 없거나 대상이 없으면 값을 내지 않습니다. */
        struct MeshModifierSessionProbes
        {
            static editor::MeshModifierSession* findSession()
            {
                editor::MeshModifierSession* pSession = editor::findSubsystem<editor::MeshModifierSession>();
                return pSession != nullptr && pSession->hasTarget() ? pSession : nullptr;
            }

            static editor::MeshModifier* findSelected()
            {
                editor::MeshModifierSession* pSession = findSession();
                return pSession != nullptr ? pSession->getStack().findModifier( pSession->getSelectedIndex() ) : nullptr;
            }

            /** @brief 고른 모디파이어의 첫 @p kind 매개변수 값입니다. 없으면 nullptr 입니다. */
            static const editor::MeshModifierParamValue* findSelectedParam( editor::MeshModifierParamKind kind )
            {
                const editor::MeshModifier* pModifier = findSelected();
                if ( pModifier == nullptr )
                    return nullptr;
                for ( uint32 index = 0; index < pModifier->getParamCount(); ++index )
                {
                    if ( pModifier->getParamDefs()[index]._kind == kind )
                        return &pModifier->getParamValue( index );
                }
                return nullptr;
            }

            [[nodiscard]] static bool readModifierCount( const GameObjectManager*, float64& outValue )
            {
                const editor::MeshModifierSession* pSession = findSession();
                outValue                                    = pSession != nullptr ? static_cast<float64>( pSession->getStack().getModifierCount() ) : 0.0;
                return true;
            }

            [[nodiscard]] static bool readGPUPreviewActive( const GameObjectManager*, float64& outValue )
            {
                const editor::MeshModifierSession* pSession = findSession();
                outValue                                    = pSession != nullptr && pSession->isGPUPreviewActive() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 고른 모디파이어의 기즈모 중심이 원본 상자 가운데(맞추기 직후 자리)에서 떨어진 거리입니다 — 손잡이를 끈 뒤 0 보다 커야 한다. */
            [[nodiscard]] static bool readSelectedCenterOffset( const GameObjectManager*, float64& outValue )
            {
                const editor::MeshModifierSession* pSession  = findSession();
                const editor::MeshModifier*        pModifier = findSelected();
                if ( pModifier == nullptr )
                    return false;
                outValue = static_cast<float64>( ( pModifier->getGizmo()._center - pSession->getSourceBounds().getCenter() ).getLength() );
                return true;
            }

            /** @brief 고른 모디파이어의 첫 매개변수 값(스칼라)입니다. 패널 입력이 값에 닿았는지 본다. */
            [[nodiscard]] static bool readSelectedFirstParam( const GameObjectManager*, float64& outValue )
            {
                const editor::MeshModifier* pModifier = findSelected();
                if ( pModifier == nullptr || pModifier->getParamCount() == 0 )
                    return false;
                outValue = static_cast<float64>( pModifier->getFloat( 0 ) );
                return true;
            }

            [[nodiscard]] static bool readNestedCount( const GameObjectManager*, float64& outValue )
            {
                editor::MeshModifier*             pModifier   = findSelected();
                const editor::DeformableModifier* pDeformable = pModifier != nullptr ? editor::DeformableModifier::findFrom( *pModifier ) : nullptr;
                if ( pDeformable == nullptr )
                    return false;
                outValue = static_cast<float64>( pDeformable->getStack().getModifierCount() );
                return true;
            }

            [[nodiscard]] static bool readCurveListKeyCount( const GameObjectManager*, float64& outValue )
            {
                const editor::MeshModifierParamValue* pValue = findSelectedParam( editor::MeshModifierParamKind::CurveList );
                if ( pValue == nullptr )
                    return false;
                size_t keyCount = 0;
                for ( const FloatCurve& curve : pValue->_listCurve )
                {
                    keyCount += curve._listKey.size();
                }
                outValue = static_cast<float64>( keyCount );
                return true;
            }

            [[nodiscard]] static bool readCurveListItemCount( const GameObjectManager*, float64& outValue )
            {
                const editor::MeshModifierParamValue* pValue = findSelectedParam( editor::MeshModifierParamKind::CurveList );
                if ( pValue == nullptr )
                    return false;
                outValue = static_cast<float64>( pValue->_listCurve.size() );
                return true;
            }

            /** @brief 이름이 들어간(비지 않은) 참조 목록 항목 수입니다. */
            [[nodiscard]] static bool readReferenceListCount( const GameObjectManager*, float64& outValue )
            {
                const editor::MeshModifierParamValue* pValue = findSelectedParam( editor::MeshModifierParamKind::ReferenceList );
                if ( pValue == nullptr )
                    return false;
                size_t count = 0;
                for ( const string& text : pValue->_listText )
                {
                    count += text.empty() ? 0u : 1u;
                }
                outValue = static_cast<float64>( count );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    MeshModifierSession::MeshModifierSession()
        : _stack{}
        , _gpuPreviewMesh{}
        , _cpuPreviewMesh{}
        , _originalMesh{}
        , _listGPUProgram{}
        , _gpuBlockingIndex{ -1 }
        , _bShadedPreview{ true }
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
        setMeshModifierTaskManager( editor::findSubsystem<TaskManager>() ); // 변형 적용(미리보기 · 굽기)을 병렬로 돌린다
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

    void MeshModifierSession::shutdownSubsystem()
    {
        restoreOriginalMesh();
        _gpuPreviewMesh = nullptr;
    }

    void MeshModifierSession::clearTarget()
    {
        restoreOriginalMesh(); // 옛 대상이 살아 있으면 원래 메시로 - 대상이 바뀌면 미리보기 메시도 새로 만든다
        _gpuPreviewMesh   = nullptr;
        _gpuBlockingIndex = -1;
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
        vector<MeshModifierBounds> listInputBounds;
        MeshModifierEvaluation     evaluation{};
        evaluation._meshToWorld      = computeMeshToWorld();
        evaluation._pInputs          = this;
        evaluation._time             = _previewTime;
        evaluation._pListInputBounds = &listInputBounds; // GPU 프로그램이 싣는 입력 상자 - 이 적용에서 공짜로 모은다
        _stack.apply( _previewMesh, evaluation );
        updateShadedPreview( evaluation, listInputBounds );
    }

    void MeshModifierSession::setShadedPreviewEnabled( bool bEnabled )
    {
        if ( _bShadedPreview == bEnabled )
            return;
        _bShadedPreview = bEnabled;
        _bPreviewDirty  = true; // 다음 refreshPreview 가 끼우거나 되돌린다
        if ( bEnabled == false )
            restoreOriginalMesh();
    }

    void MeshModifierSession::updateShadedPreview( const MeshModifierEvaluation& evaluation, const vector<MeshModifierBounds>& listInputBounds )
    {
        (void)MeshModifierGPUProgram::canMake( _stack, &_gpuBlockingIndex );
        GameObject*    pObject   = findTarget();
        MeshComponent* pMeshComp = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        if ( _bShadedPreview == false || pMeshComp == nullptr )
        {
            restoreOriginalMesh();
            return;
        }
        if ( MeshModifierGPUProgram::make( _stack, evaluation, listInputBounds, false, _listGPUProgram ) )
        {
            if ( _gpuPreviewMesh == nullptr )
            {
                _gpuPreviewMesh = Mesh::create();
                _gpuPreviewMesh->setVertices( _sourceData._listVertex );
                _gpuPreviewMesh->setGPUMorphEnabled( true );
            }
            _gpuPreviewMesh->setGPUDeformProgram( _listGPUProgram );
            mountPreviewMesh( *pMeshComp, _gpuPreviewMesh );
            _cpuPreviewMesh = nullptr;
            return;
        }
        // GPU 표에 없는 모디파이어가 있다 - 방금 건 CPU 결과를 정점으로 올린다. 그리는 중인 메시를 고치지 않고 새 메시로 바꿔 끼운다
        shared_ptr<Mesh> cpuMesh = Mesh::create();
        cpuMesh->setVertices( MeshModifierSessionInternal::makeVertices( _sourceData._listVertex, _previewMesh ) );
        mountPreviewMesh( *pMeshComp, cpuMesh );
        _cpuPreviewMesh = std::move( cpuMesh );
    }

    void MeshModifierSession::mountPreviewMesh( MeshComponent& meshComponent, const shared_ptr<Mesh>& mesh )
    {
        const shared_ptr<Mesh>& current      = meshComponent.getMesh();
        const bool              bOursMounted = current != nullptr && ( current == _gpuPreviewMesh || current == _cpuPreviewMesh );
        if ( current == mesh )
            return;
        if ( bOursMounted == false )
            _originalMesh = current; // 처음 끼우거나, 그사이 누가 다른 메시를 걸었다
        meshComponent.setMesh( mesh );
    }

    void MeshModifierSession::restoreOriginalMesh()
    {
        if ( _originalMesh == nullptr )
            return;
        GameObject*    pObject   = findTarget();
        MeshComponent* pMeshComp = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        // 그사이 누가 다른 메시를 걸었으면(메시 id 변경 · 되돌리기) 그쪽을 존중한다
        const bool bOursMounted = pMeshComp != nullptr && ( pMeshComp->getMesh() == _gpuPreviewMesh || pMeshComp->getMesh() == _cpuPreviewMesh );
        if ( bOursMounted )
            pMeshComp->setMesh( _originalMesh );
        _originalMesh   = nullptr;
        _cpuPreviewMesh = nullptr;
    }

    void MeshModifierSession::makePreviewSegments( vector<float3>& outListSegmentPoint ) const
    {
        makeSegments( _previewMesh, outListSegmentPoint );
    }

    void MeshModifierSession::makeStagePreviewSegments( vector<float3>& outListSegmentPoint )
    {
        outListSegmentPoint.clear();
        refreshPreview();
        const int32 lastIndex       = _selectedIndex;
        const bool  bNoNextModifier = hasTarget() == false || lastIndex < 0 || lastIndex + 1 >= _stack.getModifierCount();
        if ( bNoNextModifier )
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
        const int32 selectedIndex       = _selectedIndex;
        const bool  bNoPreviousModifier = hasTarget() == false || selectedIndex <= 0 || selectedIndex >= _stack.getModifierCount();
        if ( bNoPreviousModifier )
            return _sourceBounds; // 첫 모디파이어의 입력은 원본 그대로다
        if ( _bInputBoundsDirty || _inputBoundsIndex != selectedIndex )
        {
            _bInputBoundsDirty            = false;
            _inputBoundsIndex             = selectedIndex;
            const int32 lastIndex         = selectedIndex - 1;
            const bool  bStageCacheUsable = _bStagePreviewDirty == false && _stagePreviewIndex == lastIndex && _stagePreviewMesh._listPosition.empty() == false;
            if ( bStageCacheUsable )
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
        baked._listVertex     = MeshModifierSessionInternal::makeVertices( _sourceData._listVertex, mesh );
        baked._listSkinVertex = _sourceData._listSkinVertex;
        baked._skinBoneCount  = _sourceData._skinBoneCount;
        if ( _sourceData._listMorphTarget.empty() == false )
            SW_LOG_WARNING( "Mesh '%#' has %# morph targets - they are dropped from the baked mesh", _meshPath.c_str(), _sourceData._listMorphTarget.size() );

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

    SW_AUTOMATION_PROBE( meshModifierCount, "MeshModifiers.ModifierCount", "Modifiers in the mesh modifier stack (0 without a target)",
                         &MeshModifierSessionProbes::readModifierCount );
    SW_AUTOMATION_PROBE( meshModifierGPUPreview, "MeshModifiers.GPUPreviewActive", "1 when the target mesh is drawn by the GPU modifier program",
                         &MeshModifierSessionProbes::readGPUPreviewActive );
    SW_AUTOMATION_PROBE( meshModifierCenterOffset, "MeshModifiers.SelectedCenterOffset", "Distance of the selected modifier's gizmo center from the source bounds center",
                         &MeshModifierSessionProbes::readSelectedCenterOffset );
    SW_AUTOMATION_PROBE( meshModifierFirstParam, "MeshModifiers.SelectedFirstParam", "First (scalar) parameter of the selected modifier",
                         &MeshModifierSessionProbes::readSelectedFirstParam );
    SW_AUTOMATION_PROBE( meshModifierNestedCount, "MeshModifiers.NestedCount", "Nested modifiers of the selected Deformable", &MeshModifierSessionProbes::readNestedCount );
    SW_AUTOMATION_PROBE( meshModifierCurveListItems, "MeshModifiers.CurveListItemCount", "Items of the selected modifier's first curve list",
                         &MeshModifierSessionProbes::readCurveListItemCount );
    SW_AUTOMATION_PROBE( meshModifierCurveListKeys, "MeshModifiers.CurveListKeyCount", "Keys over all items of the selected modifier's first curve list",
                         &MeshModifierSessionProbes::readCurveListKeyCount );
    SW_AUTOMATION_PROBE( meshModifierReferenceList, "MeshModifiers.ReferenceListCount", "Named items of the selected modifier's first reference list",
                         &MeshModifierSessionProbes::readReferenceListCount );
} // namespace sw::editor
