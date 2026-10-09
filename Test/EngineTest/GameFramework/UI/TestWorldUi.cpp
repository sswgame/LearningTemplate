#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/GpuSpriteInstanceData.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UI/World/WidgetComponent.h"

#include "EngineTest/StateReloadTestUtil.h"

#include "GameFramework/Base/Actor/Combat/HealthListenerComponent.h"
#include "GameFramework/Base/UI/UI/DamageNumberComponent.h"
#include "GameFramework/Base/UI/UI/HealthBarComponent.h"
#include "GameFramework/Base/World/World/FadeOutComponent.h"

#include "TestFramework/TestFramework.h"

// 월드에 붙는 UI — HP 바 · 데미지 숫자는 화면 마커(WidgetComponent)의 위젯 값을 채우는가(막대 비율 · 색 · 글 · 불투명도 · 숨김), 이펙트 페이드는 스프라이트 색을 흐리는가.

namespace
{
    /** @brief 이름의 프로퍼티(별칭 포함)에 값을 넣습니다. 세터가 없는 칸을 시험이 정할 때 씁니다. */
    template <typename TComponent, typename T>
    bool setReflectedValue( TComponent* pComponent, const utf8* pName, const T& value )
    {
        const sw::TypeInfo*     pTypeInfo = pComponent->getTypeInfo();
        const sw::PropertyInfo* pProperty = ( pTypeInfo != nullptr ) ? pTypeInfo->findPropertyInHierarchy( sw::hashed_string( pName ) ) : nullptr;
        if ( pProperty == nullptr )
            return false;
        pProperty->setValue<T>( pComponent, value );
        return true;
    }

    /** @brief 씬 컴포넌트를 가진 오브젝트 하나를 @p position 에 만듭니다. */
    sw::GameObject* spawnAnchoredObject( sw::GameObjectManager& manager, const utf8* pName, const sw::float3& position )
    {
        sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        sw::SceneComponent* pRoot = pObject->addComponent<sw::SceneComponent>();
        if ( pRoot == nullptr )
            return nullptr;
        pRoot->setLocalPosition( position );
        manager.flushSceneTransforms();
        return pObject;
    }

    /** @brief 씬 컴포넌트 · 화면 마커(`WidgetComponent`)를 가진 오브젝트 하나를 @p position 에 만듭니다. UI 시스템이 없어 위젯은 마커에 들려 있다. */
    sw::GameObject* spawnMarkedObject( sw::GameObjectManager& manager, const utf8* pName, const sw::float3& position )
    {
        sw::GameObject* pObject = spawnAnchoredObject( manager, pName, position );
        if ( pObject == nullptr || pObject->addComponent<sw::WidgetComponent>() == nullptr )
            return nullptr;
        return pObject;
    }
} // namespace

/**
 * @brief [WorldUiTest] HP 바는 같은 오브젝트의 화면 마커에 겹친 진행 막대 둘을 넣는다 — 위(채움)는 맞으면 바로 줄고, 아래(흔적)가 따라 줄며, 차오를 때는 흔적이 없다
 * @details 참 비율(`setTargetRatio`)이 입력이고 채움은 줄 때 바로 · 늘 때 차오르며, 흔적은 채움까지 줄어든다. 아래 막대 = 바탕 색 위의 흔적(`_remainRatio` 까지),
 *          위 막대 = 바탕 없는 채움(`_hpRatio` 까지) — UI 는 위젯 순서로 그려 겹쳐도 앞뒤가 바뀌지 않는다. 숨기거나 소유 오브젝트를 끄면 마커를 숨긴다.
 *          변이: `refreshWidgets` 가 아래 막대에 흔적 비율 대신 채움을 넣으면 맞은 직후 흔적 단언이 진다.
 */
