// 선언형 모양(`Meta = "Shape=…"`) — 메타 읽기(PropertyShapeDecl), 모양 선 · 손잡이 계산(PropertyShapeValue · PropertyShapeHandle), 편집 요청으로 쓰기.
// 명령 순서(공통 `EditorHandleDragTracker`)는 TestEditorColliderHandleDragTracker 가 본다.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Inspector/Model/PropertyGridViewModel.h"
#include "Editor/Panels/Scene/Model/PropertyShapeEdit.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "Engine/Audio/Component/AudioReverbZoneComponent.h"
#include "Engine/Navigation/Component/NavMeshObstacleComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
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
    value._range             = 4.0f;
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
        value._range             = 2.0f;
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
    SW_EXPECT_EQUAL( circle, countLines( PropertyShapeKind::Circle ) );
    SW_EXPECT_EQUAL( circle * 2 + 4 + circle * 2, countLines( PropertyShapeKind::Capsule ) ); // 끝 원 둘 · 옆선 넷 · 반원 넷(반원 하나가 원의 절반)
    SW_EXPECT_EQUAL( circle * 2 + 4, countLines( PropertyShapeKind::Cylinder ) );             // 위 · 아래 원 둘 · 옆선 넷
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
 * @brief [PropertyShapeTest] 원은 로컬 XY 평면의 손잡이 넷이다(Z 쪽은 없다). 결함이면 2D 빛 반경에 화면 밖(깊이) 손잡이가 선다
 */
SW_TEST_CASE( PropertyShapeTest, CircleHandlesStayInTheLocalPlane )
{
    using Internal           = PropertyShapeTestInternal;
    PropertyShapeValue value = Internal::makeValue( PropertyShapeKind::Circle );
    value._number            = 3.0f;
    sw::vector<PropertyShapeHandle> listHandle;
    value.collectHandles( 2, sw::hashed_string( "_outerRadius" ), sw::hashed_string{}, listHandle );
    SW_REQUIRE_EQUAL( size_t{ 4 }, listHandle.size() );
    for ( const PropertyShapeHandle& handle : listHandle )
    {
        SW_EXPECT_TRUE( handle._axis < 2 );
        SW_EXPECT_NEAR( 3.0f, handle._position._z, Internal::kTolerance );
    }
}

/**
 * @brief [PropertyShapeTest] 바닥 원점 캡슐: 반지름 손잡이 넷은 가운데(바닥 + 반 높이 + 반지름) 둘레, 높이 손잡이 하나는 꼭대기이고 끈 거리에서 반지름 둘을 빼 반으로 나눈 값이 새 반 높이다.
 *        결함이면 캐릭터 캡슐의 손잡이가 발밑에 뜨거나 높이가 두 배로 바뀐다
 */
SW_TEST_CASE( PropertyShapeTest, BaseCapsuleHandlesAndHeightDrag )
{
    using Internal           = PropertyShapeTestInternal;
    PropertyShapeValue value = Internal::makeValue( PropertyShapeKind::Capsule );
    value._number            = 0.5f;
    value._range             = 1.0f;
    value._bBaseOrigin       = 1;
    sw::vector<PropertyShapeHandle> listHandle;
    value.collectHandles( 6, sw::hashed_string( "_radius" ), sw::hashed_string( "_halfHeight" ), listHandle );
    SW_REQUIRE_EQUAL( size_t{ 5 }, listHandle.size() );
    const PropertyShapeHandle* pPlusX = Internal::findHandle( listHandle, PropertyShapeHandleKind::Radius, 0, 1.0f );
    SW_REQUIRE_NOT_NULL( pPlusX );
    SW_EXPECT_NEAR( 3.5f, pPlusX->_position._y, Internal::kTolerance ); // 바닥 y 2 + 1 + 0.5
    SW_EXPECT_NEAR( 1.5f, pPlusX->_position._x, Internal::kTolerance );

    const PropertyShapeHandle* pTop = Internal::findHandle( listHandle, PropertyShapeHandleKind::CapsuleHeight, 1, 1.0f );
    SW_REQUIRE_NOT_NULL( pTop );
    SW_EXPECT_TRUE( pTop->_property == sw::hashed_string( "_halfHeight" ) );
    SW_EXPECT_NEAR( 5.0f, pTop->_position._y, Internal::kTolerance ); // 바닥 y 2 + 2 × 1.5
    SW_EXPECT_NULL( Internal::findHandle( listHandle, PropertyShapeHandleKind::CapsuleHeight, 1, -1.0f ) );
    // 바닥에서 위로 4 m 를 옆(+Z)에서 본다 — (4 - 2 × 0.5) ÷ 2 = 1.5.
    const sw::Ray side = Internal::makeRay( sw::float3{ 1.0f, 6.0f, 10.0f }, sw::float3{ 1.0f, 6.0f, 3.0f } );
    SW_EXPECT_NEAR( 1.5f, pTop->computeDraggedNumber( side ), Internal::kTolerance );
    // 반지름 둘보다 낮게 끌면 원기둥이 없는 구다 — 반 높이 0.
    const sw::Ray low = Internal::makeRay( sw::float3{ 1.0f, 2.5f, 10.0f }, sw::float3{ 1.0f, 2.5f, 3.0f } );
    SW_EXPECT_NEAR( 0.0f, pTop->computeDraggedNumber( low ), Internal::kTolerance );
}

