#include "pch.h"

// 인스펙터 배치는 에디터 메타데이터를 읽는다 — Shipping 빌드에는 그 메타데이터가 없어 이 시험도 없다.

#if !defined( SW_SHIPPING )

    #include "Editor/Common/Widgets/EditorListFilter.h"
    #include "Editor/Panels/Inspector/InspectorPropertyLayout.h"

    #include "Engine/Object/Component/2D/SpriteComponent.h"
    #include "Engine/Object/Component/3D/MeshComponent.h"
    #include "Engine/Object/Component/CameraComponent.h"
    #include "Engine/Object/Component/SceneComponent.h"
    #include "Engine/Reflection/ReflectionTypes.h"

    #include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 묶음 전체에서 이 이름의 프로퍼티가 몇 번 나오는지 셉니다. */
    uint32 countProperty( const vector<InspectorPropertyGroup>& listGroup, const utf8* pName )
    {
        const hashed_string wanted( pName );
        uint32              count = 0;
        for ( const InspectorPropertyGroup& group : listGroup )
        {
            for ( const PropertyInfo* pProp : group._listProperty )
            {
                if ( pProp->_name == wanted )
                    ++count;
            }
        }
        return count;
    }

    /** @brief 카테고리 묶음의 자리입니다. 없으면 묶음 수입니다. */
    size_t findGroupIndex( const vector<InspectorPropertyGroup>& listGroup, const utf8* pCategory )
    {
        for ( size_t index = 0; index < listGroup.size(); ++index )
        {
            if ( listGroup[index]._category == pCategory )
                return index;
        }
        return listGroup.size();
    }
} // namespace

/**
 * @brief [InspectorPropertyLayoutTest] 파생 컴포넌트도 상속받은 프로퍼티를 한 번씩 보인다 — 기반의 카테고리가 먼저 온다
 * @details 타입의 **자기** 프로퍼티만 모으면 스프라이트에 트랜스폼(SceneComponent) · 메시 칸(MeshComponent)이 없다. 언리얼 Details · 유니티
 *          기본 인스펙터처럼 상속분까지, 카테고리는 알파벳 순이 아니라 기반부터 첫 등장 순서로 묶는다.
 */
SW_TEST_CASE( InspectorPropertyLayoutTest, DerivedComponentShowsInheritedPropertiesOnce )
{
    const TypeInfo* pSprite = SpriteComponent::StaticType();
    SW_ASSERT_NOT_NULL( pSprite );
    vector<InspectorPropertyGroup> listGroup;
    InspectorPropertyLayout::collectPropertyGroups( *pSprite, {}, EditorListFilter{ "" }, listGroup );

    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_localPosition" ) ); // SceneComponent
    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_meshId" ) );        // MeshComponent
    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_textureName" ) );   // SpriteComponent
    const size_t transformAt = findGroupIndex( listGroup, "Transform" );
    const size_t renderingAt = findGroupIndex( listGroup, "Rendering" );
    SW_ASSERT_TRUE( transformAt < listGroup.size() );
    SW_ASSERT_TRUE( renderingAt < listGroup.size() );
    SW_EXPECT_TRUE( transformAt < renderingAt );
}

/**
 * @brief [InspectorPropertyLayoutTest] 인스펙터 확장이 직접 그린 프로퍼티만 빠진다 — 나머지는 그대로 보인다
 * @details 확장이 본문을 그릴 때 반사 프로퍼티를 통째로 감추면 카메라의 Priority · Role 과 메시의 Bounds Radius · Blend Mode 를 고칠 수 없다.
 *          언리얼 `IDetailCustomization::HideProperty` 처럼 확장이 그린 것만 뺀다.
 */
SW_TEST_CASE( InspectorPropertyLayoutTest, PropertiesDrawnByAnExtensionAreLeftOut )
{
    const TypeInfo* pCamera = CameraComponent::StaticType();
    SW_ASSERT_NOT_NULL( pCamera );
    vector<InspectorPropertyGroup> listGroup;
    InspectorPropertyLayout::collectPropertyGroups( *pCamera, { hashed_string( "_fovY" ), hashed_string( "_nearZ" ) }, EditorListFilter{ "" }, listGroup );

    SW_EXPECT_EQUAL( 0u, countProperty( listGroup, "_fovY" ) );
    SW_EXPECT_EQUAL( 0u, countProperty( listGroup, "_nearZ" ) );
    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_farZ" ) );
    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_priority" ) );
    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_role" ) );
    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_localPosition" ) );
}

/**
 * @brief [InspectorPropertyLayoutTest] 타입 사슬은 기반 → 파생 순서다 — 확장도 이 순서로 찾고 그린다
 * @details 확장을 정확한 타입 이름으로만 찾으면 게임이 만든 SceneComponent 파생에는 트랜스폼 칸이 없다.
 */
SW_TEST_CASE( InspectorPropertyLayoutTest, TypeChainRunsFromBaseToDerived )
{
    vector<const TypeInfo*> listType;
    InspectorPropertyLayout::collectTypeChain( *SpriteComponent::StaticType(), listType );
    SW_ASSERT_TRUE( listType.size() >= 3 );
    SW_EXPECT_TRUE( listType.back() == SpriteComponent::StaticType() );
    size_t sceneAt = listType.size();
    size_t meshAt  = listType.size();
    for ( size_t index = 0; index < listType.size(); ++index )
    {
        if ( listType[index] == SceneComponent::StaticType() )
            sceneAt = index;
        if ( listType[index] == MeshComponent::StaticType() )
            meshAt = index;
    }
    SW_EXPECT_TRUE( sceneAt < meshAt );
    SW_EXPECT_TRUE( meshAt < listType.size() - 1 );
}