SW_TEST_CASE( WorldUiTest, HPBarFillsItsMarkerBarsAndTheTrailFollows )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    sw::GameObject*       pHero = spawnMarkedObject( manager, "Hero", sw::float3{ 2.0f, 3.0f, 0.5f } );
    SW_ASSERT_NOT_NULL( pHero );
    sw::HealthBarComponent* pBar = pHero->addComponent<sw::HealthBarComponent>();
    SW_ASSERT_NOT_NULL( pBar );
    pBar->resetRatio( 1.0f );
    pBar->setVisible( true );
    pBar->dispatchBeginPlay();

    const sw::WidgetComponent* pMarker = pBar->findWidgetComponent();
    SW_ASSERT_NOT_NULL( pMarker );
    const sw::ProgressBarWidget* pTrail = pBar->findTrailBar();
    const sw::ProgressBarWidget* pFill  = pBar->findFillBar();
    SW_ASSERT_NOT_NULL( pTrail );
    SW_ASSERT_NOT_NULL( pFill );
    SW_EXPECT_FALSE( pMarker->isHidden() );
    // 가득 찬 바: 채움 1, 흔적 1(채움에 덮인다). 아래 막대만 바탕을 칠한다.
    SW_EXPECT_NEAR_EQUAL( 1.0f, pFill->getPercent(), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pTrail->getPercent(), 1e-6f );
    SW_EXPECT_TRUE( pFill->getBackgroundColor()._w == 0.0f );
    SW_EXPECT_TRUE( pTrail->getBackgroundColor()._w > 0.0f );
    SW_EXPECT_FALSE( pFill->getFillColor() == pTrail->getFillColor() );

    // 맞았다(1 → 0.4): 채움은 그 틱에 바로 0.4, 흔적은 0.4 와 1 사이에 남는다.
    pBar->setTargetRatio( 0.4f );
    pBar->onTick( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pBar->getHpRatio(), 1e-6f );
    SW_EXPECT_TRUE( 0.4f < pBar->getRemainRatio() && pBar->getRemainRatio() < 1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pFill->getPercent(), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( pBar->getRemainRatio(), pTrail->getPercent(), 1e-6f );

    // 시간이 지나면 흔적이 채움까지 줄어 사라진다 — 두 막대가 0.4.
    for ( uint32 step = 0; step < 100; ++step )
    {
        pBar->onTick( 0.1f );
    }
    SW_EXPECT_NEAR_EQUAL( 0.4f, pBar->getRemainRatio(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pTrail->getPercent(), 1e-4f );

    // 회복(0.4 → 0.9)은 차오른다 — 흔적은 생기지 않는다(아래 막대 = 채움).
    pBar->setTargetRatio( 0.9f );
    pBar->onTick( 0.1f );
    SW_EXPECT_TRUE( 0.4f < pBar->getHpRatio() && pBar->getHpRatio() < 0.9f );
    SW_EXPECT_NEAR_EQUAL( pBar->getHpRatio(), pBar->getRemainRatio(), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( pFill->getPercent(), pTrail->getPercent(), 1e-6f );

    // 숨기면 마커를 숨기고, 소유 오브젝트를 꺼도 숨긴다.
    pBar->setVisible( false );
    SW_EXPECT_TRUE( pMarker->isHidden() );
    pBar->setVisible( true );
    SW_EXPECT_FALSE( pMarker->isHidden() );
    pHero->setActive( false );
    SW_EXPECT_TRUE( pMarker->isHidden() );
}

/**
 * @brief [WorldUiTest] 데미지 숫자는 값을 글 위젯(스타일 클래스 `damage`)으로 보이고, 수명에 따라 불투명도가 흐려지며, 다하면 오브젝트를 지운다
 * @details 숫자 아틀라스(스프라이트 열한 칸) 대신 글리프 캐시의 글자다 — 음수 · 가장 긴 값(INT32_MIN)도 글 그대로. 변이: `refreshWidget` 이 불투명도에
 *          흐림을 곱하지 않으면 반쯤 산 숫자의 불투명도 단언이 진다.
 */
SW_TEST_CASE( WorldUiTest, DamageNumberShowsItsValueAsText )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    sw::GameObject*       pHit = spawnMarkedObject( manager, "Hit", sw::float3{ 0.0f, 1.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pHit );
    sw::DamageNumberComponent* pDamage = pHit->addComponent<sw::DamageNumberComponent>();
    SW_ASSERT_NOT_NULL( pDamage );
    SW_ASSERT_TRUE( setReflectedValue( pDamage, "_lifeTime", 1.0f ) );
    pDamage->setDamageValue( 123 );
    pDamage->dispatchBeginPlay();

    const sw::TextWidget* pText = pDamage->findTextWidget();
    SW_ASSERT_NOT_NULL( pText );
    SW_EXPECT_STREQ( "123", pText->getText().c_str() );
    SW_EXPECT_STREQ( sw::DamageNumberComponent::kStyleClass, pText->getStyleClass().c_str() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pText->getOpacity(), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( pDamage->getColor()._x, pText->getColor()._x, 1e-6f );

    // 반쯤 살면 알파가 반이다.
    pDamage->onTick( 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pDamage->getAlpha(), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pText->getOpacity(), 1e-5f );

    // 음수 · 가장 긴 값(INT32_MIN — 부호를 뒤집다 넘치지 않는다).
    pDamage->setDamageValue( -7 );
    SW_EXPECT_STREQ( "-7", pText->getText().c_str() );
    pDamage->setDamageValue( std::numeric_limits<int32>::min() );
    SW_EXPECT_STREQ( "-2147483648", pText->getText().c_str() );

    // 수명이 다하면 오브젝트를 지운다.
    pDamage->onTick( 0.6f );
    SW_EXPECT_TRUE( pHit->isPendingDestroy() );
}

/**
 * @brief [WorldUiTest] `spawnNumber` 는 씬 컴포넌트 · 화면 마커(가운데 피벗) · 데미지 숫자를 함께 붙인 오브젝트를 그 자리에 띄운다
 * @details 피해를 내는 곳(유닛 스탯 · 어빌리티)이 이것 하나를 쓴다 — 마커를 빠뜨리면 숫자가 계산만 되고 그려지지 않는다.
 */
SW_TEST_CASE( WorldUiTest, SpawnedDamageNumberCarriesItsMarker )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    manager.beginPlay();
    sw::DamageNumberComponent::spawnNumber( manager, sw::float3{ 1.0f, 2.0f, 3.0f }, 42 );
    manager.tick( 0.016f ); // 플레이 중에 붙은 컴포넌트는 다음 틱 단계에서 시작한다
    sw::DamageNumberComponent* pDamage = nullptr;
    manager.forEachComponentOfType<sw::DamageNumberComponent>( [&pDamage]( sw::DamageNumberComponent* pComponent )
    { pDamage = pComponent; } );
    SW_ASSERT_NOT_NULL( pDamage );
    const sw::WidgetComponent* pMarker = pDamage->findWidgetComponent();
    SW_ASSERT_NOT_NULL( pMarker );
    SW_EXPECT_TRUE( pMarker->getPivot() == ( sw::float2{ 0.5f, 0.5f } ) );
    SW_EXPECT_TRUE( pDamage->getLifeTime() > 0.0f );
    const sw::TextWidget* pText = pDamage->findTextWidget();
    SW_ASSERT_NOT_NULL( pText );
    SW_EXPECT_STREQ( "42", pText->getText().c_str() );
    manager.endPlay();
}

/**
 * @brief [WorldUiTest] 이펙트의 흐림은 같은 오브젝트 스프라이트들의 색 알파에 곱해진다 — 시작할 때의 알파가 기준이다
 * @details 흐림은 계산만으로 끝나지 않고 스프라이트 색에 실려야 한다. 반투명으로 만든 스프라이트(알파 0.8)는 0.8 에서 0 으로
 *          흐려져야 한다 — 흐림을 그대로 넣으면 시작 순간 불투명으로 튄다. 색은 GPU 인스턴스 칸으로 가므로 배치는 그대로다.
 */
SW_TEST_CASE( WorldUiTest, EffectFadesTheSpritesOfItsObject )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    sw::GameObject*       pSpark = manager.createGameObject( sw::hashed_string( "Spark" ) );
    SW_ASSERT_NOT_NULL( pSpark );
    sw::SpriteComponent* pGlow = pSpark->addComponent<sw::SpriteComponent>();
    sw::SpriteComponent* pCore = pSpark->addComponent<sw::SpriteComponent>();
    SW_ASSERT_NOT_NULL( pGlow );
    SW_ASSERT_NOT_NULL( pCore );
    pGlow->setTint( sw::float4{ 1.0f, 0.5f, 0.0f, 0.8f } );
    sw::FadeOutComponent* pEffect = pSpark->addComponent<sw::FadeOutComponent>();
    SW_ASSERT_NOT_NULL( pEffect );
    SW_ASSERT_TRUE( setReflectedValue( pEffect, "_duration", 1.0f ) );
    pEffect->dispatchBeginPlay();
    SW_EXPECT_NEAR_EQUAL( 0.8f, pGlow->getTint()._w, 1e-6f ); // 시작 순간은 그대로다

    pEffect->onTick( 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pEffect->getCurrentAlpha(), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pGlow->getTint()._w, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pCore->getTint()._w, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pGlow->getTint()._y, 1e-6f ); // 색은 건드리지 않는다
    // 그리는 값(인스턴스 칸)에 실렸다.
    SW_EXPECT_NEAR_EQUAL( 0.4f, pGlow->getSpriteInstanceData().getTint()._w, 1.0f / 255.0f );

    pEffect->setCurrentAlpha( 0.25f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, pGlow->getTint()._w, 1e-5f );
}

/**
 * @brief [WorldUiTest] 페이드 중에 상태를 다시 읽은 이펙트는 흐른 시간과 기준 알파를 이어 간다
 * @details 플레이 중 되돌리기 · 핫 리로드는 컴포넌트를 다시 만들고 `onBeginPlay` 를 다시 부른다. 타이머를 0 으로 돌리고 이미 흐려진 스프라이트 알파를
 *          기준으로 다시 잡으면, 반쯤 흐려진 이펙트가 그 알파에서 처음부터 다시 흐려진다(0.8 → 0.4 에서 시작해 수명의 두 배를 산다).
 */
SW_TEST_CASE( WorldUiTest, EffectResumesItsFadeAfterTheStateIsReadAgain )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    sw::GameObject*       pSpark = manager.createGameObject( sw::hashed_string( "Spark" ) );
    SW_ASSERT_NOT_NULL( pSpark );
    sw::SpriteComponent* pGlow = pSpark->addComponent<sw::SpriteComponent>();
    SW_ASSERT_NOT_NULL( pGlow );
    pGlow->setTint( sw::float4{ 1.0f, 0.5f, 0.0f, 0.8f } );
    sw::FadeOutComponent* pEffect = pSpark->addComponent<sw::FadeOutComponent>();
    SW_ASSERT_NOT_NULL( pEffect );
    SW_ASSERT_TRUE( setReflectedValue( pEffect, "_duration", 1.0f ) );
    manager.beginPlay();
    SW_ASSERT_TRUE( pEffect->hasBegunPlay() );

    pEffect->onTick( 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pGlow->getTint()._w, 1e-5f );

    SW_ASSERT_TRUE( sw::StateReloadTestUtil::reloadInPlace( pSpark ) );
    pEffect = pSpark->getComponent<sw::FadeOutComponent>();
    pGlow   = pSpark->getComponent<sw::SpriteComponent>();
    SW_ASSERT_NOT_NULL( pEffect );
    SW_ASSERT_NOT_NULL( pGlow );
    SW_ASSERT_TRUE( pEffect->hasBegunPlay() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pEffect->getCurrentTimer(), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pEffect->getCurrentAlpha(), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pGlow->getTint()._w, 1e-5f );

    // 기준은 처음의 0.8 그대로다 — 남은 반을 마저 흐린다.
    pEffect->onTick( 0.25f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, pEffect->getCurrentAlpha(), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, pGlow->getTint()._w, 1e-5f );
}

