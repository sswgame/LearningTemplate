#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Renderer/Light/GpuLightBuffer.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/3D/SpotLightComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/LightRegistry.h"
#include "Engine/Scene/Scene.h"

#include "TestFramework/TestFramework.h"

// SceneLightTest — 라이트 등록부와 그것을 읽어 GPU 원소를 만드는 CPU 절반. 디바이스 없음(nogpu).
//
// 이 경로는 매 프레임 두 곳(`EngineLoop` · `FrameRenderer`)이 부르는데 테스트가 하나도 없었다.
// 여기가 틀리면 증상은 "빛이 하나 사라졌다 / 그림자가 엉뚱한 빛에 붙었다" 라서 셰이더를 먼저
// 의심하게 된다 — 그 전에 걸러 낸다.

namespace
{
    /** @brief 라이트 하나를 든 오브젝트를 만든다. 컴포넌트를 돌려준다. */
    template <typename TLight>
    TLight* addLightObject( sw::GameObjectManager* pObjects, const utf8* pName )
    {
        sw::GameObject* pObject = pObjects->createGameObject( sw::hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        return pObject->addComponent<TLight>();
    }

    /** @brief 타입 태그가 kind 인 원소 수를 셉니다. */
    size_t countLightOfType( const sw::vector<sw::GpuLight>& listLight, uint32 kind )
    {
        size_t count = 0;
        for ( const sw::GpuLight& light : listLight )
        {
            if ( static_cast<uint32>( light._directionType._w ) == kind )
                ++count;
        }
        return count;
    }

    /** @brief 두 방향 사이의 각(도)입니다. */
    float32 angleBetweenDegrees( const sw::float3& lhs, const sw::float3& rhs )
    {
        const float32 cosine = sw::MathUtil::clamp( lhs._x * rhs._x + lhs._y * rhs._y + lhs._z * rhs._z, -1.0f, 1.0f );
        return sw::MathUtil::acos( cosine ) * ( 180.0f / 3.14159265f );
    }

    /** @brief 부모(요 @p parentYaw 라디안) 아래에 로컬 회전 0 인 빛을 붙이고, 빛 방향을 돌려줍니다. 부모 회전이 0 이면 루트와 같습니다. */
    template <typename TLight>
    sw::float3 lightDirectionUnderYawedParent( float32 parentYaw )
    {
        sw::GameObjectManager manager;
        sw::GameObject*       pParent = manager.createGameObject( sw::hashed_string( "LightParent" ) );
        sw::SceneComponent*   pRoot   = pParent->addComponent<sw::SceneComponent>();
        TLight*               pLight  = addLightObject<TLight>( &manager, "RotatedLight" );
        SW_EXPECT_TRUE( pLight->attachToComponent( pRoot ) );
        pRoot->setLocalRotation( sw::float3{ 0.0f, parentYaw, 0.0f } );
        return pLight->getLightDirection();
    }

    /** @brief 루트 빛에 로컬 회전(요 @p yaw 라디안)을 주고 빛 방향을 돌려줍니다. */
    template <typename TLight>
    sw::float3 lightDirectionWithLocalYaw( float32 yaw )
    {
        sw::GameObjectManager manager;
        TLight*               pLight = addLightObject<TLight>( &manager, "YawedLight" );
        pLight->setLocalRotation( sw::float3{ 0.0f, yaw, 0.0f } );
        return pLight->getLightDirection();
    }
} // namespace

/**
 * @brief [SceneLightTest] 빛은 붙을 때 등록부에 들어가고, 떼거나 파괴하면 빠진다
 * @details 최악 실패 모드는 **죽은 컴포넌트를 가리키는 포인터가 등록부에 남는 것**이다 — 그러면
 *          다음 프레임의 수집이 해제된 메모리를 읽는다. 지연 파괴는 플러시까지 남아 있는 것이
 *          정상이므로(그 프레임까지는 살아 있는 오브젝트다) 플러시 전후를 나눠 본다.
 */
SW_TEST_CASE( SceneLightTest, LightsJoinTheRegistryOnAttachAndLeaveWhenRemoved )
{
    sw::Scene              scene( "SceneLightRegistry" );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    sw::DirectionalLightComponent* pSun  = addLightObject<sw::DirectionalLightComponent>( pObjects, "Sun" );
    sw::PointLightComponent*       pLamp = addLightObject<sw::PointLightComponent>( pObjects, "Lamp" );
    sw::SpotLightComponent*        pSpot = addLightObject<sw::SpotLightComponent>( pObjects, "Spot" );
    SW_ASSERT_NOT_NULL( pSun );
    SW_ASSERT_NOT_NULL( pLamp );
    SW_ASSERT_NOT_NULL( pSpot );

    const sw::LightRegistry& registry = pObjects->getLightRegistry();
    SW_EXPECT_EQUAL( size_t( 1 ), registry.getAll( sw::shaderslot::kLightTypeDirectional ).size() );
    SW_EXPECT_EQUAL( size_t( 1 ), registry.getAll( sw::shaderslot::kLightTypePoint ).size() );
    SW_EXPECT_EQUAL( size_t( 1 ), registry.getAll( sw::shaderslot::kLightTypeSpot ).size() );

    // 1) 컴포넌트를 떼면 그 자리에서 빠진다.
    sw::GameObject* pSunOwner = pSun->getOwner();
    SW_ASSERT_NOT_NULL( pSunOwner );
    SW_EXPECT_TRUE( pSunOwner->removeComponent( pSun ) );
    SW_EXPECT_EQUAL( size_t( 0 ), registry.getAll( sw::shaderslot::kLightTypeDirectional ).size() );

    // 2) 오브젝트 파괴는 지연된다 — 예약만으로는 빠지지 않는다(그 프레임까지는 살아 있다).
    sw::GameObject* pLampOwner = pLamp->getOwner();
    SW_ASSERT_NOT_NULL( pLampOwner );
    pObjects->destroyObject( pLampOwner );
    SW_EXPECT_EQUAL( size_t( 1 ), registry.getAll( sw::shaderslot::kLightTypePoint ).size() );

    // 3) 플러시하면 빠진다. 여기서 안 빠지면 등록부가 죽은 포인터를 든다.
    pObjects->processDeferredDestruction();
    SW_EXPECT_EQUAL( size_t( 0 ), registry.getAll( sw::shaderslot::kLightTypePoint ).size() );
    SW_EXPECT_EQUAL( size_t( 1 ), registry.getAll( sw::shaderslot::kLightTypeSpot ).size() );
}

/**
 * @brief [SceneLightTest] 등록은 두 번 해도 하나, 해제는 없는 것을 지워도 조용하다
 * @details 컴포넌트 경로는 한 번씩만 부르지만, 등록부가 스스로 지켜야 하는 성질이다 — 같은 빛이
 *          두 번 들어가면 그 빛만 두 배로 밝아지고(원소가 둘) 원인을 셰이더에서 찾게 된다.
 */
SW_TEST_CASE( SceneLightTest, RegistryIsIdempotentForAddAndRemove )
{
    sw::Scene              scene( "SceneLightIdempotent" );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    sw::PointLightComponent* pLamp = addLightObject<sw::PointLightComponent>( pObjects, "Lamp" );
    SW_ASSERT_NOT_NULL( pLamp );

    sw::LightRegistry& registry = pObjects->getLightRegistry();
    SW_ASSERT_EQUAL( size_t( 1 ), registry.getAll( sw::shaderslot::kLightTypePoint ).size() );

    registry.add( pLamp );
    SW_EXPECT_EQUAL( size_t( 1 ), registry.getAll( sw::shaderslot::kLightTypePoint ).size() );

    registry.add( nullptr );
    SW_EXPECT_EQUAL( size_t( 1 ), registry.getAll( sw::shaderslot::kLightTypePoint ).size() );

    registry.remove( pLamp );
    registry.remove( pLamp );
    registry.remove( nullptr );
    SW_EXPECT_EQUAL( size_t( 0 ), registry.getAll( sw::shaderslot::kLightTypePoint ).size() );
}

/**
 * @brief [SceneLightTest] 빛 하나를 빼도 남은 빛은 등록 순서 그대로다 — "등록된 첫 방향광" 이 빼기마다 바뀌지 않는다
 * @details 그림자를 드리우는 방향광은 등록 순서의 첫 활성 빛이다. 빼기가 맨 뒤 빛을 빈자리로 옮기면(swap-and-pop) 첫 빛을 뺀 뒤 둘째가 아니라
 *          **마지막** 빛이 그 자리를 받아, 씬에서 아무 관계 없는 빛 하나를 지웠을 뿐인데 그림자가 다른 빛으로 넘어갔다.
 */
SW_TEST_CASE( SceneLightTest, RemovingALightKeepsTheOthersInRegistrationOrder )
{
    sw::Scene              scene( "SceneLightOrder" );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    sw::DirectionalLightComponent* pFirst  = addLightObject<sw::DirectionalLightComponent>( pObjects, "SunFirst" );
    sw::DirectionalLightComponent* pSecond = addLightObject<sw::DirectionalLightComponent>( pObjects, "SunSecond" );
    sw::DirectionalLightComponent* pThird  = addLightObject<sw::DirectionalLightComponent>( pObjects, "SunThird" );
    SW_ASSERT_TRUE( pFirst != nullptr && pSecond != nullptr && pThird != nullptr );

    sw::LightRegistry& registry = pObjects->getLightRegistry();
    registry.remove( pFirst );
    const sw::vector<sw::LightComponent*>& listSun = registry.getAll( sw::shaderslot::kLightTypeDirectional );
    SW_ASSERT_EQUAL( size_t( 2 ), listSun.size() );
    SW_EXPECT_TRUE_MSG( listSun[0] == pSecond, "첫 빛을 뺀 뒤 맨 앞이 둘째가 아니라 다른 빛입니다" );
    SW_EXPECT_TRUE( listSun[1] == pThird );
}

/**
 * @brief [SceneLightTest] 수집은 꺼진 컴포넌트와 꺼진 계층의 빛을 건너뛴다
 * @details 등록부는 "무엇이 있나" 만 안다 — 활성 판정은 수집하는 쪽의 몫이라고 두 헤더가 적어
 *          두었다. 그 몫을 실제로 하는지 보는 자리다. 계층 쪽(부모만 끈다)을 따로 보는 이유는
 *          `isActive()` 만 보면 통과하기 때문이다.
 */
SW_TEST_CASE( SceneLightTest, CollectSkipsInactiveComponentsAndInactiveOwners )
{
    sw::Scene              scene( "SceneLightActive" );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    sw::PointLightComponent* pOn       = addLightObject<sw::PointLightComponent>( pObjects, "On" );
    sw::PointLightComponent* pOffComp  = addLightObject<sw::PointLightComponent>( pObjects, "OffComponent" );
    sw::PointLightComponent* pOffOwner = addLightObject<sw::PointLightComponent>( pObjects, "OffOwner" );
    sw::PointLightComponent* pOffChild = addLightObject<sw::PointLightComponent>( pObjects, "OffParentChild" );
    SW_ASSERT_NOT_NULL( pOn );
    SW_ASSERT_NOT_NULL( pOffComp );
    SW_ASSERT_NOT_NULL( pOffOwner );
    SW_ASSERT_NOT_NULL( pOffChild );

    sw::vector<sw::GpuLight> listLight;
    collectSceneLights( &scene, listLight );
    SW_ASSERT_EQUAL( size_t( 4 ), listLight.size() );

    // 1) 컴포넌트를 끈다.
    pOffComp->setActive( false );

    // 2) 소유 오브젝트를 끈다.
    sw::GameObject* pOwner = pOffOwner->getOwner();
    SW_ASSERT_NOT_NULL( pOwner );
    pOwner->setActive( false );

    // 3) 부모만 끈다 — 자식은 자기 깃발로는 여전히 활성이다.
    sw::GameObject* pParent = pObjects->createGameObject( sw::hashed_string( "OffParent" ) );
    SW_ASSERT_NOT_NULL( pParent );
    // 붙이려면 양쪽 모두 기본 씬 컴포넌트가 있어야 한다 — 부모는 빛을 들지 않으므로 직접 준다.
    SW_ASSERT_NOT_NULL( pParent->addComponent<sw::SceneComponent>() );
    sw::GameObject* pChild = pOffChild->getOwner();
    SW_ASSERT_NOT_NULL( pChild );
    SW_ASSERT_TRUE( pChild->attachToParent( pParent ) );
    pParent->setActive( false );
    SW_EXPECT_TRUE( pChild->isActive() );
    SW_EXPECT_FALSE( pChild->isActiveInHierarchy() );

    collectSceneLights( &scene, listLight );
    SW_EXPECT_EQUAL( size_t( 1 ), listLight.size() );

    // 다시 켜면 돌아온다 — 꺼진 채로 빠지는 것이지 등록이 사라진 게 아니다.
    pOffComp->setActive( true );
    pOwner->setActive( true );
    pParent->setActive( true );
    collectSceneLights( &scene, listLight );
    SW_EXPECT_EQUAL( size_t( 4 ), listLight.size() );
}

/**
 * @brief [SceneLightTest] 그림자 슬롯은 그림자를 드리우는 **첫 방향광** 하나만 가져가고, 그림자 행렬도 그 빛에서 나온다
 * @details 그림자 맵이 하나라서 생긴 규칙이다. 둘 다 플래그를 받으면 렌더러는 마지막 것의
 *          행렬로 첫 것을 그린다 — 화면에는 "그림자가 엉뚱한 방향으로 진다" 로 보인다.
 *          행렬(`EngineLoop` · `FrameRenderer`)과 플래그(`collectSceneLights`)가 **같은 선택자**를 쓰는지도 본다. 예전에는 행렬을
 *          "켜진 첫 방향광"(그림자 여부를 안 봄)에서 골라, 첫 빛이 그림자를 끄면 행렬은 비고 플래그는 뒤의 빛에 붙었다.
 */
SW_TEST_CASE( SceneLightTest, OnlyTheFirstShadowCastingDirectionalTakesTheShadowSlot )
{
    sw::Scene              scene( "SceneLightShadow" );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    sw::DirectionalLightComponent* pNoShadow    = addLightObject<sw::DirectionalLightComponent>( pObjects, "SunNoShadow" );
    sw::DirectionalLightComponent* pFirstShadow = addLightObject<sw::DirectionalLightComponent>( pObjects, "SunShadowA" );
    sw::DirectionalLightComponent* pLateShadow  = addLightObject<sw::DirectionalLightComponent>( pObjects, "SunShadowB" );
    SW_ASSERT_NOT_NULL( pNoShadow );
    SW_ASSERT_NOT_NULL( pFirstShadow );
    SW_ASSERT_NOT_NULL( pLateShadow );

    pNoShadow->setCastShadow( false );
    pFirstShadow->setCastShadow( true );
    pLateShadow->setCastShadow( true );

    sw::vector<sw::GpuLight> listLight;
    collectSceneLights( &scene, listLight );
    SW_ASSERT_EQUAL( size_t( 3 ), listLight.size() );

    uint32 shadowCount = 0;
    for ( const sw::GpuLight& light : listLight )
    {
        if ( light._params._x > 0.5f )
            ++shadowCount;
    }
    SW_EXPECT_EQUAL( 1u, shadowCount );

    // 등록 순서대로 실린다 — 그림자를 받은 것은 둘째(첫 그림자 빛)여야 한다.
    SW_EXPECT_TRUE( listLight[0]._params._x < 0.5f );
    SW_EXPECT_TRUE( listLight[1]._params._x > 0.5f );
    SW_EXPECT_TRUE( listLight[2]._params._x < 0.5f );

    // 그림자 행렬을 만드는 쪽이 고르는 빛도 그것이다. 주광(방향 · 색 · 앰비언트)은 여전히 켜진 첫 방향광이다.
    SW_EXPECT_TRUE( scene.findShadowCastingDirectionalLight() == pFirstShadow );
    SW_EXPECT_TRUE( scene.findActiveDirectionalLight() == pNoShadow );

    // 그 빛을 끄면 그림자는 다음 빛으로 넘어간다 — 꺼진 빛이 슬롯을 들고 있으면 안 된다.
    pFirstShadow->setActive( false );
    collectSceneLights( &scene, listLight );
    SW_ASSERT_EQUAL( size_t( 2 ), listLight.size() );
    SW_EXPECT_TRUE( listLight[0]._params._x < 0.5f );
    SW_EXPECT_TRUE( listLight[1]._params._x > 0.5f );
    SW_EXPECT_TRUE( scene.findShadowCastingDirectionalLight() == pLateShadow );

    // 그림자를 드리우는 빛이 없으면 선택자는 nullptr — 행렬은 비고 플래그도 없다.
    pLateShadow->setCastShadow( false );
    SW_EXPECT_NULL( scene.findShadowCastingDirectionalLight() );
    collectSceneLights( &scene, listLight );
    for ( const sw::GpuLight& light : listLight )
        SW_EXPECT_TRUE( light._params._x < 0.5f );
}

/**
 * @brief [SceneLightTest] 빛 방향은 부모의 회전을 따르고, 회전에 대해 연속이다(방향광 · 스포트)
 * @details 예전 방향 함수(두 벌)는 **로컬** 회전이 0 이면 기본 방향을 돌려줬다 — 회전한 부모 아래의 빛은 부모를 무시했다. 그리고 회전이
 *          1e-3 라디안을 넘는 순간 기본 방향에서 전방(+Z)으로 튀었다(방향광이면 비스듬히 내리쬐던 빛이 수평이 된다). 지금은 기본
 *          방향이 로컬 방향이고 월드 회전이 그것을 돌린다. 회전이 없는 루트 빛은 예전과 같은 방향이다(지금의 씬 · 벤치 빛이 모두 그렇다).
 */
SW_TEST_CASE( SceneLightTest, LightDirectionFollowsParentAndIsContinuous )
{
    // 방향광
    const sw::float3 sunRest      = lightDirectionUnderYawedParent<sw::DirectionalLightComponent>( 0.0f );
    const sw::float3 sunTurned    = lightDirectionUnderYawedParent<sw::DirectionalLightComponent>( 1.5707963f );
    const sw::float3 sunNudged    = lightDirectionWithLocalYaw<sw::DirectionalLightComponent>( 0.002f );
    const sw::float3 sunUnrotated = lightDirectionWithLocalYaw<sw::DirectionalLightComponent>( 0.0f );
    SW_EXPECT_TRUE_MSG( angleBetweenDegrees( sunRest, sunTurned ) > 10.0f, "부모를 돌렸는데 빛 방향이 그대로다 — 부모 회전을 무시한다" );
    SW_EXPECT_NEAR_EQUAL( sunRest._y, sunTurned._y, 1e-3f ); // 요는 수직 성분을 바꾸지 않는다
    SW_EXPECT_TRUE_MSG( angleBetweenDegrees( sunUnrotated, sunNudged ) < 1.0f, "회전 0.002 에서 방향이 튀었다" );

    // 스포트 — 기본이 아래(0,-1,0)라 요로는 돌지 않는다. 피치로 본다.
    sw::GameObjectManager   manager;
    sw::SpotLightComponent* pSpot   = addLightObject<sw::SpotLightComponent>( &manager, "SpotUnderParent" );
    sw::GameObject*         pParent = manager.createGameObject( sw::hashed_string( "SpotParent" ) );
    sw::SceneComponent*     pRoot   = pParent->addComponent<sw::SceneComponent>();
    SW_ASSERT_TRUE( pSpot->attachToComponent( pRoot ) );
    const sw::float3 spotRest = pSpot->getLightDirection();
    SW_EXPECT_NEAR_EQUAL( -1.0f, spotRest._y, 1e-4f );
    pRoot->setLocalRotation( sw::float3{ 1.5707963f, 0.0f, 0.0f } );
    const sw::float3 spotTurned = pSpot->getLightDirection();
    SW_EXPECT_TRUE_MSG( angleBetweenDegrees( spotRest, spotTurned ) > 80.0f, "부모를 90도 숙였는데 스포트 방향이 그대로다" );
    pRoot->setLocalRotation( sw::float3{ 0.002f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE_MSG( angleBetweenDegrees( spotRest, pSpot->getLightDirection() ) < 1.0f, "회전 0.002 에서 스포트 방향이 튀었다" );
}

/**
 * @brief [SceneLightTest] 타입 태그·반경·원뿔이 셰이더가 읽는 모양 그대로 실린다
 * @details 원뿔은 **코사인으로** 보낸다(셰이더가 픽셀마다 acos 를 하지 않도록). 각도를 그대로
 *          넣어도 값이 있으니 로그로는 멀쩡해 보이고, 화면에서는 원뿔 경계만 조금 틀린다.
 */
SW_TEST_CASE( SceneLightTest, LightFieldsTravelInTheShaderLayout )
{
    sw::Scene              scene( "SceneLightLayout" );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    sw::DirectionalLightComponent* pSun  = addLightObject<sw::DirectionalLightComponent>( pObjects, "Sun" );
    sw::PointLightComponent*       pLamp = addLightObject<sw::PointLightComponent>( pObjects, "Lamp" );
    sw::SpotLightComponent*        pSpot = addLightObject<sw::SpotLightComponent>( pObjects, "Spot" );
    SW_ASSERT_NOT_NULL( pSun );
    SW_ASSERT_NOT_NULL( pLamp );
    SW_ASSERT_NOT_NULL( pSpot );

    pLamp->setRadius( 12.5f );
    pLamp->setIntensity( 2.5f );
    pLamp->setLocalPosition( sw::float3( 3.0f, 4.0f, 5.0f ) );
    pSpot->setOuterConeAngle( 0.6f );
    pSpot->setInnerConeAngle( 0.2f );

    sw::vector<sw::GpuLight> listLight;
    collectSceneLights( &scene, listLight );
    SW_ASSERT_EQUAL( size_t( 3 ), listLight.size() );

    SW_EXPECT_EQUAL( size_t( 1 ), countLightOfType( listLight, sw::shaderslot::kLightTypeDirectional ) );
    SW_EXPECT_EQUAL( size_t( 1 ), countLightOfType( listLight, sw::shaderslot::kLightTypePoint ) );
    SW_EXPECT_EQUAL( size_t( 1 ), countLightOfType( listLight, sw::shaderslot::kLightTypeSpot ) );

    const sw::GpuLight& point = listLight[1];
    SW_EXPECT_NEAR_EQUAL( 3.0f, point._positionRadius._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, point._positionRadius._y, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, point._positionRadius._z, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 12.5f, point._positionRadius._w, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 2.5f, point._colorIntensity._w, 0.001f );

    const sw::GpuLight& spot = listLight[2];
    SW_EXPECT_NEAR_EQUAL( sw::MathUtil::cos( 0.6f ), spot._params._y, 0.001f );
    SW_EXPECT_NEAR_EQUAL( sw::MathUtil::cos( 0.2f ), spot._params._z, 0.001f );
    // 안쪽이 바깥쪽보다 좁으므로 코사인은 안쪽이 더 크다 — 셰이더의 감쇠 분모가 양수라는 뜻이다.
    SW_EXPECT_TRUE_MSG( spot._params._z > spot._params._y, "원뿔 코사인이 뒤집혔다 — 감쇠 분모가 음수가 된다" );

    // 방향광의 방향은 정규화되어 실린다(셰이더는 정규화하지 않는다).
    const sw::GpuLight& sun    = listLight[0];
    const float32       length = sw::MathUtil::sqrt( sun._directionType._x * sun._directionType._x +
                                                     sun._directionType._y * sun._directionType._y +
                                                     sun._directionType._z * sun._directionType._z );
    SW_EXPECT_NEAR_EQUAL( 1.0f, length, 0.001f );
}

/**
 * @brief [SceneLightTest] 안쪽 원뿔은 바깥 원뿔을 넘지 못한다 — 어느 쪽을 먼저 넣어도
 */
SW_TEST_CASE( SceneLightTest, SpotConeAnglesStayOrdered )
{
    sw::Scene              scene( "SceneLightCone" );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    sw::SpotLightComponent* pSpot = addLightObject<sw::SpotLightComponent>( pObjects, "Spot" );
    SW_ASSERT_NOT_NULL( pSpot );

    // 1) 안쪽을 크게 넣으면 바깥에서 잘린다.
    pSpot->setOuterConeAngle( 0.5f );
    pSpot->setInnerConeAngle( 1.2f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pSpot->getInnerConeAngle(), 0.001f );

    // 2) 바깥을 좁히면 안쪽이 따라 내려온다.
    pSpot->setInnerConeAngle( 0.4f );
    pSpot->setOuterConeAngle( 0.1f );
    SW_EXPECT_NEAR_EQUAL( 0.1f, pSpot->getOuterConeAngle(), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.1f, pSpot->getInnerConeAngle(), 0.001f );

    // 3) 음수는 0 으로 — 원뿔이 뒤집히면 "빛이 뒤로도 나간다".
    pSpot->setOuterConeAngle( -1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pSpot->getOuterConeAngle(), 0.001f );
    SW_EXPECT_TRUE( pSpot->getInnerConeAngle() <= pSpot->getOuterConeAngle() );
}

/**
 * @brief [SceneLightTest] 주광 조회는 켜진 빛만 돌려주고, 없으면 nullptr 이다
 * @details `EngineLoop` 이 매 프레임 부르는 자리다. 예전에는 씬 전체를 훑었고(큐브 20,000 개에서
 *          게임 스레드의 38%), 지금은 등록부만 본다 — 그 교체가 **답까지 바꾸지는 않았는지** 본다.
 */
SW_TEST_CASE( SceneLightTest, FindActiveDirectionalLightSkipsInactiveOnes )
{
    sw::Scene              scene( "SceneLightFind" );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    SW_EXPECT_NULL( scene.findActiveDirectionalLight() );

    sw::DirectionalLightComponent* pFirst  = addLightObject<sw::DirectionalLightComponent>( pObjects, "SunA" );
    sw::DirectionalLightComponent* pSecond = addLightObject<sw::DirectionalLightComponent>( pObjects, "SunB" );
    SW_ASSERT_NOT_NULL( pFirst );
    SW_ASSERT_NOT_NULL( pSecond );

    SW_EXPECT_TRUE( scene.findActiveDirectionalLight() == pFirst );

    pFirst->setActive( false );
    SW_EXPECT_TRUE( scene.findActiveDirectionalLight() == pSecond );

    sw::GameObject* pSecondOwner = pSecond->getOwner();
    SW_ASSERT_NOT_NULL( pSecondOwner );
    pSecondOwner->setActive( false );
    SW_EXPECT_NULL( scene.findActiveDirectionalLight() );
}