/**
 * @brief [PropertyShapeTest] 선언 선택지 — 캐릭터 캡슐은 바닥 원점(`ShapeOrigin=Base`), 오디오 존은 월드 축(`ShapeAxes=World`)이다. 결함이면 캡슐이 발밑을 가운데로 그리거나 존이 돌아간다
 */
SW_TEST_CASE( PropertyShapeTest, DeclarationOptionsAreRead )
{
    const sw::TypeInfo* pCharacterType = sw::findStaticType<sw::CharacterControllerComponent>();
    const sw::TypeInfo* pReverbType    = sw::findStaticType<sw::AudioReverbZoneComponent>();
    SW_REQUIRE_NOT_NULL( pCharacterType );
    SW_REQUIRE_NOT_NULL( pReverbType );

    sw::vector<sw::editor::PropertyShapeDecl> listDecl;
    SW_REQUIRE_TRUE( sw::editor::PropertyShapeDecl::collect( *pCharacterType, listDecl ).has_value() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listDecl.size() );
    SW_EXPECT_TRUE( listDecl[0]._kind == PropertyShapeKind::Capsule );
    SW_EXPECT_EQUAL( uint8{ 1 }, listDecl[0]._bBaseOrigin );
    SW_REQUIRE_NOT_NULL( listDecl[0]._pRangeProperty );
    SW_EXPECT_TRUE( listDecl[0]._pRangeProperty->_name == sw::hashed_string( "_halfHeight" ) );

    listDecl.clear();
    SW_REQUIRE_TRUE( sw::editor::PropertyShapeDecl::collect( *pReverbType, listDecl ).has_value() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listDecl.size() ); // 상자 · 구 — 어느 쪽을 그릴지는 EditCondition(_shape)이 고른다
    for ( const sw::editor::PropertyShapeDecl& decl : listDecl )
    {
        SW_EXPECT_TRUE( decl._axes == sw::editor::PropertyShapeAxes::World );
    }
}

/**
 * @brief [PropertyShapeTest] 배율 2 의 구: 손잡이는 월드 반지름(저장 값 × 2)에 서고, 끈 월드 거리를 2 로 나눠 저장 값으로 되쓴다. 결함이면 배율이 있는 오브젝트에서 모양 · 손잡이가 물리와 어긋난다
 */
