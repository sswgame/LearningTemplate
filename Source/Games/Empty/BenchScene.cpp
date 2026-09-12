#include "pch.h"

#include "Games/Empty/BenchScene.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/3D/SpotLightComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/GameService.h"

#include "Games/Empty/EmptyGlobalVariable.h"

namespace sw
{
    namespace
    {
        /** @brief 큐브 사이 간격(월드 단위). */
        constexpr float32 kBenchSpacing = 2.0f;
        /** @brief 격자가 화면에 들어오도록 카메라를 뒤로 뺄 때 쓰는 여유 배수. */
        constexpr float32 kBenchCameraMargin = 0.28f;
        /** @brief 스트레스가 들고 있을 머티리얼 인스턴스 상한. 넘으면 붙이는 대신 뗀다. */
        constexpr size_t kChurnInstanceCap = 512;
        /** @brief 스트레스가 흔드는 정적 스위치 키워드 (defaultmaterial.material 의 useNormalMap). */
        constexpr const utf8* kChurnNormalMapKeyword = "MATERIAL_NORMALMAP";
        /** @brief 스트레스가 흔드는 멀티컴파일 이름. */
        constexpr const utf8* kChurnFogMultiCompile = "FogMode";
        /** @brief 그 멀티컴파일의 선택지 — 머티리얼 에셋의 _multiCompiles 와 같아야 한다. */
        constexpr const utf8* kArrChurnFogOption[] = { "FOG_OFF", "FOG_LINEAR", "FOG_EXP" };
        /**
         * @brief 벤치가 섞어 쓸 도형 — `MeshUtil::createPrimitive` 가 아는 이름이다.
         * @note 큐브가 **첫 번째**여야 한다. `-gv_benchMeshShapes=1`(기본)이 예전과 같은 그림을 내야
         *       기존 측정·스크린샷이 그대로 유효하다.
         */
        constexpr const utf8* kArrBenchShape[] = { "Cube", "Sphere", "Cylinder", "Capsule", "Cone" };
    } // namespace

