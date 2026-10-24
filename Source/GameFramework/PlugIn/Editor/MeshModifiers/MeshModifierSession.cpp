/**
 * @file MeshModifierSession.cpp
 * @brief 메시 모디파이어 편집 세션을 구현합니다 — 대상 읽기, 되돌리기, CPU 미리보기, 사이드카 저장, 베이크, 씬 입력.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/MeshModifierSession.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Resource/ResourcePaths.h"

#include "GameFramework/Base/World/Spline/SplineComponent.h"
#include "GameFramework/Base/World/Spline/SplinePath.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/DeformableModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierGPUProgram.h"

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

            /** @brief 원본 정점 @p listSource 에 스택 결과 @p mesh 의 위치 · 법선 · UV · 색을 덮은 정점을 만듭니다(베이크와 CPU 미리보기가 같이 쓴다). */
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
        , _prefixMesh{}
        , _listPrefixInputBounds{}
        , _prefixKey{}
        , _prefixMeshToWorld{}
        , _prefixTime{ 0.0f }
        , _prefixEnd{ -1 }
        , _sourceData{}
        , _listSourcePosition{}
        , _listUndo{}
        , _listRedo{}
        , _savedStackText{}
        , _meshPath{}
        , _sourceBounds{}
        , _target{}
        , _textureSampler{}
        , _previewTime{ 0.0f }
        , _selectedIndex{ -1 }
        , _inputBounds{}
        , _stagePreviewIndex{ -1 }
        , _selectedControlPoint{ -1 }
        , _mirrorAxis{ 0 }
        , _bPreviewDirty{ true }
        , _bStagePreviewDirty{ true }
        , _bInputBoundsDirty{ true }
        , _bMirrorEditing{ false }
        , _bPreviewPlaying{ false }
    {
    }

    bool MeshModifierSession::setTarget( GameObject* pObject )
    {
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

        _listSourcePosition.reserve( _sourceData._listVertex.size() );
        for ( const RHIVertex& vertex : _sourceData._listVertex )
        {
            _listSourcePosition.push_back( float3{ vertex._arrPosition } );
        }
        _sourceBounds = MeshModifierBounds::makeFromPoints( _listSourcePosition );
        (void)loadSidecar();                         // 사이드카가 없으면 빈 스택으로 시작한다
        _savedStackText = _stack.makeJSONText( -1 ); // 사이드카가 없으면 빈 스택이 저장된 상태다
        _bPreviewDirty  = true;
        return true;
    }

    bool MeshModifierSession::isStackUnsaved() const
    {
        return hasTarget() && _stack.makeJSONText( -1 ) != _savedStackText;
    }

    void MeshModifierSession::revertStackToSaved()
    {
        if ( hasTarget() == false || isStackUnsaved() == false )
            return;
        recordUndo();
        if ( _stack.readJSONText( _savedStackText ) == false )
            SW_LOG_ERROR( "Mesh modifier stack of %# could not be restored to its saved state - undo brings the edits back", _meshPath.c_str() );
        _selectedIndex = MathUtil::min( _selectedIndex, _stack.getModifierCount() - 1 );
        _bPreviewDirty = true;
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
        _prefixMesh       = MeshModifierMesh{};
        _listPrefixInputBounds.clear();
        _prefixKey.clear();
        _prefixEnd  = -1; // 다음 refreshPreview 가 앞부분 캐시를 새로 건다
        _sourceData = MeshAssetData{};
        _listSourcePosition.clear();
        _meshPath.clear();
        _sourceBounds         = MeshModifierBounds{};
        _target               = GameObjectHandle{};
        _selectedIndex        = -1;
        _stagePreviewIndex    = -1;
        _inputBounds          = MeshModifierBounds{};
        _selectedControlPoint = -1;
        _listUndo.clear();
        _listRedo.clear();
        _savedStackText.clear();
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
        // 구간 이름이 미리보기 비용의 측정 질문(MeshModifierPreviewCost MM2)이 읽는 자리다: 스택 적용(CPU) → 셰이딩 미리보기 갱신(GPU 프로그램 또는 CPU 결과 메시).
        SW_PROFILE_SCOPE( "Editor.MeshModifiers.Refresh" );
        MeshModifierEvaluation evaluation = makeEvaluation();
        updatePrefixCache( evaluation, true );
        // 고른 모디파이어 앞까지는 캐시를 이어 쓰고 그 뒤만 다시 건다 - 끌기 · 입력 중에는 보통 고른 것만 바뀐다
        _previewMesh                               = _prefixMesh;
        vector<MeshModifierBounds> listInputBounds = _listPrefixInputBounds;
        evaluation._pListInputBounds               = &listInputBounds; // GPU 프로그램이 싣는 입력 상자 - 이 적용에서 공짜로 모은다
        {
            SW_PROFILE_SCOPE( "Editor.MeshModifiers.Apply" );
            _stack.applyRange( _previewMesh, evaluation, _prefixEnd, _stack.getModifierCount() );
            _stack.finishApply( _previewMesh );
        }
        updateShadedPreview( evaluation, listInputBounds );
    }

    MeshModifierEvaluation MeshModifierSession::makeEvaluation() const
    {
        MeshModifierEvaluation evaluation{};
        evaluation._meshToWorld  = computeMeshToWorld();
        evaluation._pInputs      = this;
        evaluation._time         = _previewTime;
        evaluation._pTaskManager = editor::findSubsystem<TaskManager>(); // 변형 적용(미리보기 · 베이크)을 병렬로 돌린다
        // 나눠 걸어도 스택 입력은 원본 위치다(`MeshModifierStack::apply` 가 복사해 넘기는 것과 같은 값)
        if ( _stack.needsStackInputPosition() )
            evaluation._pListStackInputPosition = &_listSourcePosition;
        return evaluation;
    }

    void MeshModifierSession::updatePrefixCache( const MeshModifierEvaluation& evaluation, bool bStackChanged )
    {
        const int32 prefixEnd = MathUtil::clamp( _selectedIndex, 0, _stack.getModifierCount() );
        if ( bStackChanged == false && prefixEnd == _prefixEnd )
            return;
        // 앞부분의 결과는 앞부분 모디파이어 상태(저장 글) · 시간 · 메시 변환으로 정해진다. 셋이 같으면 다시 걸지 않는다
        string     key   = _stack.makePrefixJSONText( prefixEnd );
        const bool bSame = prefixEnd == _prefixEnd && key == _prefixKey && evaluation._time == _prefixTime && evaluation._meshToWorld == _prefixMeshToWorld;
        if ( bSame )
            return;
        _prefixEnd         = prefixEnd;
        _prefixKey         = std::move( key );
        _prefixTime        = evaluation._time;
        _prefixMeshToWorld = evaluation._meshToWorld;
        makeSourceMesh( _prefixMesh );
        _listPrefixInputBounds.clear();
        MeshModifierEvaluation prefixEvaluation = evaluation;
        prefixEvaluation._pListInputBounds      = &_listPrefixInputBounds;
        _stack.applyRange( _prefixMesh, prefixEvaluation, 0, prefixEnd );
        _bInputBoundsDirty  = true;
        _bStagePreviewDirty = true;
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
        // (렌더 스냅샷이 옛 메시를 쥐고 있어 제자리 갱신은 안전하지 않다 - 그래서 비용은 새 메시 만들기 + 탄젠트 + 정점 풀 재업로드다: MeshModifierPreviewCost MM3 결정)
        SW_PROFILE_SCOPE( "Editor.MeshModifiers.CpuMesh" );
        SW_PROFILE_COUNT( "Editor.MeshModifiers.CpuMeshVertices", static_cast<uint32>( _previewMesh._listPosition.size() ) );
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
        const MeshModifierEvaluation evaluation = makeEvaluation();
        updatePrefixCache( evaluation, false ); // 고른 번호만 바뀌었으면 앞부분 캐시를 그 번호로 옮긴다
        if ( _bStagePreviewDirty || _stagePreviewIndex != lastIndex )
        {
            _bStagePreviewDirty = false;
            _stagePreviewIndex  = lastIndex;
            // 앞부분 캐시(고른 것 앞까지)에 고른 모디파이어 하나만 더 건다. 법선 재계산은 위치에 영향이 없어 건너뛴다(선만 그린다)
            _stagePreviewMesh = _prefixMesh;
            _stack.applyRange( _stagePreviewMesh, evaluation, lastIndex, lastIndex + 1 );
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
        // 고른 모디파이어의 입력 = 앞부분 캐시(앞 모디파이어까지 건 결과). 캐시가 다시 걸렸을 때만 상자를 다시 센다
        updatePrefixCache( makeEvaluation(), false );
        if ( _bInputBoundsDirty )
        {
            _bInputBoundsDirty = false;
            _inputBounds       = MeshModifierBounds::makeFromPoints( _prefixMesh._listPosition );
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

    bool MeshModifierSession::saveSidecar()
    {
        if ( hasTarget() == false )
            return false;
        const string path = ResourcePaths::getWritePath( MeshModifierStack::makeSidecarPath( _meshPath ) );
        if ( _stack.saveFile( path ) == false )
        {
            SW_LOG_ERROR( "Failed to write mesh modifier stack %#", path.c_str() );
            return false;
        }
        _savedStackText = _stack.makeJSONText( -1 );
        SW_LOG_INFO( "Mesh modifier stack saved to %#", path.c_str() );
        return true;
    }

    bool MeshModifierSession::loadSidecar()
    {
        if ( hasTarget() == false )
            return false;
        const string path = ResourcePaths::findLooseFile( MeshModifierStack::makeSidecarPath( _meshPath ) ).getDiskPath();
        if ( path.empty() )
            return false;
        if ( _stack.loadFile( path ) == false )
        {
            SW_LOG_WARNING( "Mesh modifier stack %# could not be read", path.c_str() );
            return false;
        }
        _savedStackText = _stack.makeJSONText( -1 );
        _selectedIndex  = _stack.getModifierCount() > 0 ? 0 : -1;
        _bPreviewDirty  = true;
        return true;
    }

    bool MeshModifierSession::bake( string* pOutPath ) const
    {
        if ( hasTarget() == false || _sourceData._listVertex.empty() )
            return false;
        MeshModifierMesh mesh{};
        makeSourceMesh( mesh );
        MeshModifierEvaluation evaluation{};
        evaluation._meshToWorld  = computeMeshToWorld();
        evaluation._pInputs      = this;
        evaluation._time         = _previewTime;
        evaluation._pTaskManager = editor::findSubsystem<TaskManager>();
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

    void MeshModifierSession::advancePreviewTime( float32 deltaSeconds )
    {
        const bool bValidDelta = MathUtil::isFinite( deltaSeconds ) && deltaSeconds > 0.0f;
        const bool bAdvance    = _bPreviewPlaying && hasTarget() && bValidDelta;
        if ( bAdvance == false )
            return;
        // 시간 칸의 범위 [0, kMaxPreviewTimeSeconds] 안에서 되감아 돈다
        setPreviewTime( MathUtil::fmod( _previewTime + deltaSeconds, kMaxPreviewTimeSeconds ) );
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
        return _textureSampler.trySample( assetPath, uv, outColor );
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
