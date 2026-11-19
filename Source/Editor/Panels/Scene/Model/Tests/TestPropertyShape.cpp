// 선언형 모양(`Meta = "Shape=…"`) — 메타 읽기(PropertyShapeDecl), 모양 선 · 손잡이 계산(PropertyShapeValue · PropertyShapeHandle), 명령 순서, 편집 요청으로 쓰기.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Inspector/Model/PropertyGridViewModel.h"
#include "Editor/Panels/Scene/Model/PropertyShapeEdit.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Visual/Component/PointLightComponent.h"
#include "Engine/Visual/Component/SpotLightComponent.h"

namespace
{
    using sw::editor::PropertyShapeHandle;
    using sw::editor::PropertyShapeHandleKind;
    using sw::editor::PropertyShapeKind;
    using sw::editor::PropertyShapeValue;

    struct PropertyShapeTestInternal
    {
        static constexpr float32 kTolerance = 1.0e-4f;

        /** @brief 원점 (1, 2, 3) 에 놓인 회전 없는 모양 값입니다. */
        static PropertyShapeValue makeValue( PropertyShapeKind kind )
        {
            PropertyShapeValue value{};
            value._kind          = kind;
            value._frame._origin = sw::float3{ 1.0f, 2.0f, 3.0f };
            return value;
        }

        /** @brief 점 @p from 에서 점 @p to 쪽으로 쏜 레이입니다. */
        static sw::Ray makeRay( const sw::float3& from, const sw::float3& to )
        {
            sw::Ray ray{};
            ray._origin    = from;
            ray._direction = ( to - from ).normalize();
            return ray;
        }

        static const PropertyShapeHandle* findHandle( const sw::vector<PropertyShapeHandle>& listHandle, PropertyShapeHandleKind kind, uint32 axis, float32 sign )
        {
            for ( const PropertyShapeHandle& handle : listHandle )
            {
                if ( handle._kind == kind && handle._axis == axis && handle._sign == sign )
                    return &handle;
            }
            return nullptr;
        }
    };
} // namespace

/**
 * @brief [PropertyShapeTest] 구는 축마다 양 · 음 손잡이 여섯을 반지름 거리에 두고, 손잡이를 끈 자리의 거리가 새 반지름이다(아래는 `kMinExtent`). 결함이면 손잡이가 모양 밖에 뜬다
 */
SW_TEST_CASE( PropertyShapeTest, SphereHandlesSitOnTheRadiusAndDragToTheRayDistance )
{
    using Internal           = PropertyShapeTestInternal;
    PropertyShapeValue value = Internal::makeValue( PropertyShapeKind::Sphere );
    value._number            = 2.0f;
    sw::vector<PropertyShapeHandle> listHandle;
    value.collectHandles( 7, sw::hashed_string( "_radius" ), sw::hashed_string{}, listHandle );
    SW_REQUIRE_EQUAL( size_t{ 6 }, listHandle.size() );
    const PropertyShapeHandle* pPlusX = Internal::findHandle( listHandle, PropertyShapeHandleKind::Radius, 0, 1.0f );
    SW_REQUIRE_NOT_NULL( pPlusX );
    SW_EXPECT_NEAR( 3.0f, pPlusX->_position._x, Internal::kTolerance );
    SW_EXPECT_EQUAL( uint64{ 7 }, pPlusX->_componentID );
    SW_EXPECT_FALSE( pPlusX->isVectorValue() );

    // 손잡이 축(+X) 위 x = 1 + 5 를 위에서 내려다 본다 — 중심에서 5 m.
    const sw::Ray down = Internal::makeRay( sw::float3{ 6.0f, 10.0f, 3.0f }, sw::float3{ 6.0f, 2.0f, 3.0f } );
    SW_EXPECT_NEAR( 5.0f, pPlusX->computeDraggedNumber( down ), Internal::kTolerance );
    // 반대편으로 끌면 가장 작은 길이에서 멈춘다.
    const sw::Ray behind = Internal::makeRay( sw::float3{ -4.0f, 10.0f, 3.0f }, sw::float3{ -4.0f, 2.0f, 3.0f } );
    SW_EXPECT_NEAR( PropertyShapeValue::kMinExtent, pPlusX->computeDraggedNumber( behind ), Internal::kTolerance );
}

/**
 * @brief [PropertyShapeTest] 상자 손잡이는 잡은 축의 반 크기만 바꾸고 나머지 축은 지킨다. 결함이면 한 면을 끌 때 다른 면이 따라 움직인다
 */