    BenchScene::BenchScene()
        : _listBenchMesh{}
        , _keyLight{}
        , _glassMaterial{ nullptr }
        , _listChurnInstance{}
        , _churnRandom{ 0x9E3779B9u }
        , _churnFrame{ 0 }
        , _benchElapsed{ 0.0f }
        , _benchGridSide{ 0 }
        , _bRefreshedCameras{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    BenchScene::~BenchScene() = default;

    bool BenchScene::spawnFromGlobals()
    {
        // 커맨드라인은 게임에 열려 있지 않지만(CommandLineManager gameAllowed=0), 이 스위치는 이제
        // **이 모듈이 선언한다**(EmptyGlobalVariable.cpp). 그래서 이름으로 조회하지 않고 그대로 읽는다 —
        // 예전 문자열 조회는 이름을 잘못 쓰면 조용히 0 으로 읽혔다.
        if ( gv_benchMeshes <= 0 )
            return false;

        spawn( static_cast<uint32>( gv_benchMeshes ) );
        return isActive();
    }

    void BenchScene::despawn()
    {
        SceneManager*      pSceneManager = game::getService<SceneManager>();
        Scene*             pScene        = ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
        GameObjectManager* pObjects      = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
        if ( pObjects != nullptr )
        {
            // 핸들이 해석되면 그 소유 오브젝트를 지운다. 이미 없으면(씬이 바뀌었으면) 할 일이 없다.
            for ( const ComponentHandle& handle : _listBenchMesh )
            {
                if ( Component* pComp = pObjects->resolveComponent( handle ) )
                    pObjects->destroyObject( pComp->getOwner() );
            }
            if ( Component* pLight = pObjects->resolveComponent( _keyLight ) )
                pObjects->destroyObject( pLight->getOwner() );
        }
        _listBenchMesh.clear();
        _listChurnInstance.clear();
        _keyLight = {};
    }

    void BenchScene::spawn( uint32 meshCount )
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

        // 메시 종류 수. 배치 키에 메시가 들어가므로 종류가 곧 **배치 수**다 — 1 이면 배치가 하나로 묶여
        // 드로우 경로(드로우별 상수·바인딩)를 전혀 재지 못한다. 실제 씬은 늘 여러 메시를 쓴다.
        uint32 meshVariantCount = static_cast<uint32>( MathUtil::max( 1, gv_benchMeshVariants ) );
        meshVariantCount        = MathUtil::min( meshVariantCount, meshCount );

        // 도형을 몇 종 섞을지. 1 이면 예전 그대로 큐브만 쓴다(기존 측정·스크린샷 보존).
        const uint32 shapeCount = static_cast<uint32>(
            MathUtil::clamp( gv_benchMeshShapes, 1, static_cast<int32>( SW_COUNT_OF( kArrBenchShape ) ) ) );

        // 같은 기하를 여러 객체로 만든다 — 배치만 갈린다. 도형을 섞으면 배치마다 정점 수가 달라져
        // 간접 인자·정점 버퍼 바인딩·바운드 반경이 전부 다른 값을 탄다.
        vector<shared_ptr<Mesh>> listMeshVariant;
        listMeshVariant.reserve( meshVariantCount );
        for ( uint32 variantIndex = 0; variantIndex < meshVariantCount; ++variantIndex )
        {
            const utf8*      pShapeId = kArrBenchShape[variantIndex % shapeCount];
            shared_ptr<Mesh> variant  = MeshUtil::createPrimitive( pShapeId );
            if ( variant == nullptr )
            {
                SW_LOG_ERROR( "[Bench] 도형 '%#' 을 만들지 못했습니다.", pShapeId );
                return;
            }
            // GPU 모프 옵트인 — 유니티의 vertexBufferTarget 옵트인과 같은 자리다. 켠 메시만 풀에 들어간다.
            if ( gv_benchMeshMorph != 0 )
                variant->setGpuMorphEnabled( true );
            listMeshVariant.push_back( std::move( variant ) );
        }

        // 정사각에 가까운 격자로 흩는다. 전부 같은 메시·머티리얼이라 배치가 하나로 묶이는데,
        // 그게 지금 렌더러의 기본 경로(배치당 drawInstanced 한 번)를 재는 조건이다.
        const uint32  side   = static_cast<uint32>( MathUtil::sqrt( static_cast<float32>( meshCount ) ) ) + 1;
        const float32 origin = -0.5f * static_cast<float32>( side - 1 ) * kBenchSpacing;

        _listBenchMesh.clear();
        _listBenchMesh.reserve( meshCount );

        // 큐브별 머티리얼 인스턴스는 DX12 크래시를 재현하는 용도라 기본은 꺼 둔다.
        const bool bPerCubeMaterial = ( gv_benchMaterialInstances != 0 );

        const uint32 transparentPercent =
            ( gv_benchTransparent > 0 ) ? static_cast<uint32>( MathUtil::min( gv_benchTransparent, 100 ) ) : 0u;

        // 투명은 **별도 머티리얼 에셋**이다 — 블렌드 모드가 머티리얼의 성질이고 알파 사용 여부가
        // 셰이더 퍼뮤테이션(MATERIAL_BLEND_TRANSLUCENT)을 가르기 때문이다. 메시에 플래그를 세우는
        // 방식이었을 때는 불투명으로 컴파일된 머티리얼을 블렌딩으로 그리는 어긋난 상태가 됐다.
        //
        // 인스턴스는 **소수만 만들어 돌려 쓴다** — 큐브마다 하나씩 주면 배치가 인스턴스마다 갈려 한
        // 배치에 투명 인스턴스가 하나뿐이 되고, 그러면 배치 안의 정렬(instancesort 가 되돌리는 그 순서)이
        // 한 번도 검사되지 않는다.
        constexpr uint32             kTransparentMaterialCount = 3;
        shared_ptr<MaterialInstance> arrTransparentMaterial[kTransparentMaterialCount];
        if ( transparentPercent > 0 )
        {
            if ( _glassMaterial == nullptr )
            {
                _glassMaterial = Material::create();
                if ( _glassMaterial->loadFromFile( "engine/materials/glassmaterial.material" ) == false )
                {
                    SW_LOG_WARNING( "[Bench] 투명 머티리얼을 읽지 못했습니다 — 투명 큐브를 건너뜁니다." );
                    _glassMaterial.reset();
                }
            }
            for ( uint32 slot = 0; slot < kTransparentMaterialCount && _glassMaterial != nullptr; ++slot )
            {
                shared_ptr<MaterialInstance> instance = MaterialInstance::create( _glassMaterial.get() );
                const float4                 tint     = makeBenchColor( slot * 977u + 13u );
                // 알파를 눈에 띄게 낮춘다 — 1.0 에 가까우면 블렌딩이 됐는지 그림으로 구분할 수 없다.
                const float32 alpha = 0.30f + 0.15f * static_cast<float32>( slot );
                instance->setVectorParameter( hashed_string( "color" ), float4{ tint._x, tint._y, tint._z, alpha } );
                _listChurnInstance.push_back( instance );
                arrTransparentMaterial[slot] = std::move( instance );
            }
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
            pMesh->setMesh( listMeshVariant[index % meshVariantCount] );
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
                    shared_ptr<MaterialInstance> instance = MaterialInstance::create( pSceneMaterial );
                    instance->setVectorParameter( hashed_string( "color" ), makeBenchColor( index ) );
                    _listChurnInstance.push_back( instance );
                    pMesh->setMaterialInstance( std::move( instance ) );
                }
            }
            // 격자를 원점 기준으로 X/Z 양쪽에 펼친다 — 카메라를 정면에서 뒤로 빼면 전부 들어온다.
            pMesh->setLocalPosition( float3{ origin + static_cast<float32>( col ) * kBenchSpacing,
                                             0.0f,
                                             origin + static_cast<float32>( row ) * kBenchSpacing } );
            // 일부를 투명으로 — 블렌드 모드가 배치를 가르고, 컬링이 압축한 순서를 instancesort 가
            // 깊이순으로 되돌린다. 투명 큐브끼리는 머티리얼 인스턴스를 나눠 쓰므로 한 배치에 여럿 들어간다.
            if ( isBenchTransparent( index, transparentPercent ) && arrTransparentMaterial[0] != nullptr )
            {
                // 블렌드 모드는 머티리얼이 정한다 — 메시에 따로 세우지 않는다.
                pMesh->setMaterial( _glassMaterial.get() );
                pMesh->setMaterialInstance( arrTransparentMaterial[index % kTransparentMaterialCount] );
            }