/**
 * @brief [WorldUiTest] 이펙트는 흐른 시간이 수명에 닿는 걸음에 지워진다 — 데미지 숫자 · 투사체 · `Countdown` 과 같은 "수명 이상" 경계
 */
SW_TEST_CASE( WorldUiTest, EffectIsDestroyedTheTickItsTimeRunsOut )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    sw::GameObject*       pSpark = manager.createGameObject( sw::hashed_string( "Spark" ) );
    SW_ASSERT_NOT_NULL( pSpark );
    SW_ASSERT_NOT_NULL( pSpark->addComponent<sw::SpriteComponent>() );
    sw::FadeOutComponent* pEffect = pSpark->addComponent<sw::FadeOutComponent>();
    SW_ASSERT_NOT_NULL( pEffect );
    SW_ASSERT_TRUE( setReflectedValue( pEffect, "_duration", 1.0f ) );
    pEffect->dispatchBeginPlay();

    pEffect->onTick( 0.5f );
    SW_EXPECT_FALSE( pSpark->isPendingDestroy() );
    pEffect->onTick( 0.5f ); // 흐른 시간 1.0 == 수명
    SW_EXPECT_NEAR_EQUAL( 0.0f, pEffect->getCurrentAlpha(), 1e-6f );
    SW_EXPECT_TRUE( pSpark->isPendingDestroy() );
}