SW_TEST_CASE( PropertyShapeTest, ScaledSphereHandlesSitOnTheWorldRadiusAndWriteTheStoredValue )
{
    using Internal           = PropertyShapeTestInternal;
    PropertyShapeValue value = Internal::makeValue( PropertyShapeKind::Sphere );
    value._number            = 1.5f;
    value._numberScale       = 2.0f;
    sw::vector<PropertyShapeHandle> listHandle;
    value.collectHandles( 1, sw::hashed_string( "_radius" ), sw::hashed_string{}, listHandle );
    const PropertyShapeHandle* pPlusX = Internal::findHandle( listHandle, PropertyShapeHandleKind::Radius, 0, 1.0f );
    SW_REQUIRE_NOT_NULL( pPlusX );
    SW_EXPECT_NEAR( 4.0f, pPlusX->_position._x, Internal::kTolerance ); // 원점 x 1 + 1.5 × 2
    // 중심에서 월드 6 m — 저장 값 3.
    const sw::Ray down = Internal::makeRay( sw::float3{ 7.0f, 10.0f, 3.0f }, sw::float3{ 7.0f, 2.0f, 3.0f } );
    SW_EXPECT_NEAR( 3.0f, pPlusX->computeDraggedNumber( down ), Internal::kTolerance );

    // 배율 0 이면 끈 거리를 저장 값으로 되돌릴 수 없다 — 손잡이가 없다.
    value._numberScale = 0.0f;
    listHandle.clear();
    value.collectHandles( 1, sw::hashed_string( "_radius" ), sw::hashed_string{}, listHandle );
    SW_EXPECT_TRUE( listHandle.empty() );
}

/**
 * @brief [PropertyShapeTest] 배율 2 의 바닥 캡슐: 가운데 · 꼭대기 · 둘레가 월드 크기로 서고, 반지름 · 반 높이 끌기는 각자의 배율로 나눈다. 결함이면 키운 캐릭터의 캡슐이 발밑에 작게 그려진다
 */
SW_TEST_CASE( PropertyShapeTest, ScaledBaseCapsuleHandlesAndDrags )
{
    using Internal           = PropertyShapeTestInternal;
    PropertyShapeValue value = Internal::makeValue( PropertyShapeKind::Capsule );
    value._number            = 0.5f;
    value._range             = 1.0f;
    value._numberScale       = 2.0f;
    value._rangeScale        = 2.0f;
    value._bBaseOrigin       = 1;
    sw::vector<PropertyShapeHandle> listHandle;
    value.collectHandles( 6, sw::hashed_string( "_radius" ), sw::hashed_string( "_halfHeight" ), listHandle );
    SW_REQUIRE_EQUAL( size_t{ 5 }, listHandle.size() );

    const PropertyShapeHandle* pPlusX = Internal::findHandle( listHandle, PropertyShapeHandleKind::Radius, 0, 1.0f );
    SW_REQUIRE_NOT_NULL( pPlusX );
    SW_EXPECT_NEAR( 5.0f, pPlusX->_position._y, Internal::kTolerance ); // 바닥 y 2 + 월드 (2 + 1)
    SW_EXPECT_NEAR( 2.0f, pPlusX->_position._x, Internal::kTolerance ); // 원점 x 1 + 월드 반지름 1
    // 가운데에서 +X 로 월드 3 m — 저장 반지름 1.5.
    const sw::Ray radiusRay = Internal::makeRay( sw::float3{ 4.0f, 5.0f, 10.0f }, sw::float3{ 4.0f, 5.0f, 3.0f } );
    SW_EXPECT_NEAR( 1.5f, pPlusX->computeDraggedNumber( radiusRay ), Internal::kTolerance );

    const PropertyShapeHandle* pTop = Internal::findHandle( listHandle, PropertyShapeHandleKind::CapsuleHeight, 1, 1.0f );
    SW_REQUIRE_NOT_NULL( pTop );
    SW_EXPECT_NEAR( 8.0f, pTop->_position._y, Internal::kTolerance ); // 바닥 y 2 + 2 × 월드 3
    // 바닥에서 위로 월드 8 m — 월드 반 높이 (8 - 2 × 1) ÷ 2 = 3, 저장 값 1.5.
    const sw::Ray heightRay = Internal::makeRay( sw::float3{ 1.0f, 10.0f, 10.0f }, sw::float3{ 1.0f, 10.0f, 3.0f } );
    SW_EXPECT_NEAR( 1.5f, pTop->computeDraggedNumber( heightRay ), Internal::kTolerance );
}