            // GPU 가 이 큐브를 돌린다 — 시드가 각속도와 방향을 정하므로 큐브마다 속도가 다르다.
            // 0 은 "돌리지 않음"이라 인덱스에 1 을 더한다. CPU 는 이제 회전을 계산하지 않는다.
            pMesh->setGpuSpinSeed( index + 1u );
            pMesh->setVisible( true );
            _listBenchMesh.push_back( pMesh->getHandle() );
        }

        spawnLight( pScene, halfExtentOf( side, kBenchSpacing ) );
        spawnBenchLights( pScene, halfExtentOf( side, kBenchSpacing ) );
        spawnGround( pScene, halfExtentOf( side, kBenchSpacing ) );
        _benchGridSide = side;
        frameCameras( pScene, side, kBenchSpacing );

        SW_LOG_INFO( "[Bench] 메시 종류 %#개 (= 배치 수), 도형 %#종. -gv_benchMeshVariants · -gv_benchMeshShapes 로 바꾼다.",
                     meshVariantCount, shapeCount );
        SW_LOG_INFO( "[Bench] 씬 '%#' 에 큐브 %#개를 %#×%# 격자로 만들었습니다.",
                     pScene->getName(), static_cast<uint32>( _listBenchMesh.size() ), side, side );
    }

    float4 BenchScene::makeBenchColor( uint32 index )
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

    bool BenchScene::isBenchTransparent( uint32 index, uint32 percent )
    {
        if ( percent == 0 )
            return false;
        // 격자 위치와 무관하게 흩어져야 한다 — 인덱스를 그대로 나누면 줄 단위로 뭉친다.
        uint32 hash = index * 2246822519u;
        hash ^= hash >> 13;
        hash *= 3266489917u;
        hash ^= hash >> 16;
        return ( hash % 100u ) < percent;
    }

    float32 BenchScene::halfExtentOf( uint32 side, float32 spacing )
    {
        return 0.5f * static_cast<float32>( side ) * spacing;
    }

    void BenchScene::spawnLight( Scene* pScene, float32 halfExtent )
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
        _keyLight = pLight->getHandle();

        // 그림자 볼륨은 씬을 덮어야 한다. 엔진 기본값은 2 유닛이라 142 유닛 격자에서는
        // 그림자가 원점 근처 몇 개에만 걸린다.
        pLight->setShadowExtent( halfExtent * 1.15f );
        pLight->setShadowDistance( halfExtent * 1.5f );
        pLight->setCastShadow( true );
        pLight->setIntensity( 1.6f );

        SW_LOG_INFO( "[Bench] 주광을 만들었습니다 (그림자 볼륨 반경 %#).", static_cast<int32>( halfExtent * 1.15f ) );
    }

    void BenchScene::spawnBenchLights( Scene* pScene, float32 halfExtent )
    {
        const int32 lightCount = gv_benchLights;
        if ( pScene == nullptr || lightCount <= 0 )
            return;
        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
            return;

        // 반경은 비용을 정하는 값이다 — 기본값은 격자 간격의 몇 배로 두어, 한 픽셀에 라이트가
        // 여럿 닿게 한다(그래야 루프가 실제로 돌아 라이트 수에 따른 비용이 측정된다).
        const float32 radius = ( gv_benchLightRadius > 0.0f ) ? gv_benchLightRadius : ( kBenchSpacing * 3.0f );

        for ( int32 lightIndex = 0; lightIndex < lightCount; ++lightIndex )
        {
            // 결정적 해시 — 실행마다 같은 자리에 놓여야 스크린샷 비교가 성립한다.
            uint32 hash = static_cast<uint32>( lightIndex ) * 2654435761u;
            hash ^= hash >> 15;
            hash *= 2246822519u;
            hash ^= hash >> 13;

            const float32 unitX = static_cast<float32>( ( hash >> 0 ) & 0x3FFu ) / 1023.0f;
            const float32 unitY = static_cast<float32>( ( hash >> 10 ) & 0x3FFu ) / 1023.0f;
            const float32 unitZ = static_cast<float32>( ( hash >> 20 ) & 0x3FFu ) / 1023.0f;

            const float3 position{ ( unitX * 2.0f - 1.0f ) * halfExtent,
                                   1.0f + unitY * 3.0f,
                                   ( unitZ * 2.0f - 1.0f ) * halfExtent };
            const float3 color{ 0.3f + unitZ * 0.7f, 0.3f + unitX * 0.7f, 0.3f + unitY * 0.7f };

            GameObject* pLightObject = pObjects->createGameObject( hashed_string( "BenchLight" ) );
            if ( pLightObject == nullptr )
                continue;

            // 홀수 번째는 스포트라이트 — 점광만 두면 원뿔 감쇠 분기가 한 번도 실행되지 않는다.
            if ( ( lightIndex & 1 ) != 0 )
            {
                SpotLightComponent* pSpot = pLightObject->addComponent<SpotLightComponent>();
                if ( pSpot == nullptr )
                    continue;
                pSpot->setColor( color );
                pSpot->setIntensity( 4.0f );
                pSpot->setRadius( radius );
                pSpot->setOuterConeAngle( 0.6f );
                pSpot->setInnerConeAngle( 0.25f );
                pSpot->setLocalPosition( position ); // 회전은 주지 않는다 — 기본 방향이 아래다
                continue;
            }

            PointLightComponent* pPoint = pLightObject->addComponent<PointLightComponent>();
            if ( pPoint == nullptr )
                continue;
            pPoint->setColor( color );
            pPoint->setIntensity( 2.5f );
            pPoint->setRadius( radius );
            pPoint->setLocalPosition( position );
        }

        SW_LOG_INFO( "[Bench] 라이트 %#개를 흩뿌렸습니다 (반경 %#, 절반은 스포트). -gv_benchLightRadius 로 반경을 바꾼다.",
                     lightCount, static_cast<int32>( radius ) );
    }

    void BenchScene::spawnGround( Scene* pScene, float32 halfExtent )
    {
        if ( pScene == nullptr || gv_benchGround == 0 )
            return;
        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
            return;

        GameObject* pGroundObject = pObjects->createGameObject( hashed_string( "BenchGround" ) );
        if ( pGroundObject == nullptr )
            return;
        MeshComponent* pMesh = pGroundObject->addComponent<MeshComponent>();
        if ( pMesh == nullptr )
            return;

        // 격자보다 넉넉히 크게 — 그림자 볼륨 가장자리가 화면 안에 들어오면 "그림자가 잘렸다" 로 오인한다.
        const float32 size = halfExtent * 2.6f;
        pMesh->setMesh( MeshUtil::createPlane( 24 ) );
        pMesh->setLocalScale( float3{ size, 1.0f, size } );
        // 평면의 면은 로컬 y = +0.5 다(MeshUtil::createPlane 주석) — 큐브 아랫면(-0.5)보다 조금 더 아래로 내린다.
        pMesh->setLocalPosition( float3{ 0.0f, -1.1f, 0.0f } );
        pMesh->setVisible( true );

        SW_LOG_INFO( "[Bench] 바닥 평면을 깔았습니다 (한 변 %#).", static_cast<int32>( size ) );
    }

    void BenchScene::frameCameras( Scene* pScene, uint32 side, float32 spacing )
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

    void BenchScene::frameOneCamera( CameraComponent* pCamera, uint32 side, float32 spacing )
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

    void BenchScene::update( float32 deltaTime )
    {
        _benchElapsed += deltaTime;

        // 에디터 뷰포트 카메라는 게임 onInitialize 보다 늦게 만들어진다. 한 번만 다시 맞춘다.
        if ( _bRefreshedCameras == SW_FALSE && _benchElapsed > 0.5f )
        {
            _bRefreshedCameras = SW_TRUE;
            if ( SceneManager* pSceneManager = game::getService<SceneManager>() )
            {
                if ( Scene* pScene = pSceneManager->getActiveScene() )
                    frameCameras( pScene, _benchGridSide, kBenchSpacing );
            }
        }

        // 큐브는 씬이 소유한다. 여기 있는 것은 핸들뿐이라 씬이 통째로 바뀌어도(상태 복원) 죽은 주소를
        // 건드릴 수 없다 — 해석이 nullptr 이면 그 큐브는 이미 없는 것이다.
        SceneManager*      pSceneManager = game::getService<SceneManager>();
        Scene*             pScene        = ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
        GameObjectManager* pObjects      = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
        if ( pObjects == nullptr )
            return;

        updateMaterialChurn( pObjects );

        const uint32 count = static_cast<uint32>( _listBenchMesh.size() );
        for ( uint32 index = 0; index < count; ++index )
        {
            // 핸들은 MeshComponent 에서 만들었으므로 해석 결과도 MeshComponent 다.
            MeshComponent* pMesh = static_cast<MeshComponent*>( pObjects->resolveComponent( _listBenchMesh[index] ) );
            if ( pMesh == nullptr )
                continue;

            // 인덱스마다 위상을 어긋나게 해 전부 같은 값이 되지 않도록 한다 — 전부 같으면
            // 배치 키는 물론 트랜스폼까지 동일해져 실제와 다른(너무 좋은) 결과가 나온다.
            const float32 phase = static_cast<float32>( index ) * 0.37f;
            const float32 wave  = MathUtil::sin( _benchElapsed + phase );

            float3 position = pMesh->getLocalPosition();
            position._y     = wave * 0.75f;
            pMesh->setLocalPosition( position );
            // 회전은 **컴퓨트가 만든다**(instanceanim.hlsl). 예전엔 여기서 전부 45도/초로 돌렸는데,
            // 속도가 하나뿐이라 큐브가 몇 천 개여도 한 덩어리처럼 보였다. 지금은 시드 해시가
            // 인스턴스마다 속도와 방향을 갈라 준다. CPU 가 여기서 회전을 다시 쓰면 GPU 가 쓴 값을
            // 다음 업로드가 덮어써 도로 균일해진다 — 그래서 회전은 CPU 가 손대지 않는다.

            // 스케일도 흔든다 — 위치·회전만 바꾸면 월드 행렬의 회전/이동 성분만 갱신되므로
            // 스케일 경로(및 바운드 반지름을 쓰는 컬링)가 검증되지 않는다.
            const float32 scale = 0.6f + 0.4f * MathUtil::abs( wave );
            pMesh->setLocalScale( float3{ scale, scale, scale } );
        }
    }

    uint32 BenchScene::nextChurnRandom()
    {
        // xorshift32. 표준 난수를 쓰지 않는 이유는 **재현**이다 — 같은 프레임 수를 돌리면 같은 순서가
        // 나와야 스크린샷과 로그를 실행 사이에 비교할 수 있다.
        _churnRandom ^= _churnRandom << 13;
        _churnRandom ^= _churnRandom >> 17;
        _churnRandom ^= _churnRandom << 5;
        return _churnRandom;
    }

    void BenchScene::releaseChurnInstance( const MaterialInstance* pInstance )
    {
        if ( pInstance == nullptr )
            return;
        for ( size_t slot = 0; slot < _listChurnInstance.size(); ++slot )
        {
            if ( _listChurnInstance[slot].get() != pInstance )
                continue;
            // swap-and-pop. 순서는 의미가 없다 — 무작위로 고르는 목록이다.
            _listChurnInstance[slot] = _listChurnInstance.back();
            _listChurnInstance.pop_back();
            return;
        }
    }

    void BenchScene::churnInstanceValue( MaterialInstance* pInstance )
    {
        if ( pInstance == nullptr )
            return;

        if ( ( nextChurnRandom() & 1u ) != 0u )
        {
            // 알파는 부모를 따라간다 — 유리 머티리얼의 알파를 1 로 올려 버리면 투명 경로가 그림에서
            // 사라져, 흔들고 있다는 사실 자체가 검증을 망가뜨린다.
            const float4  tint   = makeBenchColor( nextChurnRandom() );
            const bool    bGlass = ( pInstance->getParent() != nullptr ) && ( pInstance->getParent() == _glassMaterial.get() );
            const float32 alpha  = bGlass ? ( 0.25f + 0.35f * static_cast<float32>( nextChurnRandom() & 0xFFu ) / 255.0f ) : 1.0f;
            pInstance->setVectorParameter( hashed_string( "color" ), float4{ tint._x, tint._y, tint._z, alpha } );
        }
        else
        {
            const float32 roughness = static_cast<float32>( nextChurnRandom() & 0xFFFFu ) / 65535.0f;
            pInstance->setScalarParameter( hashed_string( "roughness" ), roughness );
        }
    }

    void BenchScene::updateMaterialChurn( GameObjectManager* pObjects )
    {
        const uint32 valueChurn   = ( gv_benchMaterialChurn > 0 ) ? static_cast<uint32>( gv_benchMaterialChurn ) : 0u;
        const uint32 addChurn     = ( gv_benchMaterialChurnAdd > 0 ) ? static_cast<uint32>( gv_benchMaterialChurnAdd ) : 0u;
        const uint32 keywordEvery = ( gv_benchMaterialChurnKeyword > 0 ) ? static_cast<uint32>( gv_benchMaterialChurnKeyword ) : 0u;
        if ( ( valueChurn | addChurn | keywordEvery ) == 0u )
            return;

        const uint32 cubeCount = static_cast<uint32>( _listBenchMesh.size() );
        if ( pObjects == nullptr || cubeCount == 0 )
            return;

        ++_churnFrame;

        // 1) 값만 흔든다 — 배치 구성은 그대로고 머티리얼 바이트만 바뀐다.
        for ( uint32 step = 0; step < valueChurn && _listChurnInstance.empty() == false; ++step )
            churnInstanceValue( _listChurnInstance[nextChurnRandom() % _listChurnInstance.size()].get() );

        // 2) 집합을 흔든다 — 붙이면 배치가 갈리고 원소가 늘고, 떼면 회수·재사용이 돈다.
        for ( uint32 step = 0; step < addChurn; ++step )
        {
            MeshComponent* pMesh =
                static_cast<MeshComponent*>( pObjects->resolveComponent( _listBenchMesh[nextChurnRandom() % cubeCount] ) );
            if ( pMesh == nullptr )
                continue;

            // 목록이 상한에 닿으면 떼는 쪽으로 기운다. 상한이 없으면 큐브 수만큼 늘어나 이 스위치가
            // 사실상 -gv_benchMaterialInstances 와 같아지고, "붙였다 뗐다" 를 재지 못한다.
            const bool bDrop = ( _listChurnInstance.size() >= kChurnInstanceCap ) || ( ( nextChurnRandom() & 3u ) == 0u );
            if ( bDrop )
            {
                const shared_ptr<MaterialInstance> dropped = pMesh->getMaterialInstance();
                if ( dropped == nullptr )
                    continue;
                // 큐브에서 떼고 **우리 목록에서도 놓는다.** 목록에 남겨 두면 참조가 사라지지 않아
                // 회수 경로가 영영 돌지 않는다. 렌더 패킷이 아직 들고 있으면 거기서 마지막으로 죽는다.
                pMesh->setMaterialInstance( nullptr );
                releaseChurnInstance( dropped.get() );
                continue;
            }

            Material* pParent = pMesh->getMaterial();
            if ( pParent == nullptr )
                continue;
            // 부모는 **그 큐브가 쓰는 머티리얼**이어야 한다 — 불투명 큐브에 유리 부모를 주면 블렌드
            // 모드가 뒤집혀 그림이 달라지고, 스트레스가 아니라 버그가 된다.
            shared_ptr<MaterialInstance> instance = MaterialInstance::create( pParent );
            churnInstanceValue( instance.get() );
            _listChurnInstance.push_back( instance );
            pMesh->setMaterialInstance( std::move( instance ) );
        }

        // 3) 퍼뮤테이션을 흔든다 — 셰이더가 다시 컴파일되므로 주기를 길게 준다.
        if ( keywordEvery > 0 && ( _churnFrame % keywordEvery ) == 0 && _listChurnInstance.empty() == false )
        {
            MaterialInstance* pInstance = _listChurnInstance[nextChurnRandom() % _listChurnInstance.size()].get();
            if ( pInstance != nullptr )
            {
                if ( ( nextChurnRandom() & 1u ) != 0u )
                {
                    if ( ( nextChurnRandom() & 1u ) != 0u )
                        pInstance->enableKeyword( hashed_string( kChurnNormalMapKeyword ) );
                    else
                        pInstance->disableKeyword( hashed_string( kChurnNormalMapKeyword ) );
                }
                else
                {
                    pInstance->setMultiCompile( hashed_string( kChurnFogMultiCompile ),
                                                kArrChurnFogOption[nextChurnRandom() % SW_COUNT_OF( kArrChurnFogOption )] );
                }
            }
        }
    }
} // namespace sw
