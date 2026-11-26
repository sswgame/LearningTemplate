#include "pch.h"

#include "Engine/Visual/MeshLODSystem.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Visual/Component/MeshComponent.h"
#include "Engine/Visual/Component/MeshSectionComponent.h"
#include "Engine/Visual/MeshLODSettings.h"

namespace sw
{
    SW_SCENE_SUBSYSTEM( MeshLODSystem, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    MeshLODSystem::MeshLODSystem( GameObjectManager& manager )
        : _manager{ manager }
        , _listView{}
        , _listPrimitive{}
        , _listPickedLOD{}
        , _pendingView{}
        , _forcedLOD{ kPickByScreenSize }
        , _bViewsSet{ SW_FALSE }
        , _bHasPendingView{ SW_FALSE }
        , _bGPUSelectionActive{ SW_FALSE }
    {
    }

    void MeshLODSystem::setViews( const vector<LODView>& listView )
    {
        _listView  = listView;
        _bViewsSet = SW_TRUE;
    }

    void MeshLODSystem::setPendingView( const LODView& view )
    {
        _pendingView     = view;
        _bHasPendingView = SW_TRUE;
    }

    void MeshLODSystem::clearPendingView()
    {
        _bHasPendingView = SW_FALSE;
    }

    uint32 MeshLODSystem::selectNextLOD( const Mesh& mesh, float32 screenSize, uint32 currentLOD )
    {
        const uint32 strictLOD = mesh.selectLOD( screenSize );
        if ( strictLOD >= currentLOD )
            return strictLOD;
        // 고운 쪽으로는 임계값을 여유만큼 넘어야 간다 — 화면 크기를 그만큼 깎아 다시 고른다.
        const uint32 relaxedLOD = mesh.selectLOD( screenSize / ( 1.0f + MeshLODSettings::getDefault()._refineHysteresis ) );
        return MathUtil::min( relaxedLOD, currentLOD );
    }

    bool MeshLODSystem::isGPUSelectable( const Mesh& mesh, uint32 skinSlot )
    {
        return skinSlot == constant::invalid_index::kUint32 && mesh.getSections().empty();
    }

    bool MeshLODSystem::needsCPUSelection() const
    {
        return _forcedLOD >= 0 || _bHasPendingView == SW_TRUE || _bViewsSet == SW_FALSE;
    }

    float32 MeshLODSystem::computeScreenSize( const MeshComponent& component ) const
    {
        float3  center{};
        float32 radius = 0.0f;
        (void)component.getWorldBounds( center, radius ); // 메시 컴포넌트는 늘 경계를 낸다
        // 크기는 메시 경계로 잰다 — 컴포넌트 경계는 컬링 여유(스킨드 1.5 배 · VAT)를 품어 그대로 쓰면 단계가 늘 한 칸 곱다(언리얼 LOD 화면 크기도 메시 경계다).
        const Mesh* pMesh = component.getRawMesh();
        if ( pMesh != nullptr && pMesh->getVertexCount() > 0 )
            radius = pMesh->getBoundingRadius() * component.getWorldMatrix().getMaximumAxisScale();
        float32 screenSize = 0.0f;
        for ( const LODView& view : _listView )
        {
            screenSize = MathUtil::max( screenSize, LODView::computeScreenSize( view, center, radius ) );
        }
        if ( _bHasPendingView == SW_TRUE )
            screenSize = MathUtil::max( screenSize, LODView::computeScreenSize( _pendingView, center, radius ) );
        return screenSize;
    }

    string MeshLODSystem::explainLOD( const MeshComponent& component ) const
    {
        const Mesh* pMesh = component.getRawMesh();
        if ( pMesh == nullptr )
            return "no mesh";
        const float32                            screenSize = computeScreenSize( component );
        StringBuilder<constant::buffer::kMax512> text;
        text.appendFormat( "LOD %#/%# screen %# thresholds [", component.getLODIndex(), pMesh->getLODCount(), screenSize );
        for ( size_t lodIndex = 0; lodIndex < pMesh->getLODs().size(); ++lodIndex )
        {
            if ( lodIndex != 0 )
                text.append( ' ' );
            text.appendFormat( "%#", pMesh->getLODs()[lodIndex]._screenSize );
        }
        text.append( "] by " );
        // `update` 와 같은 순서로 누가 단계를 정하는지 가른다.
        if ( pMesh->getLODs().empty() )
            text.append( "single" );
        else if ( castTo<MeshSectionComponent>( &component ) != nullptr )
            text.append( "section" );
        else if ( _bGPUSelectionActive == SW_TRUE && needsCPUSelection() == false && isGPUSelectable( *pMesh, component.getSkinSlot() ) )
            text.appendFormat( "gpu (strict pick %#)", pMesh->selectLOD( screenSize ) );
        else if ( _forcedLOD >= 0 )
            text.appendFormat( "forced %#", _forcedLOD );
        else if ( _bViewsSet == SW_FALSE )
            text.append( "views-not-set" );
        else
            text.appendFormat( "cpu (strict pick %#, refine hysteresis %#)", pMesh->selectLOD( screenSize ), MeshLODSettings::getDefault()._refineHysteresis );
        return string( text.view() );
    }

    void MeshLODSystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        SW_PROFILE_SCOPE( "GT.MeshLOD.select" );
        // GPU 컬링이 인스턴스마다 단계를 고르는 프레임은 그것이 고르는 컴포넌트의 단계를 건드리지 않는다(고정 단계 · 대기 뷰가 생기면 빌더가 다음 빌드에 CPU 로 돌려 놓는다).
        const bool                                bGPUPicks    = _bGPUSelectionActive == SW_TRUE && needsCPUSelection() == false;
        const unordered_set<PrimitiveComponent*>& setPrimitive = _manager.getPrimitiveRegistry().getLODPrimitives();
        _listPrimitive.assign( setPrimitive.begin(), setPrimitive.end() );
        const vector<PrimitiveComponent*>& listPrimitive = _listPrimitive;
        const int32                        forcedLOD     = _forcedLOD;
        const uint32                       count         = static_cast<uint32>( listPrimitive.size() );
        // 고르기(화면 크기 · 판정)는 컴포넌트마다 독립이라 워커로 나누고, 적용(`setLODIndex`)은 이 스레드가 목록 순서로 한다(언리얼 ParallelFor + 게임 스레드 반영).
        // 스레드 안전: 본문은 뷰 · 고정 단계(이 객체, 병렬 동안 바뀌지 않는다) · 컴포넌트의 메시 · 월드 캐시를 읽고 자기 칸 `_listPickedLOD[index]` 에만 쓴다.
        // 적용을 본문에 두지 않는 까닭 — `setLODIndex` 는 오너의 구간 컴포넌트 단계를 함께 쓰므로 한 오너의 주 메시 둘이 같은 구간을 다툰다.
        // 월드 캐시는 읽기만 해야 한다(더티 노드를 읽으면 `ensureWorldCache` 가 계층을 합성해 쓴다) — 남은 더티가 있으면 먼저 펼친다(`runFrameStepFrameSystems` 와 같은 규칙).
        if ( _manager.hasDirtySceneTransforms() )
            _manager.flushSceneTransforms();
        _listPickedLOD.resize( count );
        engine::runParallelIntoByCost<uint32>( _listPickedLOD, [this, &listPrimitive, forcedLOD, bGPUPicks]( uint32 index, uint32& outPicked )
        { outPicked = pickLOD( static_cast<const MeshComponent&>( *listPrimitive[index] ), forcedLOD, bGPUPicks ); } );
        for ( uint32 index = 0; index < count; ++index )
        {
            if ( _listPickedLOD[index] != kKeepLOD )
                static_cast<MeshComponent&>( *listPrimitive[index] ).setLODIndex( _listPickedLOD[index] );
        }
    }

    uint32 MeshLODSystem::pickLOD( const MeshComponent& component, int32 forcedLOD, bool bGPUPicks ) const
    {
        // 레지스트리에 들어오는 것은 메시 계열뿐이다(`PrimitiveComponent.h` 의 불변식). LOD 목록에는 메시가 단계 여럿일 때만 든다(`MeshComponent::onMeshAssigned`).
        const Mesh* pMesh = component.getRawMesh();
        // 구간 컴포넌트는 따르는 주 메시의 단계를 받는다(`MeshComponent::setLODIndex`) — 따로 고르면 경계에서 단계가 갈린다.
        if ( pMesh == nullptr || castTo<MeshSectionComponent>( &component ) != nullptr )
            return kKeepLOD;
        if ( bGPUPicks && isGPUSelectable( *pMesh, component.getSkinSlot() ) )
            return kKeepLOD;
        if ( forcedLOD >= 0 )
            return static_cast<uint32>( forcedLOD );
        // 뷰를 받기 전(렌더 없는 실행)에는 원본을 그린다.
        if ( _bViewsSet == SW_FALSE )
            return kKeepLOD;
        return selectNextLOD( *pMesh, computeScreenSize( component ), component.getLODIndex() );
    }
} // namespace sw
