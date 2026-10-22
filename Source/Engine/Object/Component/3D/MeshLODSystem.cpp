#include "pch.h"

#include "Engine/Object/Component/3D/MeshLODSystem.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    /**
     * @brief `-gv_meshLodForce=N` — 메시 LOD 를 화면 크기와 상관없이 단계 N 으로 그립니다(-1 이면 화면 크기로 고른다). 단계 확인 캡처 · 비교 측정용입니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_meshLodForce, -1, "Draw every LOD mesh at this LOD level regardless of screen size (-1 = pick by screen size)" );
} // namespace sw

namespace sw
{
    SW_SCENE_SERVICE( MeshLODSystem, FlushEnd )

    MeshLODSystem::MeshLODSystem( GameObjectManager& manager )
        : _manager{ manager }
        , _listView{}
        , _pendingView{}
        , _bViewsSet{ SW_FALSE }
        , _bHasPendingView{ SW_FALSE }
    {
    }

    void MeshLODSystem::setViews( const vector<AnimationLODView>& listView )
    {
        _listView  = listView;
        _bViewsSet = SW_TRUE;
    }

    void MeshLODSystem::setPendingView( const AnimationLODView& view )
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
        const uint32 relaxedLOD = mesh.selectLOD( screenSize / ( 1.0f + kRefineHysteresis ) );
        return MathUtil::min( relaxedLOD, currentLOD );
    }

    float32 MeshLODSystem::computeScreenSize( const MeshComponent& component ) const
    {
        float3  center{};
        float32 radius = 0.0f;
        (void)component.getWorldBounds( center, radius ); // 메시 컴포넌트는 늘 경계를 낸다
        float32 screenSize = 0.0f;
        for ( const AnimationLODView& view : _listView )
        {
            screenSize = MathUtil::max( screenSize, AnimationLODUtil::computeScreenSize( view, center, radius ) );
        }
        if ( _bHasPendingView == SW_TRUE )
            screenSize = MathUtil::max( screenSize, AnimationLODUtil::computeScreenSize( _pendingView, center, radius ) );
        return screenSize;
    }

    void MeshLODSystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        SW_PROFILE_SCOPE( "GT.MeshLOD.select" );
        const vector<ScenePrimitive*>& listPrimitive = _manager.getPrimitiveRegistry().getLODPrimitives();
        const int32                    forcedLOD     = gv_meshLodForce;
        for ( ScenePrimitive* pPrimitive : listPrimitive )
        {
            // 레지스트리에 들어오는 것은 메시 계열뿐이다(`ScenePrimitive.h` 의 불변식). LOD 목록에는 메시가 단계 여럿일 때만 든다(`MeshComponent::onMeshAssigned`).
            MeshComponent& component = static_cast<MeshComponent&>( *pPrimitive );
            const Mesh*    pMesh     = component.getRawMesh();
            if ( pMesh == nullptr )
                continue;
            if ( forcedLOD >= 0 )
            {
                component.setLODIndex( static_cast<uint32>( forcedLOD ) );
                continue;
            }
            // 뷰를 받기 전(렌더 없는 실행)에는 원본을 그린다.
            if ( _bViewsSet == SW_FALSE )
                continue;
            component.setLODIndex( selectNextLOD( *pMesh, computeScreenSize( component ), component.getLODIndex() ) );
        }
    }
} // namespace sw