SW_TEST_CASE( PropertyShapeTest, BoxHandleChangesOnlyItsAxis )
{
    using Internal           = PropertyShapeTestInternal;
    PropertyShapeValue value = Internal::makeValue( PropertyShapeKind::Box );
    value._vector            = sw::float3{ 1.0f, 2.0f, 3.0f };
    sw::vector<PropertyShapeHandle> listHandle;
    value.collectHandles( 1, sw::hashed_string( "_halfExtents" ), sw::hashed_string{}, listHandle );
    SW_REQUIRE_EQUAL( size_t{ 6 }, listHandle.size() );
    const PropertyShapeHandle* pMinusY = Internal::findHandle( listHandle, PropertyShapeHandleKind::BoxExtent, 1, -1.0f );
    SW_REQUIRE_NOT_NULL( pMinusY );
    SW_EXPECT_NEAR( 0.0f, pMinusY->_position._y, Internal::kTolerance ); // 원점 y 2 에서 -2
    SW_EXPECT_TRUE( pMinusY->isVectorValue() );

    // -Y 축 위 y = 2 - 4 를 옆(+X)에서 본다.
    const sw::Ray    side = Internal::makeRay( sw::float3{ 10.0f, -2.0f, 3.0f }, sw::float3{ 1.0f, -2.0f, 3.0f } );
    const sw::float3 size = pMinusY->computeDraggedVector( side );
    SW_EXPECT_NEAR( 1.0f, size._x, Internal::kTolerance );
    SW_EXPECT_NEAR( 4.0f, size._y, Internal::kTolerance );
    SW_EXPECT_NEAR( 3.0f, size._z, Internal::kTolerance );
}

/**
 * @brief [PropertyShapeTest] 원뿔은 반각 손잡이 넷(테두리 원)과 길이 손잡이 하나(축 끝, 길이 프로퍼티)를 두고, 반각은 축에서 끈 거리의 atan 이다. 결함이면 길이 손잡이가 반각 프로퍼티에 쓴다
 */
SW_TEST_CASE( PropertyShapeTest, ConeHandlesEditAngleAndRange )
{
    using Internal           = PropertyShapeTestInternal;
    PropertyShapeValue value = Internal::makeValue( PropertyShapeKind::Cone );
    value._number            = 0.5f;
    value._coneLength        = 4.0f;
    sw::vector<PropertyShapeHandle> listHandle;
    value.collectHandles( 3, sw::hashed_string( "_outerConeAngle" ), sw::hashed_string( "_radius" ), listHandle );
    SW_REQUIRE_EQUAL( size_t{ 5 }, listHandle.size() );

    const PropertyShapeHandle* pRange = Internal::findHandle( listHandle, PropertyShapeHandleKind::ConeRange, 2, 1.0f );
    SW_REQUIRE_NOT_NULL( pRange );
    SW_EXPECT_TRUE( pRange->_property == sw::hashed_string( "_radius" ) );
    SW_EXPECT_NEAR( 7.0f, pRange->_position._z, Internal::kTolerance );

    const PropertyShapeHandle* pAngle = Internal::findHandle( listHandle, PropertyShapeHandleKind::ConeAngle, 0, 1.0f );
    SW_REQUIRE_NOT_NULL( pAngle );
    SW_EXPECT_TRUE( pAngle->_property == sw::hashed_string( "_outerConeAngle" ) );
    SW_EXPECT_NEAR( 1.0f + 4.0f * sw::MathUtil::tan( 0.5f ), pAngle->_position._x, Internal::kTolerance );
    // 테두리 원 중심(1, 2, 7)에서 +X 로 4 m — 반각 45°.
    const sw::Ray down = Internal::makeRay( sw::float3{ 5.0f, 10.0f, 7.0f }, sw::float3{ 5.0f, 2.0f, 7.0f } );
    SW_EXPECT_NEAR( sw::MathUtil::kPi * 0.25f, pAngle->computeDraggedNumber( down ), Internal::kTolerance );
    // 아주 멀리 끌어도 90° 앞에서 멈춘다.
    const sw::Ray far = Internal::makeRay( sw::float3{ 1000.0f, 10.0f, 7.0f }, sw::float3{ 1000.0f, 2.0f, 7.0f } );
    SW_EXPECT_NEAR( PropertyShapeValue::kMaxConeAngle, pAngle->computeDraggedNumber( far ), Internal::kTolerance );
}

/**
 * @brief [PropertyShapeTest] 화살표 끝을 끌면 끈 자리 쪽으로 로컬 방향을 돌리고 잡은 길이를 지킨다. 결함이면 방향 값의 길이(발사 속도의 배율)가 끌기마다 바뀐다
 */
