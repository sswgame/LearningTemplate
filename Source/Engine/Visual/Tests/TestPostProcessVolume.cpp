#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/View/PostProcessValues.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/PropertyRoleUtil.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Visual/Component/PostProcessVolumeComponent.h"

using namespace sw;

namespace
{
    /** @brief 볼륨 하나를 가진 오브젝트를 만듭니다. */
    PostProcessVolumeComponent* addVolume( GameObjectManager& manager, const char* pName, const float3& position )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        PostProcessVolumeComponent* pVolume = pObject->addComponent<PostProcessVolumeComponent>();
        if ( pVolume != nullptr )
            pVolume->setLocalPosition( position );
        return pVolume;
    }

    /** @brief 경로 · 무게 · 자리의 블렌더블 하나입니다. 경로는 데이터 읽기로만 들어가므로(`DataPath`) 텍스트 처리기로 넣는다. */
    PostProcessBlendable makeBlendable( const char* pPath, float32 weight, PostProcessBlendableLocation location )
    {
        PostProcessBlendable blendable;
        blendable._weight   = weight;
        blendable._location = location;
        const bool bParsed  = SerializerUtil::parseTextValue( MutableValueAddress{ &blendable._materialPath }, hashed_string( "DataPath" ), pPath,
                                                              SerializeContext::getDefault( SerializePurpose::Authoring ) );
        SW_EXPECT_TRUE( bParsed );
        return blendable;
    }

    /** @brief 목록에서 @p pPath 항목의 무게입니다. 없으면 -1 입니다. */
    float32 findWeight( const vector<PostProcessBlendable>& listBlendable, const char* pPath )
    {
        for ( const PostProcessBlendable& blendable : listBlendable )
        {
            if ( blendable._materialPath == string_view( pPath ) )
                return blendable._weight;
        }
        return -1.0f;
    }
} // namespace

/** @brief [PostProcessVolumeTest] 덮어쓰기 표시는 값 칸마다 하나이고 같은 차례 · 이름(`_b` + 첫 글자 대문자)이다. 모든 값 칸은 섞을 수 있는 형식이거나 섞기가 따로 다루는 칸이다 */
SW_TEST_CASE( PostProcessVolumeTest, OverrideFlagsPairEveryValue )
{
    const PropertyListView listValue    = PostProcessValues::StaticType()->getPropertiesWithBase();
    const PropertyListView listOverride = PostProcessOverrides::StaticType()->getPropertiesWithBase();
    SW_REQUIRE_EQUAL( listValue.size(), listOverride.size() );
    for ( size_t index = 0; index < listValue.size(); ++index )
    {
        const string_view valueName = listValue[index]._name.view();
        SW_REQUIRE_TRUE( valueName.size() > 1 && valueName[0] == '_' );
        string expected = "_b";
        expected.push_back( static_cast<char>( valueName[1] >= 'a' && valueName[1] <= 'z' ? valueName[1] - 'a' + 'A' : valueName[1] ) );
        expected.append( valueName.substr( 2 ) );
        SW_EXPECT_EQUAL( string_view( expected ), listOverride[index]._name.view() );
        SW_EXPECT_TRUE( listOverride[index]._bIsBitField == SW_TRUE );
        // 섞을 수 없는 칸은 경로(DataPath)와 후처리 머티리얼 목록뿐이다 — 섞기가 주소로 골라 따로 다룬다. 다른 형식을 더하면 blendOverrides 에 줄을 더하고 여기를 넓힌다.
        const bool bHandledByAddress = listValue[index]._typeName.view() == "DataPath" || valueName == "_listBlendable";
        SW_EXPECT_TRUE( PropertyRoleUtil::isInterpolatable( listValue[index] ) || bHandledByAddress );
    }
}