/**
 * @brief [InspectorPropertyLayoutTest] 라디안으로 저장한 각도는 도로 보인다 — 트랜스폼 회전 · FOV 가 같은 규칙이다
 * @details 트랜스폼 회전은 라디안으로 저장된다. 메타가 그것을 `Units=deg` 로 적으면 인스펙터가 그 라디안을 1 픽셀에 0.5(약 29 도)씩 움직인다.
 *          언리얼 Details(FRotator · FOV) · 유니티 인스펙터(`localEulerAngles` · `fieldOfView`)는 각도를 도로 보인다. 저장은 그대로 라디안이다.
 */
SW_TEST_CASE( InspectorPropertyLayoutTest, RadianAnglesAreShownInDegrees )
{
    const PropertyInfo* pRotation = SceneComponent::StaticType()->findPropertyInHierarchy( hashed_string( "_localRotation" ) );
    const PropertyInfo* pPosition = SceneComponent::StaticType()->findPropertyInHierarchy( hashed_string( "_localPosition" ) );
    const PropertyInfo* pFov      = CameraComponent::StaticType()->findPropertyInHierarchy( hashed_string( "_fovY" ) );
    SW_ASSERT_NOT_NULL( pRotation );
    SW_ASSERT_NOT_NULL( pPosition );
    SW_ASSERT_NOT_NULL( pFov );

    // 저장 단위는 라디안이라고 적혀 있어야 한다(엔진의 `setLocalRotation` 이 받는 단위).
    const string* pRotationUnits = pRotation->findCustomMeta( hashed_string( "Units" ) );
    SW_ASSERT_NOT_NULL( pRotationUnits );
    SW_EXPECT_EQUAL( string( "rad" ), *pRotationUnits );

    for ( const PropertyInfo* pAngle : { pRotation, pFov } )
    {
        const InspectorDisplayUnit unit = InspectorPropertyLayout::getDisplayUnit( *pAngle );
        // pi/2 라디안이 90 도로 보인다.
        SW_EXPECT_NEAR_EQUAL( 90.0f, 1.5707963f * unit._scale, 1e-3f );
        SW_EXPECT_EQUAL( string( "deg" ), unit._suffix );
        // 드래그는 도 단위다 — 한 픽셀이 1 도 이하.
        SW_EXPECT_TRUE( unit._dragSpeed > 0.0f && unit._dragSpeed <= 1.0f );
    }

    // 각도가 아닌 단위는 배율 없이 글자만 붙는다.
    const InspectorDisplayUnit meters = InspectorPropertyLayout::getDisplayUnit( *pPosition );
    SW_EXPECT_EQUAL( 1.0f, meters._scale );
    SW_EXPECT_EQUAL( string( "m" ), meters._suffix );
}

/**
 * @brief [InspectorPropertyLayoutTest] 0..1 비율(`Units=ratio`)은 백분율로 보이고, 단위 글자의 `%` 는 서식을 깨지 않는다
 * @details 0..1 비율을 `Units=%` 로 적으면 0.5 가 "0.5 %" 라는 뜻이 되고, 그 `%` 는 printf 서식에 그대로 붙어 짝 없는 변환 지정자가
 *          된다(UCRT 는 조용히 버려 단위가 화면에 나오지도 않는다). 비율은 `ratio` 로 적고 × 100 · "%" 로 보이며,
 *          서식에는 `%%` 로 들어간다. 저장 값은 그대로 0..1 이다.
 */
SW_TEST_CASE( InspectorPropertyLayoutTest, RatiosAreShownAsPercent )
{
    PropertyInfo ratio;
    ratio._metadata._mapCustomMeta[hashed_string( "Units" )] = "ratio";
    const InspectorDisplayUnit unit                          = InspectorPropertyLayout::getDisplayUnit( ratio );
    // 0.25 가 25 % 로 보인다. 드래그는 퍼센트 단위다 — 한 픽셀이 1 % 이하.
    SW_EXPECT_NEAR_EQUAL( 25.0f, 0.25f * unit._scale, 1e-4f );
    SW_EXPECT_EQUAL( string( "%" ), unit._suffix );
    SW_EXPECT_TRUE( unit._dragSpeed > 0.0f && unit._dragSpeed <= 1.0f );

    // 서식에는 `%%` 로 들어간다 — printf 가 글자 `%` 하나를 찍는다.
    SW_EXPECT_EQUAL( string( "%.3f %%" ), InspectorPropertyLayout::appendUnitSuffix( "%.3f", unit._suffix ) );
    SW_EXPECT_EQUAL( string( "%.3f m/s" ), InspectorPropertyLayout::appendUnitSuffix( "%.3f", "m/s" ) );
    SW_EXPECT_EQUAL( string( "%d" ), InspectorPropertyLayout::appendUnitSuffix( "%d", "" ) );
}

#endif // !SW_SHIPPING
