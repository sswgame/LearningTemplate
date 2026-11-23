// InspectorNameSourceTest — 이름 칸의 고르기 목록 출처(`Meta = "NameSource=…"`): 충돌 프로필 · 채널은 물리 설정의 이름을 표 차례로 내고, 모르는 출처는 오류 한 번이다.
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorNameSource.h"

#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/TypeInfo.h"

using namespace sw;
using namespace sw::editor;

/** @brief [InspectorNameSourceTest] PhysicsMaterial 은 물리 재질 이름, CollisionProfile 은 배포 설정의 프로필(첫째 Default), CollisionChannel 은 채널(트레이스 채널 Camera 포함)을 내고, 강체의 프로필 칸은 그 출처를 메타로 든다 */
SW_TEST_CASE( InspectorNameSourceTest, CollisionNamesComeFromPhysicsSettings )
{
    vector<hashed_string> listName;
    SW_REQUIRE_TRUE( InspectorNameSource::collectNames( hashed_string( "CollisionProfile" ), listName ) );
    SW_REQUIRE_TRUE( listName.empty() == false );
    SW_EXPECT_TRUE( listName.front() == hashed_string( "Default" ) );
    SW_REQUIRE_TRUE( InspectorNameSource::collectNames( hashed_string( "CollisionChannel" ), listName ) );
    bool bCamera = false;
    for ( const hashed_string& name : listName )
    {
        bCamera = bCamera || name == hashed_string( "Camera" );
    }
    SW_EXPECT_TRUE( bCamera );

    const PropertyInfo* pLayer = RigidBodyComponent::StaticType()->findProperty( hashed_string( "_layer" ) );
    SW_REQUIRE_NOT_NULL( pLayer );
    const string* pSource = pLayer->findCustomMeta( hashed_string( "NameSource" ) );
    SW_REQUIRE_NOT_NULL( pSource );
    SW_EXPECT_EQUAL( string( "CollisionProfile" ), *pSource );

    SW_REQUIRE_TRUE( InspectorNameSource::collectNames( hashed_string( "PhysicsMaterial" ), listName ) );
    SW_EXPECT_TRUE( VectorUtil::contains( listName, hashed_string( "Stone" ) ) );

    SW_EXPECT_LOG( "Inspector.UnknownNameSource" );
    SW_EXPECT_FALSE( InspectorNameSource::collectNames( hashed_string( "Nowhere" ), listName ) );
}