/**
 * @brief [WorldUiTest] 떠 있는 동안 상태를 다시 읽은 데미지 숫자는 남은 수명을 이어 간다
 * @details `onBeginPlay` 가 수명을 0 · 알파를 1 로 돌리면, 플레이 중 되돌리기 · 핫 리로드 때마다 떠 있던 숫자가 처음부터 다시 떠오른다.
 */
SW_TEST_CASE( WorldUiTest, DamageNumberKeepsItsLifeAfterTheStateIsReadAgain )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    sw::GameObject*       pHit = spawnMarkedObject( manager, "Hit", sw::float3{ 0.0f, 1.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pHit );
    sw::DamageNumberComponent* pDamage = pHit->addComponent<sw::DamageNumberComponent>();
    SW_ASSERT_NOT_NULL( pDamage );
    SW_ASSERT_TRUE( setReflectedValue( pDamage, "_lifeTime", 1.0f ) );
    pDamage->setDamageValue( 42 );
    manager.beginPlay();
    pDamage->onTick( 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pDamage->getAlpha(), 1e-5f );

    SW_ASSERT_TRUE( sw::StateReloadTestUtil::reloadInPlace( pHit ) );
    pDamage = pHit->getComponent<sw::DamageNumberComponent>();
    SW_ASSERT_NOT_NULL( pDamage );
    SW_ASSERT_TRUE( pDamage->hasBegunPlay() );
    SW_EXPECT_EQUAL( 42, pDamage->getDamageValue() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pDamage->getAlpha(), 1e-5f );
    const sw::TextWidget* pText = pDamage->findTextWidget();
    SW_ASSERT_NOT_NULL( pText );
    SW_EXPECT_STREQ( "42", pText->getText().c_str() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pText->getOpacity(), 1e-5f );

    // 남은 반이 지나면 지운다.
    pDamage->onTick( 0.6f );
    SW_EXPECT_TRUE( pHit->isPendingDestroy() );
}