/**
 * @brief [PropertyShapeTest] 캐릭터 캡슐의 배율 규칙은 물리(`makeScaledShape`)와 같다 — 반지름은 max(X, Z), 반 높이는 Y. 결함이면 늘인 캐릭터의 그린 캡슐과 물리 캡슐이 다르다
 */
SW_TEST_CASE( PropertyShapeTest, CharacterCapsuleUsesThePhysicsScaleRule )
{
    const sw::TypeInfo* pType = sw::findStaticType<sw::CharacterControllerComponent>();
    SW_REQUIRE_NOT_NULL( pType );
    sw::vector<sw::editor::PropertyShapeDecl> listDecl;
    SW_REQUIRE_TRUE( sw::editor::PropertyShapeDecl::collect( *pType, listDecl ).has_value() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listDecl.size() );
    SW_EXPECT_TRUE( listDecl[0]._scaleRule == sw::editor::PropertyShapeScaleRule::Physics );

    const sw::CharacterControllerComponent component;
    sw::editor::PropertyShapeFrame         frame{};
    frame._scale                   = sw::float3{ 2.0f, 3.0f, 4.0f };
    const PropertyShapeValue value = listDecl[0].makeValue( &component, frame );
    SW_EXPECT_NEAR( 4.0f, value._numberScale, PropertyShapeTestInternal::kTolerance );
    SW_EXPECT_NEAR( 3.0f, value._rangeScale, PropertyShapeTestInternal::kTolerance );
}

/**
 * @brief [PropertyShapeTest] 원기둥은 옆(±X · ±Z) 반지름 손잡이 넷과 위아래(±Y) 반 높이 손잡이 둘을 두고, 옆에서 끌면 float3 의 x 만, 위에서 끌면 y 만 바뀐다. 결함이면 Z 쪽 손잡이가 z 를 쓴다
 */
SW_TEST_CASE( PropertyShapeTest, CylinderHandlesWriteRadiusToXAndHalfHeightToY )
{
    using Internal           = PropertyShapeTestInternal;
    PropertyShapeValue value = Internal::makeValue( PropertyShapeKind::Cylinder );
    value._vector            = sw::float3{ 0.5f, 1.5f, 9.0f };
    sw::vector<PropertyShapeHandle> listHandle;
    value.collectHandles( 7, sw::hashed_string( "_halfExtents" ), sw::hashed_string{}, listHandle );
    SW_REQUIRE_EQUAL( size_t{ 6 }, listHandle.size() );

    const PropertyShapeHandle* pPlusZ = Internal::findHandle( listHandle, PropertyShapeHandleKind::CylinderRadius, 2, 1.0f );
    SW_REQUIRE_NOT_NULL( pPlusZ );
    SW_EXPECT_NEAR( 3.5f, pPlusZ->_position._z, Internal::kTolerance ); // 원점 z 3 + 반지름 0.5
    // 옆(+X)에서 축 +Z 를 따라 z 5 자리를 본다 — 반지름 2, 반 높이 · z 는 그대로.
    const sw::float3 radiusDragged = pPlusZ->computeDraggedVector( Internal::makeRay( sw::float3{ 10.0f, 2.0f, 5.0f }, sw::float3{ 1.0f, 2.0f, 5.0f } ) );
    SW_EXPECT_NEAR( 2.0f, radiusDragged._x, Internal::kTolerance );
    SW_EXPECT_NEAR( 1.5f, radiusDragged._y, Internal::kTolerance );
    SW_EXPECT_NEAR( 9.0f, radiusDragged._z, Internal::kTolerance );

    const PropertyShapeHandle* pTop = Internal::findHandle( listHandle, PropertyShapeHandleKind::BoxExtent, 1, 1.0f );
    SW_REQUIRE_NOT_NULL( pTop );
    SW_EXPECT_NEAR( 3.5f, pTop->_position._y, Internal::kTolerance ); // 원점 y 2 + 반 높이 1.5
    const sw::float3 heightDragged = pTop->computeDraggedVector( Internal::makeRay( sw::float3{ 1.0f, 5.0f, 10.0f }, sw::float3{ 1.0f, 5.0f, 3.0f } ) );
    SW_EXPECT_NEAR( 0.5f, heightDragged._x, Internal::kTolerance );
    SW_EXPECT_NEAR( 3.0f, heightDragged._y, Internal::kTolerance );
}