/** @brief [PostProcessVolumeTest] 무게는 모양 안 1, 블렌드 반경 동안 1 → 0, 그 밖 0 이고 블렌드 무게를 곱한다. 무한 볼륨은 어디서나 블렌드 무게다 */
SW_TEST_CASE( PostProcessVolumeTest, WeightFallsOffOverBlendRadius )
{
    GameObjectManager           manager;
    PostProcessVolumeComponent* pVolume = addVolume( manager, "Volume", float3{ 10.0f, 0.0f, 0.0f } );
    SW_REQUIRE_NOT_NULL( pVolume );

    pVolume->setShape( PostProcessVolumeShape::Box, float3{ 2.0f, 2.0f, 2.0f }, 0.0f, 4.0f, 1.0f, 0.0f );
    SW_EXPECT_NEAR( 1.0f, pVolume->computeWeight( float3{ 11.0f, 1.0f, 0.0f } ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.5f, pVolume->computeWeight( float3{ 6.0f, 0.0f, 0.0f } ), 1.0e-5f ); // 상자 면(x 8)에서 2 m 밖
    SW_EXPECT_NEAR( 0.0f, pVolume->computeWeight( float3{ 4.0f, 0.0f, 0.0f } ), 1.0e-5f ); // 4 m 밖 — 반경 끝

    pVolume->setShape( PostProcessVolumeShape::Sphere, float3{}, 3.0f, 2.0f, 0.5f, 0.0f );
    SW_EXPECT_NEAR( 0.5f, pVolume->computeWeight( float3{ 10.0f, 0.0f, 0.0f } ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.25f, pVolume->computeWeight( float3{ 6.0f, 0.0f, 0.0f } ), 1.0e-5f ); // 구 면에서 1 m 밖

    pVolume->setShape( PostProcessVolumeShape::Box, float3{ 1.0f, 1.0f, 1.0f }, 0.0f, 0.0f, 1.0f, 0.0f );
    SW_EXPECT_NEAR( 0.0f, pVolume->computeWeight( float3{ 12.0f, 0.0f, 0.0f } ), 1.0e-5f ); // 반경 0 — 밖은 바로 0

    pVolume->setShape( PostProcessVolumeShape::Unbound, float3{}, 0.0f, 0.0f, 0.75f, 0.0f );
    SW_EXPECT_NEAR( 0.75f, pVolume->computeWeight( float3{ -1000.0f, 0.0f, 0.0f } ), 1.0e-5f );
}

/** @brief [PostProcessVolumeTest] 볼륨은 켠 칸만 카메라 값 위에 섞고, 겹치면 우선순위가 높은 것이 마지막에 덮는다. 꺼진 칸은 카메라 값 그대로다 */
SW_TEST_CASE( PostProcessVolumeTest, ResolveBlendsOverriddenPropertiesByPriority )
{
    GameObjectManager           manager;
    PostProcessVolumeComponent* pLow  = addVolume( manager, "Low", float3{} );
    PostProcessVolumeComponent* pHigh = addVolume( manager, "High", float3{} );
    SW_REQUIRE_NOT_NULL( pLow );
    SW_REQUIRE_NOT_NULL( pHigh );

    PostProcessValues camera;
    camera._saturation = 0.5f;
    camera._contrast   = 1.5f;

    PostProcessValues    lowValues;
    PostProcessOverrides lowOverrides;
    lowValues._saturation            = 2.0f;
    lowValues._vignetteIntensity     = 1.0f;
    lowOverrides._bSaturation        = SW_TRUE;
    lowOverrides._bVignetteIntensity = SW_TRUE;
    pLow->setSettings( lowValues, lowOverrides );
    pLow->setShape( PostProcessVolumeShape::Unbound, float3{}, 0.0f, 0.0f, 1.0f, 5.0f );

    PostProcessValues    highValues;
    PostProcessOverrides highOverrides;
    highValues._saturation     = 0.0f;
    highOverrides._bSaturation = SW_TRUE;
    pHigh->setSettings( highValues, highOverrides );
    pHigh->setShape( PostProcessVolumeShape::Unbound, float3{}, 0.0f, 0.0f, 0.5f, 10.0f );

    // 우선순위 5 가 먼저(채도 2, 비네트 1), 10 이 반 무게로 그 위(채도 2 → 0 의 절반 = 1). 대비는 아무도 켜지 않아 카메라 값이다.
    const PostProcessValues resolved = PostProcessVolumeComponent::resolveAt( manager.getComponentRegistry(), float3{}, camera );
    SW_EXPECT_NEAR( 1.0f, resolved._saturation, 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, resolved._vignetteIntensity, 1.0e-5f );
    SW_EXPECT_NEAR( 1.5f, resolved._contrast, 1.0e-5f );

    // 우선순위를 뒤집으면 높은 쪽(이제 Low)이 마지막에 덮는다: 채도 0.5 → 0 의 절반 0.25, 그 위 2 로 끝까지 = 2.
    pHigh->setShape( PostProcessVolumeShape::Unbound, float3{}, 0.0f, 0.0f, 0.5f, 1.0f );
    const PostProcessValues swapped = PostProcessVolumeComponent::resolveAt( manager.getComponentRegistry(), float3{}, camera );
    SW_EXPECT_NEAR( 2.0f, swapped._saturation, 1.0e-5f );
}

/** @brief [PostProcessVolumeTest] 볼륨의 후처리 머티리얼은 더해진다 — 같은 (경로, 자리) 는 무게를 섞고, 새 항목은 볼륨 무게로 들어오며, 카메라의 항목은 남는다 */
SW_TEST_CASE( PostProcessVolumeTest, VolumeBlendablesAddByWeight )
{
    vector<PostProcessBlendable> list;
    list.push_back( makeBlendable( "engine/materials/postprocess/a.material", 1.0f, PostProcessBlendableLocation::AfterTonemap ) );
    vector<PostProcessBlendable> volume;
    volume.push_back( makeBlendable( "engine/materials/postprocess/a.material", 0.0f, PostProcessBlendableLocation::AfterTonemap ) );
    volume.push_back( makeBlendable( "engine/materials/postprocess/b.material", 0.8f, PostProcessBlendableLocation::BeforeTonemap ) );
    PostProcessBlendable::blendList( list, volume, 0.5f, false );
    SW_EXPECT_NEAR( 0.5f, findWeight( list, "engine/materials/postprocess/a.material" ), 1.0e-5f ); // 1 → 0 의 절반
    SW_EXPECT_NEAR( 0.4f, findWeight( list, "engine/materials/postprocess/b.material" ), 1.0e-5f ); // 0 → 0.8 의 절반
    SW_EXPECT_TRUE( PostProcessBlendable::hasActive( list, PostProcessBlendableLocation::BeforeTonemap ) );
    SW_EXPECT_FALSE( PostProcessBlendable::hasActive( list, PostProcessBlendableLocation::ReplacingTonemap ) );

    // 같은 경로라도 자리가 다르면 다른 항목이다. 무게가 0 이 된 항목은 지워진다.
    vector<PostProcessBlendable> off;
    off.push_back( makeBlendable( "engine/materials/postprocess/a.material", 0.0f, PostProcessBlendableLocation::AfterTonemap ) );
    PostProcessBlendable::blendList( list, off, 1.0f, false );
    SW_EXPECT_NEAR( -1.0f, findWeight( list, "engine/materials/postprocess/a.material" ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.4f, findWeight( list, "engine/materials/postprocess/b.material" ), 1.0e-5f );
}

/** @brief [PostProcessVolumeTest] 포즈 블렌드는 앞 카메라에만 있던 머티리얼을 넘어가는 만큼 줄인다 — 다 넘어가면 사라진다 */
SW_TEST_CASE( PostProcessVolumeTest, PoseBlendFadesMissingBlendables )
{
    PostProcessValues from;
    from._listBlendable.push_back( makeBlendable( "engine/materials/postprocess/a.material", 1.0f, PostProcessBlendableLocation::AfterTonemap ) );
    PostProcessValues to;
    to._listBlendable.push_back( makeBlendable( "engine/materials/postprocess/b.material", 1.0f, PostProcessBlendableLocation::AfterTonemap ) );
    const PostProcessValues quarter = PostProcessValues::blend( from, to, 0.25f );
    SW_EXPECT_NEAR( 0.75f, findWeight( quarter._listBlendable, "engine/materials/postprocess/a.material" ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.25f, findWeight( quarter._listBlendable, "engine/materials/postprocess/b.material" ), 1.0e-5f );
    const PostProcessValues full = PostProcessValues::blend( from, to, 1.0f );
    SW_EXPECT_NEAR( -1.0f, findWeight( full._listBlendable, "engine/materials/postprocess/a.material" ), 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, findWeight( full._listBlendable, "engine/materials/postprocess/b.material" ), 1.0e-5f );
}

/** @brief [PostProcessVolumeTest] 볼륨은 목록 칸을 켜야 머티리얼을 더한다 — 켜지 않은 볼륨은 카메라 목록 그대로다 */
SW_TEST_CASE( PostProcessVolumeTest, VolumeBlendablesNeedTheOverrideFlag )
{
    GameObjectManager           manager;
    PostProcessVolumeComponent* pVolume = addVolume( manager, "Volume", float3{} );
    SW_REQUIRE_NOT_NULL( pVolume );
    PostProcessValues values;
    values._listBlendable.push_back( makeBlendable( "engine/materials/postprocess/a.material", 1.0f, PostProcessBlendableLocation::BeforeTonemap ) );
    PostProcessOverrides overrides;
    pVolume->setSettings( values, overrides );
    pVolume->setShape( PostProcessVolumeShape::Unbound, float3{}, 0.0f, 0.0f, 1.0f, 0.0f );
    SW_EXPECT_TRUE( PostProcessVolumeComponent::resolveAt( manager.getComponentRegistry(), float3{}, PostProcessValues{} )._listBlendable.empty() );

    overrides._bListBlendable = SW_TRUE;
    pVolume->setSettings( values, overrides );
    const PostProcessValues resolved = PostProcessVolumeComponent::resolveAt( manager.getComponentRegistry(), float3{}, PostProcessValues{} );
    SW_EXPECT_NEAR( 1.0f, findWeight( resolved._listBlendable, "engine/materials/postprocess/a.material" ), 1.0e-5f );
}

/** @brief [PostProcessVolumeTest] 포즈 블렌드는 모든 칸을 섞고, 레터박스는 한쪽에만 있으면 절반에서 바꾼다 */
SW_TEST_CASE( PostProcessVolumeTest, BlendMixesEveryFieldAndStepsLetterbox )
{
    PostProcessValues from;
    PostProcessValues to;
    to._exposureCompensation        = 2.0f;
    to._letterboxAspectRatio        = 2.39f;
    const PostProcessValues quarter = PostProcessValues::blend( from, to, 0.25f );
    SW_EXPECT_NEAR( 0.5f, quarter._exposureCompensation, 1.0e-5f );
    SW_EXPECT_NEAR( 0.0f, quarter._letterboxAspectRatio, 1.0e-5f );
    const PostProcessValues half = PostProcessValues::blend( from, to, 0.5f );
    SW_EXPECT_NEAR( 2.39f, half._letterboxAspectRatio, 1.0e-5f );

    from._letterboxAspectRatio   = 1.85f;
    const PostProcessValues both = PostProcessValues::blend( from, to, 0.5f );
    SW_EXPECT_NEAR( ( 1.85f + 2.39f ) * 0.5f, both._letterboxAspectRatio, 1.0e-5f );
}