/**
 * @brief [WorldUiTest] 플레이 중에 데미지 숫자의 색 칸을 고치면(인스펙터 · 데이터 핫 리로드 알림) 글 위젯이 새 색으로 다시 칠해진다
 * @details 위젯 값을 시작할 때만 넣으면 `onPropertyChanged` 뒤에도 옛 색이 남는다. 변이: `onPropertyChanged` 의 `refreshWidget` 을 빼면 진다.
 */
SW_TEST_CASE( WorldUiTest, DamageNumberStyleChangeRepaints )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    sw::GameObject*       pHit = spawnMarkedObject( manager, "Hit", sw::float3{ 0.0f, 1.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pHit );
    sw::DamageNumberComponent* pDamage = pHit->addComponent<sw::DamageNumberComponent>();
    SW_ASSERT_NOT_NULL( pDamage );
    pDamage->setDamageValue( 7 );
    pDamage->dispatchBeginPlay();
    const sw::TextWidget* pText = pDamage->findTextWidget();
    SW_ASSERT_NOT_NULL( pText );
    SW_EXPECT_FALSE( pText->getColor()._z > 0.9f ); // 기본 노랑

    SW_ASSERT_TRUE( setReflectedValue( pDamage, "_color", sw::float4{ 0.2f, 0.4f, 1.0f, 0.5f } ) );
    pDamage->onPropertyChanged( sw::hashed_string( "_color" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pText->getColor()._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pText->getColor()._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pText->getOpacity(), 1e-6f ); // 색 알파 × 흐림(수명 0 이면 1)
}

/**
 * @brief [WorldUiTest] HP 바는 같은 오브젝트의 체력 신호(`HealthListenerComponent::broadcast`)로만 움직이고, 보이기 정책은 바가 정한다
 * @details 체력 시스템은 바를 모른다 — 다시 두기는 흔적 없이, 바뀜은 목표만. `_bShowWhenHurt` 면 처음 줄 때 보이고, `_bHideWhenDead` 면 쓰러질 때 숨는다.
 */
SW_TEST_CASE( WorldUiTest, HPBarFollowsHealthSignalAndOwnsItsVisibility )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pEnemy = spawnAnchoredObject( manager, "Enemy", sw::float3{ 0.0f, 0.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pEnemy );
    sw::HealthBarComponent* pBar = pEnemy->addComponent<sw::HealthBarComponent>();
    SW_ASSERT_NOT_NULL( pBar );
    SW_ASSERT_TRUE( setReflectedValue( pBar, "_bShowWhenHurt", true ) );
    SW_ASSERT_TRUE( setReflectedValue( pBar, "_bHideWhenDead", true ) );

    sw::HealthChangedEvent event;
    event._ratio = 1.0f;
    event._kind  = sw::HealthChangeKind::Reset;
    sw::HealthListenerComponent::broadcast( *pEnemy, event );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pBar->getRemainRatio(), 1e-6f ); // 다시 두기는 흔적 없이
    SW_EXPECT_FALSE( pBar->isVisible() );                        // 아직 맞지 않았다

    event._ratio = 0.6f;
    event._kind  = sw::HealthChangeKind::Changed;
    sw::HealthListenerComponent::broadcast( *pEnemy, event );
    SW_EXPECT_NEAR_EQUAL( 0.6f, pBar->getTargetRatio(), 1e-6f );
    SW_EXPECT_TRUE( pBar->isVisible() ); // 처음 줄 때 보인다

    event._ratio = 0.0f;
    event._kind  = sw::HealthChangeKind::Died;
    sw::HealthListenerComponent::broadcast( *pEnemy, event );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pBar->getTargetRatio(), 1e-6f );
    SW_EXPECT_FALSE( pBar->isVisible() ); // 쓰러지면 숨는다
}