/**
 * @brief [PropertyShapeTest] 내비 장애물 — 반 크기 하나를 `_shape` 열거가 상자 · 원기둥으로 고르고, 원점은 `_center` 를 월드 행렬로 옮긴 자리, 틀은 요만 돈다.
 *        결함이면 원기둥 장애물이 상자로 그려지거나, 오프셋 없이 발밑에 그려지거나, 기운 오브젝트에서 Detour 와 다른 축으로 그려진다
 */
SW_TEST_CASE( PropertyShapeTest, NavObstacleShapeFollowsSelectorCenterAndYaw )
{
    using Internal            = PropertyShapeTestInternal;
    const sw::TypeInfo* pType = sw::findStaticType<sw::NavMeshObstacleComponent>();
    SW_REQUIRE_NOT_NULL( pType );
    sw::vector<sw::editor::PropertyShapeDecl> listDecl;
    SW_REQUIRE_TRUE( sw::editor::PropertyShapeDecl::collect( *pType, listDecl ).has_value() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listDecl.size() );
    const sw::editor::PropertyShapeDecl& decl = listDecl[0];
    SW_EXPECT_TRUE( decl._axes == sw::editor::PropertyShapeAxes::Yaw );
    SW_REQUIRE_NOT_NULL( decl._pSelectorProperty );
    SW_REQUIRE_NOT_NULL( decl._pCenterProperty );

    sw::NavMeshObstacleComponent component;
    component.setHalfExtents( sw::float3{ 1.0f, 2.0f, 3.0f } );
    component.setCenter( sw::float3{ 0.0f, 1.0f, 0.0f } );
    // 요 90°(+X → -Z) 에 앞으로 기운(롤) 틀 — 축은 회전 행렬의 행이다. 배율 2.
    sw::editor::PropertyShapeFrame frame{};
    frame._origin = sw::float3{ 10.0f, 0.0f, 0.0f };
    frame._axisX  = sw::float3{ 0.0f, 0.0f, -1.0f };
    frame._axisY  = sw::float3{ 0.6f, 0.8f, 0.0f };
    frame._axisZ  = sw::float3{ 0.8f, -0.6f, 0.0f };
    frame._scale  = sw::float3{ 2.0f, 2.0f, 2.0f };

    component.setShape( sw::NavObstacleShape::Box );
    const PropertyShapeValue box = decl.makeValue( &component, frame );
    SW_EXPECT_TRUE( box._kind == PropertyShapeKind::Box );
    // 원점: 원래 축 Y(기운) × 오프셋 1 × 배율 2 — `float3::transform( _center, world )` 와 같은 점.
    SW_EXPECT_NEAR( 11.2f, box._frame._origin._x, Internal::kTolerance );
    SW_EXPECT_NEAR( 1.6f, box._frame._origin._y, Internal::kTolerance );
    // 요 틀: X 는 수평으로 눕힌 원래 X, Y 는 월드 위.
    SW_EXPECT_NEAR( -1.0f, box._frame._axisX._z, Internal::kTolerance );
    SW_EXPECT_NEAR( 1.0f, box._frame._axisY._y, Internal::kTolerance );
    SW_EXPECT_NEAR( 1.0f, box._frame._axisZ._x, Internal::kTolerance );
    SW_EXPECT_NEAR( 2.0f, box._vectorScale._z, Internal::kTolerance );

    component.setShape( sw::NavObstacleShape::Cylinder );
    const PropertyShapeValue cylinder = decl.makeValue( &component, frame );
    SW_EXPECT_TRUE( cylinder._kind == PropertyShapeKind::Cylinder );
    SW_EXPECT_NEAR( 2.0f, cylinder.getWorldVector()._x, Internal::kTolerance ); // 반지름 1 × max(X, Z) 배율 2
    SW_EXPECT_NEAR( 4.0f, cylinder.getWorldVector()._y, Internal::kTolerance ); // 반 높이 2 × Y 배율 2
}
