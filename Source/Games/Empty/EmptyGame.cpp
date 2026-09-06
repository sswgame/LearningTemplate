#include "pch.h"

#include "Games/Empty/EmptyGame.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    namespace
    {
        /** @brief 큐브 사이 간격(월드 단위). */
        constexpr float32 kBenchSpacing = 2.0f;
        /** @brief 격자가 화면에 들어오도록 카메라를 뒤로 뺄 때 쓰는 여유 배수. */
        constexpr float32 kBenchCameraMargin = 0.28f;
    } // namespace

    void EmptyGame::configureBootstrap( BootstrapConfig& outConfig )
    {
        outConfig._packRoot = "game/empty";
    }

    bool EmptyGame::onInitialize()
    {
        // 커맨드라인은 게임에 열려 있지 않다(CommandLineManager gameAllowed=0). 엔진이 선언한
        // 전역 변수를 허용된 서비스로 읽는다 — `-gv_benchMeshes=5000` 처럼 준다.
        GlobalVariableManager* pGlobals = game::getService<GlobalVariableManager>();
        if ( pGlobals != nullptr )
        {
            const GlobalVariableInfo* pBenchMeshes = pGlobals->findVariable( "gv_benchMeshes" );
            const int32               meshCount    = pBenchMeshes != nullptr ? pBenchMeshes->getValueAsInt() : 0;
            if ( meshCount > 0 && _listBenchMesh.empty() )
                spawnBenchScene( static_cast<uint32>( meshCount ) );
        }

        return true;
    }

    void EmptyGame::onUpdate( float32 deltaTime )
    {
        GameInstanceBase::onUpdate( deltaTime );
        if ( _listBenchMesh.empty() == false )
            updateBenchScene( deltaTime );
    }

    void EmptyGame::spawnBenchScene( uint32 meshCount )
    {
        SceneManager* pSceneManager = game::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return;

        // createScene 은 Scene::initialize 를 부르지 않는다 — 기본 머티리얼 획득, 기본 카메라,
        // 메시 기본값 바인딩이 전부 그 안에 있다. 그래서 createScene 으로 만든 씬의 메시는
        // 머티리얼이 없어 배치는 만들어지는데 화면에는 아무것도 나오지 않는다.
        Scene* pScene = pSceneManager->getActiveScene();
        if ( pScene == nullptr )
            pScene = pSceneManager->createEmptyActiveScene( "BenchScene" );
        if ( pScene == nullptr )
        {
            SW_LOG_ERROR( "[Bench] 씬을 만들지 못했습니다." );
            return;
        }
        pScene->ensureDefaultCameras();

        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
            return;

        Material* pSceneMaterial = pScene->getMaterial();
        if ( pSceneMaterial == nullptr )
            SW_LOG_WARNING( "[Bench] 씬 기본 머티리얼이 없습니다 — 큐브가 보이지 않을 수 있습니다." );

        shared_ptr<Mesh> cube = Mesh::createUnitCube();
        if ( cube == nullptr )
        {
            SW_LOG_ERROR( "[Bench] 단위 큐브를 만들지 못했습니다." );
            return;
        }

        // 정사각에 가까운 격자로 흩는다. 전부 같은 메시·머티리얼이라 배치가 하나로 묶이는데,
        // 그게 지금 렌더러의 기본 경로(배치당 drawInstanced 한 번)를 재는 조건이다.
        const uint32  side   = static_cast<uint32>( MathUtil::sqrt( static_cast<float32>( meshCount ) ) ) + 1;
        const float32 origin = -0.5f * static_cast<float32>( side - 1 ) * kBenchSpacing;

        _listBenchMesh.clear();
        _listBenchMesh.reserve( meshCount );

        // 큐브별 머티리얼 인스턴스는 DX12 크래시를 재현하는 용도라 기본은 꺼 둔다.
        bool bPerCubeMaterial = false;
        if ( GlobalVariableManager* pGlobals = game::getService<GlobalVariableManager>() )
        {
            const GlobalVariableInfo* pVar = pGlobals->findVariable( "gv_benchMaterialInstances" );
            bPerCubeMaterial               = ( pVar != nullptr && pVar->getValueAsInt() != 0 );
        }

        StringBuilder<constant::kMaxBuffer64> nameBuilder;
        for ( uint32 index = 0; index < meshCount; ++index )
        {
            nameBuilder.clear();
            nameBuilder.append( "BenchMesh_" ).append( index );

            GameObject* pObject = pObjects->createGameObject( hashed_string( nameBuilder.c_str(), nameBuilder.size() ) );
            if ( pObject == nullptr )
                continue;
            MeshComponent* pMesh = pObject->addComponent<MeshComponent>();
            if ( pMesh == nullptr )
                continue;

            const uint32 col = index % side;
            const uint32 row = index / side;
            pMesh->setMesh( cube );
            // 기본 머티리얼은 씬 **로드** 경로(bindSceneMeshDefaults)에서만 붙는다.
            // createScene 으로 직접 만든 씬은 그 단계를 지나지 않으므로 여기서 붙여준다 —
            // 없으면 배치는 만들어지는데 화면에는 아무것도 안 나온다.
            if ( pSceneMaterial != nullptr )
            {
                pMesh->setMaterial( pSceneMaterial );

                // 큐브마다 자기 머티리얼 인스턴스를 준다. 색이 달라지는 것도 목적이지만, 배치 키가
                // 인스턴스 포인터를 포함하므로 배치가 1개에서 N개로 갈라진다 — 배치·드로우 경로가
                // 그제야 실제 부하를 받는다(전부 같은 인스턴스면 drawInstanced 한 번으로 끝난다).
                //
                // 다만 DX12 에서는 이게 기존 커맨드 얼로케이터 버그를 100% 터뜨린다(아래 참고).
                // 기본 벤치가 네 백엔드에서 다 돌아야 하므로 옵트인으로 둔다.
                if ( bPerCubeMaterial )
                {
                    shared_ptr<MaterialInstance> instance = make_shared<MaterialInstance>( pSceneMaterial );
                    instance->setVectorParameter( hashed_string( "color" ), makeBenchColor( index ) );
                    pMesh->setMaterialInstance( std::move( instance ) );
                }
            }
            // 격자를 원점 기준으로 X/Z 양쪽에 펼친다 — 카메라를 정면에서 뒤로 빼면 전부 들어온다.
            pMesh->setLocalPosition( float3{ origin + static_cast<float32>( col ) * kBenchSpacing,
                                             0.0f,
                                             origin + static_cast<float32>( row ) * kBenchSpacing } );
            pMesh->setVisible( true );
            _listBenchMesh.push_back( pMesh );
        }

        spawnBenchLight( pScene, halfExtentOf( side, kBenchSpacing ) );
        _benchGridSide = side;
        frameBenchCamera( pScene, side, kBenchSpacing );

        SW_LOG_INFO( "[Bench] 씬 '%#' 에 큐브 %#개를 %#x%# 격자로 만들었습니다.",
                     pScene->getName(), static_cast<uint32>( _listBenchMesh.size() ), side, side );
    }

    float4 EmptyGame::makeBenchColor( uint32 index )
    {
        // 결정적 해시 — 실행마다 같은 그림이 나와야 스크린샷 비교가 의미를 갖는다.
        uint32 hash = index * 2654435761u;
        hash ^= hash >> 15;
        const float32 r = static_cast<float32>( ( hash >> 0 ) & 0xFFu ) / 255.0f;
        const float32 g = static_cast<float32>( ( hash >> 8 ) & 0xFFu ) / 255.0f;
        const float32 b = static_cast<float32>( ( hash >> 16 ) & 0xFFu ) / 255.0f;
        // 너무 어두우면 조명 확인이 어려우므로 아래를 들어 올린다.
        return float4{ 0.35f + r * 0.65f, 0.35f + g * 0.65f, 0.35f + b * 0.65f, 1.0f };
    }

    float32 EmptyGame::halfExtentOf( uint32 side, float32 spacing )
    {
        return 0.5f * static_cast<float32>( side ) * spacing;
    }

    void EmptyGame::spawnBenchLight( Scene* pScene, float32 halfExtent )
    {
        if ( pScene == nullptr )
            return;
        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
            return;

        GameObject* pLightObject = pObjects->createGameObject( hashed_string( "BenchKeyLight" ) );
        if ( pLightObject == nullptr )
            return;
        DirectionalLightComponent* pLight = pLightObject->addComponent<DirectionalLightComponent>();
        if ( pLight == nullptr )
            return;

        // 그림자 볼륨은 씬을 덮어야 한다. 엔진 기본값은 2 유닛이라 142 유닛 격자에서는
        // 그림자가 원점 근처 몇 개에만 걸린다.
        pLight->setShadowExtent( halfExtent * 1.15f );
        pLight->setShadowDistance( halfExtent * 1.5f );
        pLight->setCastShadow( true );
        pLight->setIntensity( 1.6f );

        SW_LOG_INFO( "[Bench] 주광을 만들었습니다 (그림자 볼륨 반경 %#).", static_cast<int32>( halfExtent * 1.15f ) );
    }

    void EmptyGame::frameBenchCamera( Scene* pScene, uint32 side, float32 spacing )
    {
        if ( pScene == nullptr )
            return;
        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
            return;

        // 씬의 **모든** 카메라를 맞춘다. 에디터 GameView 는 게임 카메라가 아니라 자기 뷰포트
        // 카메라로 그리므로(App::getEditorViewCamera), 게임 카메라만 옮기면 에디터에서는
        // 아무것도 안 보인다 — 실제로 그 이유로 한참 헤맸다.
        pObjects->forEachGameObject( [&]( GameObject* pObj )
        {
            if ( pObj == nullptr )
                return;
            if ( CameraComponent* pCam = pObj->getComponent<CameraComponent>() )
                frameOneCamera( pCam, side, spacing );
        } );
    }

    void EmptyGame::frameOneCamera( CameraComponent* pCamera, uint32 side, float32 spacing )
    {
        if ( pCamera == nullptr )
            return;

        // 격자 한 변의 절반이 시야각 안에 들어오는 거리로 뺀다. 기본 카메라는 (0, 1.2, 3.2) 라
        // 큐브 5000 개(71x71 = 140 유닛)면 화면에 몇 개밖에 안 걸린다 — 그러면 드로우는 도는데
        // 눈으로 확인할 수가 없다.
        const float32 halfExtent = halfExtentOf( side, spacing );
        const float32 fovY       = pCamera->getFieldOfViewY();
        const float32 tanHalf    = MathUtil::tan( fovY * 0.5f );
        const float32 distance   = ( tanHalf > MathUtil::Epsilon ) ? ( halfExtent / tanHalf ) : ( halfExtent * 2.0f );

        // 격자는 카메라 축(Z)으로도 ±halfExtent 펼쳐져 있다. 중심까지의 거리만 쓰면 가까운 쪽이
        // 화면을 넘치고, 깊이까지 다 빼면 격자가 점처럼 작아진다 — 절반만 더한다.
        // 높이는 낮게 둔다. 높이 올려 내려다보면 격자가 화면 아래쪽으로 쏠린다.
        const float32 height = halfExtent * kBenchCameraMargin;
        pCamera->setLocalPosition( float3{ 0.0f, height, -( distance + halfExtent * 0.5f ) } );
        pCamera->setFarPlane( MathUtil::max( pCamera->getFarPlane(), ( halfExtent + distance ) * 3.0f ) );
        pCamera->lookAt( float3{ 0.0f, 0.0f, 0.0f } );

        SW_LOG_INFO( "[Bench] 카메라 '%#' 을 격자(반경 %#)에 맞춰 z=%# 로 물렸습니다.",
                     pCamera->getComponentName().c_str(), static_cast<int32>( halfExtent ),
                     static_cast<int32>( -( distance + halfExtent * 0.5f ) ) );
    }

    void EmptyGame::updateBenchScene( float32 deltaTime )
    {
        _benchElapsed += deltaTime;

        // 에디터 뷰포트 카메라는 게임 onInitialize 보다 늦게 만들어진다. 한 번만 다시 맞춘다.
        if ( _bRefreshedCameras == 0 && _benchElapsed > 0.5f )
        {
            _bRefreshedCameras = 1;
            if ( SceneManager* pSceneManager = game::getService<SceneManager>() )
            {
                if ( Scene* pScene = pSceneManager->getActiveScene() )
                    frameBenchCamera( pScene, _benchGridSide, kBenchSpacing );
            }
        }

        const uint32 count = static_cast<uint32>( _listBenchMesh.size() );
        for ( uint32 index = 0; index < count; ++index )
        {
            MeshComponent* pMesh = _listBenchMesh[index];
            if ( pMesh == nullptr )
                continue;

            // 인덱스마다 위상을 어긋나게 해 전부 같은 값이 되지 않도록 한다 — 전부 같으면
            // 배치 키는 물론 트랜스폼까지 동일해져 실제와 다른(너무 좋은) 결과가 나온다.
            const float32 phase = static_cast<float32>( index ) * 0.37f;
            const float32 wave  = MathUtil::sin( _benchElapsed + phase );

            float3 position = pMesh->getLocalPosition();
            position._y     = wave * 0.75f;
            pMesh->setLocalPosition( position );
            pMesh->setLocalRotation( float3{ 0.0f, ( _benchElapsed + phase ) * 45.0f, 0.0f } );

            // 스케일도 흔든다 — 위치·회전만 바꾸면 월드 행렬의 회전/이동 성분만 갱신되므로
            // 스케일 경로(및 바운드 반지름을 쓰는 컬링)가 검증되지 않는다.
            const float32 scale = 0.6f + 0.4f * MathUtil::abs( wave );
            pMesh->setLocalScale( float3{ scale, scale, scale } );
        }
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::EmptyGame );
