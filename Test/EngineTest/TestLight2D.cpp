#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Renderer/Light/GpuLightBuffer.h"
#include "Engine/Graphics/Shader/Binding/GpuLight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/2D/Light2DComponent.h"
#include "Engine/Object/Component/2D/ShadowCaster2DComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

#include "TestFramework/TestFramework.h"

// Light2DTest — 2D 빛 · 그림자 가림막: 감쇠 식 · GPU 원소 칸 · 수집 순서(빛 뒤에 가림막) · 상자 가림막의 바깥쪽 방향. 디바이스 없음(nogpu).

/**
 * @brief [Light2DTest] 감쇠는 안 반경 안에서 1, 바깥 반경에서 0, 사이는 ((바깥 − 거리) / (바깥 − 안))^지수다
 * @details 셰이더 `swComputeLight2dAttenuation` 과 같은 식이다 — RenderPassGpuTest.Light2DFalloffAndShadowOnEveryBackend 가 GPU 되읽기를 이 값과 견준다.
 */
SW_TEST_CASE( Light2DTest, AttenuationFollowsInnerOuterAndExponent )
{
    SW_EXPECT_NEAR_EQUAL( 1.0f, sw::PointLight2DComponent::computeAttenuation( 0.5f, 1.0f, 3.0f, 1.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, sw::PointLight2DComponent::computeAttenuation( 2.0f, 1.0f, 3.0f, 1.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, sw::PointLight2DComponent::computeAttenuation( 2.0f, 1.0f, 3.0f, 2.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::PointLight2DComponent::computeAttenuation( 3.0f, 1.0f, 3.0f, 1.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::PointLight2DComponent::computeAttenuation( 9.0f, 1.0f, 3.0f, 1.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, sw::PointLight2DComponent::computeAttenuation( 0.5f, 0.0f, 2.0f, 1.0f ), 1e-6f );
}

/**
 * @brief [Light2DTest] 수집은 2D 빛을 칸의 뜻대로 쓰고(위치 · 노멀 맵 높이 · 반경 · 원뿔 cos · 그림자 · 지수), 가림막 토막은 빛 **뒤에** 그림자 원소로 붙인다
 * @details 상자 가림막 2 × 1 은 토막 넷이고 바깥쪽은 가운데에서 멀어지는 쪽이다. 꺼진 가림막은 빠진다. 전역 빛은 색 · 세기만 싣는다.
 */
SW_TEST_CASE( Light2DTest, CollectWritesLightsThenShadowSegments )
{
    sw::Scene              scene( "Light2DScene" );
    sw::GameObjectManager* pManager = scene.getObjectManager();

    sw::GameObject*              pCasterObject = pManager->createGameObject( sw::hashed_string( "Crate" ) );
    sw::SceneComponent*          pCasterRoot   = pCasterObject->addComponent<sw::SceneComponent>();
    sw::ShadowCaster2DComponent* pCaster       = pCasterObject->addComponent<sw::ShadowCaster2DComponent>();
    SW_ASSERT_NOT_NULL( pCaster );
    pCasterRoot->setLocalPosition( sw::float3{ 3.0f, 1.0f, 0.0f } );
    pCaster->setSize( sw::float2{ 2.0f, 1.0f } );

    sw::GameObject*            pLightObject = pManager->createGameObject( sw::hashed_string( "Torch" ) );
    sw::PointLight2DComponent* pLight       = pLightObject->addComponent<sw::PointLight2DComponent>();
    SW_ASSERT_NOT_NULL( pLight );
    pLight->setLocalPosition( sw::float3{ -1.0f, 2.0f, 0.0f } );
    pLight->setRadius( 0.5f, 5.0f );
    pLight->setConeAngles( 60.0f, 90.0f );
    pLight->setFalloffExponent( 2.0f );
    pLight->setNormalMapHeight( 0.75f );
    sw::GameObject* pAmbientObject = pManager->createGameObject( sw::hashed_string( "Ambient" ) );
    SW_ASSERT_NOT_NULL( pAmbientObject->addComponent<sw::GlobalLight2DComponent>() );
    pManager->flushSceneTransforms();

    sw::vector<sw::GpuLight> listLight;
    sw::collectSceneLights( &scene, listLight );
    SW_ASSERT_EQUAL( 2u + 4u, static_cast<uint32>( listLight.size() ) );
    // 빛(종류 순서: 점 2D → 전역 2D) 다음에 가림막 토막 넷.
    SW_EXPECT_NEAR_EQUAL( static_cast<float32>( sw::shaderslot::kLightTypePoint2D ), listLight[0]._directionType._w, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( static_cast<float32>( sw::shaderslot::kLightTypeGlobal2D ), listLight[1]._directionType._w, 1e-6f );
    for ( size_t index = 2; index < listLight.size(); ++index )
        SW_EXPECT_NEAR_EQUAL( static_cast<float32>( sw::shaderslot::kLightTypeShadow2D ), listLight[index]._directionType._w, 1e-6f );

    const sw::GpuLight& point = listLight[0];
    SW_EXPECT_NEAR_EQUAL( -1.0f, point._positionRadius._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, point._positionRadius._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, point._positionRadius._z, 1e-5f ); // 노멀 맵 높이
    SW_EXPECT_NEAR_EQUAL( 5.0f, point._positionRadius._w, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, point._directionType._x, 1e-5f ); // 로컬 +X
    SW_EXPECT_NEAR_EQUAL( 0.5f, point._directionType._z, 1e-5f ); // 안 반경
    SW_EXPECT_NEAR_EQUAL( 1.0f, point._params._x, 1e-6f );        // 그림자
    SW_EXPECT_NEAR_EQUAL( sw::MathUtil::cos( 45.0f * sw::MathUtil::DegreeToRadian ), point._params._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( sw::MathUtil::cos( 30.0f * sw::MathUtil::DegreeToRadian ), point._params._z, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, point._params._w, 1e-6f );

    // 상자 토막 넷: 바깥쪽은 가운데(3, 1)에서 멀어진다.
    for ( size_t index = 2; index < listLight.size(); ++index )
    {
        const sw::float4& segment = listLight[index]._positionRadius;
        const sw::float2  middle{ ( segment._x + segment._z ) * 0.5f - 3.0f, ( segment._y + segment._w ) * 0.5f - 1.0f };
        const sw::float2  outward{ listLight[index]._colorIntensity._x, listLight[index]._colorIntensity._y };
        SW_EXPECT_TRUE( outward._x * middle._x + outward._y * middle._y > 0.0f );
        SW_EXPECT_NEAR_EQUAL( 1.0f, outward._x * outward._x + outward._y * outward._y, 1e-4f );
    }

    // 꺼진 가림막은 빠진다.
    pCaster->setActive( false );
    sw::collectSceneLights( &scene, listLight );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listLight.size() ) );
}