SW_TEST_CASE( PropertyShapeTest, ArrowDragTurnsTheDirectionAndKeepsItsLength )
{
    using Internal           = PropertyShapeTestInternal;
    PropertyShapeValue value = Internal::makeValue( PropertyShapeKind::Arrow );
    value._vector            = sw::float3{ 0.0f, 0.0f, 2.0f };
    sw::vector<PropertyShapeHandle> listHandle;
    value.collectHandles( 5, sw::hashed_string( "_localDirection" ), sw::hashed_string{}, listHandle );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listHandle.size() );
    const PropertyShapeHandle& tip = listHandle[0];
    SW_EXPECT_NEAR( 3.0f + PropertyShapeValue::kArrowLength, tip._position._z, Internal::kTolerance );

    // 끝(1, 2, 4)을 지나 레이에 수직인 평면에서 원점의 +X 쪽 (2, 2, 3) 을 가리킨다 — 레이는 위에서 아래로.
    const sw::Ray    down      = Internal::makeRay( sw::float3{ 2.0f, 10.0f, 3.0f }, sw::float3{ 2.0f, 2.0f, 3.0f } );
    const sw::float3 direction = tip.computeDraggedVector( down );
    SW_EXPECT_NEAR( 2.0f, direction._x, Internal::kTolerance );
    SW_EXPECT_NEAR( 0.0f, direction._z, Internal::kTolerance );
}

/**
 * @brief [PropertyShapeTest] 모양마다 긋는 선 수 — 구는 원 셋, 상자는 모서리 12, 원뿔은 테두리 원과 옆선 넷, 화살표는 대와 날개 넷. 결함이면 상자 모서리가 겹치거나 빠진다
 */
SW_TEST_CASE( PropertyShapeTest, LineCountsPerShape )
{
    using Internal = PropertyShapeTestInternal;
    const sw::editor::EditorVisualizerLine prototype{};
    const auto                             countLines = [&]( PropertyShapeKind kind )
    {
        PropertyShapeValue value = Internal::makeValue( kind );
        value._number            = 0.4f;
        value._coneLength        = 2.0f;
        value._vector            = sw::float3{ 1.0f, 1.0f, 1.0f };
        sw::editor::EditorVisualizerDrawList drawList;
        value.addLines( prototype, drawList );
        return drawList._listLine.size();
    };
    const size_t circle = PropertyShapeValue::kCircleSegmentCount;
    SW_EXPECT_EQUAL( circle * 3, countLines( PropertyShapeKind::Sphere ) );
    SW_EXPECT_EQUAL( size_t{ 12 }, countLines( PropertyShapeKind::Box ) );
    SW_EXPECT_EQUAL( circle + 4, countLines( PropertyShapeKind::Cone ) );
    SW_EXPECT_EQUAL( size_t{ 5 }, countLines( PropertyShapeKind::Arrow ) );
}

/**
 * @brief [PropertyShapeTest] 빛 컴포넌트의 메타 선언 — 점광은 반경 구 하나, 스폿은 안 · 밖 반각 원뿔 둘이 길이로 반경을 가리킨다. 결함이면 선언이 그려지지 않거나 길이 프로퍼티가 빠진다
 */
SW_TEST_CASE( PropertyShapeTest, LightComponentsDeclareTheirShapes )
{
    const sw::TypeInfo* pPointType = sw::findStaticType<sw::PointLightComponent>();
    const sw::TypeInfo* pSpotType  = sw::findStaticType<sw::SpotLightComponent>();
    SW_REQUIRE_NOT_NULL( pPointType );
    SW_REQUIRE_NOT_NULL( pSpotType );

    sw::vector<sw::editor::PropertyShapeDecl> listDecl;
    SW_REQUIRE_TRUE( sw::editor::PropertyShapeDecl::collect( *pPointType, listDecl ).has_value() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listDecl.size() );
    SW_EXPECT_TRUE( listDecl[0]._kind == PropertyShapeKind::Sphere );
    SW_EXPECT_TRUE( listDecl[0]._pProperty->_name == sw::hashed_string( "_radius" ) );
    SW_EXPECT_NULL( listDecl[0]._pRangeProperty );

    listDecl.clear();
    SW_REQUIRE_TRUE( sw::editor::PropertyShapeDecl::collect( *pSpotType, listDecl ).has_value() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listDecl.size() );
    for ( const sw::editor::PropertyShapeDecl& decl : listDecl )
    {
        SW_EXPECT_TRUE( decl._kind == PropertyShapeKind::Cone );
        SW_REQUIRE_NOT_NULL( decl._pRangeProperty );
        SW_EXPECT_TRUE( decl._pRangeProperty->_name == sw::hashed_string( "_radius" ) );
    }
}

/**
 * @brief [PropertyShapeTest] 등록된 모든 타입의 `Shape=` 선언이 바르다(모양 이름 · 값 형식 · 원뿔 길이 프로퍼티). 결함이면 그 타입의 모양이 에디터에서 말없이 사라지고 오류 로그만 남는다
 */