/**
 * @brief [WorldUiTest] 한 방에 쓰러져도 `_bShowWhenHurt` 인 바는 보인다 — `_bHideWhenDead` 가 아니면
 * @details 쓰러짐(`Died`)도 줄어든 것이다. 바뀜에서만 보이게 하면 한 방에 죽은 적의 바가 끝내 나타나지 않는다.
 */
SW_TEST_CASE( WorldUiTest, HPBarShowsOnAOneHitKillUnlessItHidesOnDeath )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pEnemy = spawnAnchoredObject( manager, "Enemy", sw::float3{ 0.0f, 0.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pEnemy );
    sw::HealthBarComponent* pBar = pEnemy->addComponent<sw::HealthBarComponent>();
    SW_ASSERT_NOT_NULL( pBar );
    SW_ASSERT_TRUE( setReflectedValue( pBar, "_bShowWhenHurt", true ) );

    sw::HealthChangedEvent event;
    event._ratio = 1.0f;
    event._kind  = sw::HealthChangeKind::Reset;
    sw::HealthListenerComponent::broadcast( *pEnemy, event );
    SW_EXPECT_FALSE( pBar->isVisible() );

    event._ratio = 0.0f;
    event._kind  = sw::HealthChangeKind::Died;
    sw::HealthListenerComponent::broadcast( *pEnemy, event );
    SW_EXPECT_TRUE( pBar->isVisible() );
}
