#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/Component/AudioAmbientEmitterComponent.h"
#include "Engine/Audio/Component/AudioReverbZoneComponent.h"
#include "Engine/Audio/Scene/SceneAudio.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

using namespace sw;

/** @brief [AudioVolumesTest] 상자 앰비언트는 안에 선 리스너의 자리에서, 밖의 리스너에게는 가장자리에서, 구는 표면에서, 점은 제 자리에서 소리가 난다 */
SW_TEST_CASE( AudioVolumesTest, AmbientEmitterSoundsFromTheClosestPoint )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Ambience" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    AudioAmbientEmitterComponent* pAmbient = pObject->addComponent<AudioAmbientEmitterComponent>();
    SW_REQUIRE_NOT_NULL( pAmbient );
    pAmbient->setLocalPosition( float3{ 10.0f, 0.0f, 0.0f } );
    pAmbient->setShape( AudioVolumeShape::Box, float3{ 2.0f, 2.0f, 2.0f }, 0.0f );

    const float3 inside = pAmbient->computeAudioPosition( float3{ 11.0f, 1.0f, -1.0f } );
    SW_EXPECT_NEAR( 11.0f, inside._x, 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, inside._y, 1.0e-5f );
    SW_EXPECT_NEAR( -1.0f, inside._z, 1.0e-5f );

    const float3 outside = pAmbient->computeAudioPosition( float3{ 0.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR( 8.0f, outside._x, 1.0e-5f );
    SW_EXPECT_NEAR( 0.0f, outside._y, 1.0e-5f );

    pAmbient->setShape( AudioVolumeShape::Sphere, float3{}, 3.0f );
    const float3 surface = pAmbient->computeAudioPosition( float3{ 0.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR( 7.0f, surface._x, 1.0e-5f );

    pAmbient->setShape( AudioVolumeShape::Point, float3{}, 0.0f );
    const float3 point = pAmbient->computeAudioPosition( float3{ 0.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR( 10.0f, point._x, 1.0e-5f );
}

/** @brief [AudioVolumesTest] 리버브 존 세기는 밖 0, 경계에서 안쪽으로 블렌드 거리만큼 0 → 1, 그 안은 1 이다. 씬 오디오는 갱신 전에 세기가 없다 */
SW_TEST_CASE( AudioVolumesTest, ReverbZoneBlendsInsideItsBoundary )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Zone" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    AudioReverbZoneComponent* pZone = pObject->addComponent<AudioReverbZoneComponent>();
    SW_REQUIRE_NOT_NULL( pZone );
    pZone->setSnapshot( hashed_string( "Cave" ) );
    pZone->setShape( AudioVolumeShape::Box, float3{ 5.0f, 5.0f, 5.0f }, 0.0f, 2.0f );

    SW_EXPECT_NEAR( 0.0f, pZone->computeIntensity( float3{ 6.0f, 0.0f, 0.0f } ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.5f, pZone->computeIntensity( float3{ 4.0f, 0.0f, 0.0f } ), 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, pZone->computeIntensity( float3{ 0.0f, 0.0f, 0.0f } ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.0f, manager.getSubsystem<SceneAudio>().findZoneIntensity( hashed_string( "Cave" ) ), 0.0f );
}