SW_TEST_CASE( PropertyShapeTest, EveryRegisteredDeclarationIsWellFormed )
{
    const sw::TypeRegistry&                   registry = sw::editor::PropertyGridViewModel::getTypeRegistry();
    sw::vector<sw::editor::PropertyShapeDecl> listDecl;
    sw::string                                failures; // 틀린 타입 이름 — 비교가 실패하면 글로 보인다
    registry.forEachType( [&]( const sw::TypeInfo& type )
    {
        listDecl.clear();
        if ( sw::editor::PropertyShapeDecl::collect( type, listDecl ).has_value() == false )
        {
            failures += type._fullyQualifiedName.c_str();
            failures += ' ';
        }
    } );
    SW_EXPECT_EQUAL( sw::string{}, failures );
}

/**
 * @brief [PropertyShapeTest] 손잡이 끌기는 편집 요청으로 쓴다 — 점광 반경이 끈 거리로 바뀌고, 같은 값이면 쓰지 않는다. 결함이면 끌기가 되돌리기 없이 남거나 값이 그대로다
 */
SW_TEST_CASE( PropertyShapeTest, ApplyDragWritesThroughThePropertyEditRequest )
{
    using Internal = PropertyShapeTestInternal;
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Lamp" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    sw::PointLightComponent* pLight = pObject->addComponent<sw::PointLightComponent>();
    SW_REQUIRE_NOT_NULL( pLight );
    pLight->setRadius( 2.0f );

    PropertyShapeValue value = Internal::makeValue( PropertyShapeKind::Sphere );
    value._frame._origin     = sw::float3{};
    value._number            = pLight->getRadius();
    sw::vector<PropertyShapeHandle> listHandle;
    value.collectHandles( pLight->getComponentID(), sw::hashed_string( "_radius" ), sw::hashed_string{}, listHandle );
    const PropertyShapeHandle* pPlusX = Internal::findHandle( listHandle, PropertyShapeHandleKind::Radius, 0, 1.0f );
    SW_REQUIRE_NOT_NULL( pPlusX );

    const sw::TypeRegistry& registry = sw::editor::PropertyGridViewModel::getTypeRegistry();
    const sw::Ray           down     = Internal::makeRay( sw::float3{ 6.0f, 10.0f, 0.0f }, sw::float3{ 6.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( sw::editor::PropertyShapeHandleEdit::applyDrag( *pLight, *pPlusX, down, registry ) );
    SW_EXPECT_NEAR( 6.0f, pLight->getRadius(), Internal::kTolerance );
    SW_EXPECT_FALSE( sw::editor::PropertyShapeHandleEdit::applyDrag( *pLight, *pPlusX, down, registry ) );
}

/**
 * @brief [PropertyShapeTest] 손잡이 명령 순서 — 누름은 잡기, 끄는 동안 레이마다 끌기, 놓으면 커밋하는 놓기, 대상이 사라지면 커밋 없는 놓기. 결함이면 끊긴 끌기가 되돌리기를 남긴다
 */
SW_TEST_CASE( PropertyShapeTest, DragTrackerSendsCaptureDragEnd )
{
    using sw::editor::PropertyShapeHandleCommand;
    using sw::editor::PropertyShapeHandleCommandKind;
    sw::editor::PropertyShapeHandleDragTracker tracker;
    sw::vector<PropertyShapeHandleCommand>     listCommand;
    PropertyShapeHandle                        handle{};
    handle._componentID = 9;
    tracker.press( sw::GameObjectHandle::make( 4 ), handle, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( listCommand[0]._kind == PropertyShapeHandleCommandKind::Capture );
    SW_EXPECT_TRUE( tracker.isDragging() );

    listCommand.clear();
    const sw::Ray ray{};
    SW_EXPECT_TRUE( tracker.step( true, true, &ray, listCommand ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( listCommand[0]._kind == PropertyShapeHandleCommandKind::Drag );
    SW_EXPECT_EQUAL( uint64{ 9 }, listCommand[0]._handle._componentID );

    listCommand.clear();
    SW_EXPECT_FALSE( tracker.step( true, false, &ray, listCommand ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_TRUE( listCommand[0]._kind == PropertyShapeHandleCommandKind::End );
    SW_EXPECT_TRUE( listCommand[0]._bCommit );

    listCommand.clear();
    tracker.press( sw::GameObjectHandle::make( 4 ), handle, listCommand );
    listCommand.clear();
    SW_EXPECT_FALSE( tracker.step( false, true, nullptr, listCommand ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCommand.size() );
    SW_EXPECT_FALSE( listCommand[0]._bCommit );
}
