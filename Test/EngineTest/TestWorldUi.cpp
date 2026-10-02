#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Graphics/Shader/Binding/GpuSpriteInstanceData.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/MeshInstanceBatch.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Resource/SpriteClipCache.h"

#include "GameFramework/Base/EffectBaseComponent.h"
#include "GameFramework/UI/DamageUIComponent.h"
#include "GameFramework/UI/HPBarBaseComponent.h"

#include "TestFramework/TestFramework.h"

// 월드 공간 UI — HP 바 · 데미지 숫자 · 이펙트 페이드가 실제로 스프라이트를 내는가(조각의 자리 · 길이 · 프레임 · 색).

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

    /** @brief 항목 하나의 (중심 X, 폭) 입니다. 사각형 메시의 한 변이 1 이라 월드 스케일 X 가 곧 폭입니다. */
    sw::float2 getEntrySpan( const sw::MeshInstanceBatch& batch, uint32 index )
    {
        const sw::float4x4& world = batch.getEntry( index )._world;
        return sw::float2{ world.getTranslation()._x, world.getScale()._x };
    }
} // namespace

/**
 * @brief [WorldUiTest] HP 바는 채움 · 흔적 · 바탕 세 조각을 겹치지 않게 그리고, 맞으면 채움이 바로 줄고 흔적이 따라 줄며, 차오를 때는 흔적이 없다
 * @details 예전 HP 바는 비율 셋을 보간하기만 하고 그리는 곳이 없었다(`_hpRatio` 는 시작 뒤 한 번도 바뀌지 않았다). 지금은 참 비율(`setTargetRatio`)이 입력이고
 *          채움은 줄 때 바로 · 늘 때 차오르며, 흔적은 채움까지 줄어든다. 조각은 [0, hp] · [hp, remain] · [remain, 1] 이라 겹치지 않는다 — 같은 깊이의
 *          반투명 조각이 겹치면 순서를 카메라 거리 정렬에 맡기게 된다. 숨기거나 소유 오브젝트를 끄면 조각도 숨는다.
 */
SW_TEST_CASE( WorldUiTest, HPBarDrawsFillTrailAndBackgroundWithoutOverlap )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::GameObjectManager manager;
    sw::GameObject*       pHero = spawnAnchoredObject( manager, "Hero", sw::float3{ 2.0f, 3.0f, 0.5f } );
    SW_ASSERT_NOT_NULL( pHero );
    sw::HPBarBaseComponent* pBar = pHero->addComponent<sw::HPBarBaseComponent>();
    SW_ASSERT_NOT_NULL( pBar );
    SW_ASSERT_TRUE( setReflectedValue( pBar, "offsetPos", sw::float2{ 0.0f, 0.8f } ) );
    pBar->resetRatio( 1.0f );
    pBar->setVisible( true );
    pBar->dispatchBeginPlay();

    const sw::MeshInstanceBatch* pBatch = pBar->getSpriteBatch().getBatch();
    SW_ASSERT_NOT_NULL( pBatch );
    SW_ASSERT_EQUAL( sw::HPBarBaseComponent::kEntryCount, pBatch->getCount() );
    SW_EXPECT_TRUE( pBatch->isVisible() );
    // 가득 찬 바: 채움 하나가 바 전체(폭 1, 중심 = 소유자 + 오프셋), 흔적 · 바탕은 길이 0 이라 숨는다.
    SW_EXPECT_TRUE( pBatch->isEntryVisible( sw::HPBarBaseComponent::kFillEntry ) );
    SW_EXPECT_FALSE( pBatch->isEntryVisible( sw::HPBarBaseComponent::kTrailEntry ) );
    SW_EXPECT_FALSE( pBatch->isEntryVisible( sw::HPBarBaseComponent::kBackgroundEntry ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, getEntrySpan( *pBatch, sw::HPBarBaseComponent::kFillEntry )._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, getEntrySpan( *pBatch, sw::HPBarBaseComponent::kFillEntry )._y, 1e-5f );
    const sw::float3 fillCenter = pBatch->getEntry( sw::HPBarBaseComponent::kFillEntry )._world.getTranslation();
    SW_EXPECT_NEAR_EQUAL( 3.8f, fillCenter._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, fillCenter._z, 1e-5f );

    // 맞았다(1 → 0.4): 채움은 그 틱에 바로 0.4, 흔적은 0.4 와 1 사이에 남는다.
    pBar->setTargetRatio( 0.4f );
    pBar->onTick( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pBar->getHpRatio(), 1e-6f );
    SW_EXPECT_TRUE( 0.4f < pBar->getRemainRatio() && pBar->getRemainRatio() < 1.0f );
    const sw::float2 fill  = getEntrySpan( *pBatch, sw::HPBarBaseComponent::kFillEntry );
    const sw::float2 trail = getEntrySpan( *pBatch, sw::HPBarBaseComponent::kTrailEntry );
    const sw::float2 back  = getEntrySpan( *pBatch, sw::HPBarBaseComponent::kBackgroundEntry );
    SW_EXPECT_TRUE( pBatch->isEntryVisible( sw::HPBarBaseComponent::kTrailEntry ) );
    SW_EXPECT_TRUE( pBatch->isEntryVisible( sw::HPBarBaseComponent::kBackgroundEntry ) );
    SW_EXPECT_NEAR_EQUAL( 0.4f, fill._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.5f + 0.2f, fill._x, 1e-5f ); // 왼쪽 끝 1.5 에서 시작한다
    // 조각은 맞닿고 겹치지 않으며 셋을 합치면 바 전체다.
    SW_EXPECT_NEAR_EQUAL( fill._x + fill._y * 0.5f, trail._x - trail._y * 0.5f, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( trail._x + trail._y * 0.5f, back._x - back._y * 0.5f, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, fill._y + trail._y + back._y, 1e-5f );
    // 색은 조각마다 다르고 인스턴스 칸에 실린다.
    SW_EXPECT_FALSE( pBatch->getEntry( sw::HPBarBaseComponent::kFillEntry )._sprite == pBatch->getEntry( sw::HPBarBaseComponent::kTrailEntry )._sprite );

    // 시간이 지나면 흔적이 채움까지 줄어 사라진다 — 바는 채움 0.4 와 바탕 0.6.
    for ( uint32 step = 0; step < 100; ++step )
        pBar->onTick( 0.1f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pBar->getRemainRatio(), 1e-4f );
    SW_EXPECT_FALSE( pBatch->isEntryVisible( sw::HPBarBaseComponent::kTrailEntry ) );
    SW_EXPECT_NEAR_EQUAL( 0.6f, getEntrySpan( *pBatch, sw::HPBarBaseComponent::kBackgroundEntry )._y, 1e-3f );

    // 회복(0.4 → 0.9)은 차오른다 — 흔적은 생기지 않는다.
    pBar->setTargetRatio( 0.9f );
    pBar->onTick( 0.1f );
    SW_EXPECT_TRUE( 0.4f < pBar->getHpRatio() && pBar->getHpRatio() < 0.9f );
    SW_EXPECT_NEAR_EQUAL( pBar->getHpRatio(), pBar->getRemainRatio(), 1e-6f );
    SW_EXPECT_FALSE( pBatch->isEntryVisible( sw::HPBarBaseComponent::kTrailEntry ) );

    // 숨기면 조각이 모두 숨고, 소유 오브젝트를 꺼도 숨는다.
    pBar->setVisible( false );
    SW_EXPECT_FALSE( pBatch->isVisible() );
    pBar->setVisible( true );
    SW_EXPECT_TRUE( pBatch->isVisible() );
    pHero->setActive( false );
    SW_EXPECT_FALSE( pBatch->isVisible() );

    // 끝나면 놓는다.
    pBar->dispatchEndPlay();
    SW_EXPECT_FALSE( pBar->getSpriteBatch().isInitialized() );
}

/**
 * @brief [WorldUiTest] 데미지 숫자는 값의 자릿수마다 글리프 아틀라스의 프레임을 보이고, 남는 자리는 숨기며, 알파는 수명에 따라 흐려진다
 * @details 예전 데미지 UI 는 수명 · 알파 · 떠오름만 계산하고 그리지 않았고 값을 넣는 세터도 없었다. 글리프 배치(칸 · 순서)는 생성 스크립트가 쓴 클립
 *          (`engine/textures/ui/digits.sprite.json`, 프레임 0..9 숫자 · 10 '-')에만 있고 컴포넌트는 프레임 번호만 안다. 자릿수가 바뀌어도 구조 변경
 *          없이 자리를 숨기고 보인다 — 틱 중에 값을 바꿔도 된다.
 */
SW_TEST_CASE( WorldUiTest, DamageNumberShowsItsDigitsFromTheGlyphAtlas )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::shared_ptr<const sw::SpriteClipAsset> glyphs = sw::SpriteClipCache::acquire( "engine/textures/ui/digits.sprite.json" );
    SW_ASSERT_NOT_NULL( glyphs.get() );
    SW_ASSERT_EQUAL( 11, glyphs->getFrameCount() );

    sw::GameObjectManager manager;
    sw::GameObject*       pHit = spawnAnchoredObject( manager, "Hit", sw::float3{ 0.0f, 1.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pHit );
    sw::DamageUIComponent* pDamage = pHit->addComponent<sw::DamageUIComponent>();
    SW_ASSERT_NOT_NULL( pDamage );
    SW_ASSERT_TRUE( setReflectedValue( pDamage, "lifeTime", 1.0f ) );
    pDamage->setDamageValue( 123 );
    pDamage->dispatchBeginPlay();

    const sw::MeshInstanceBatch* pBatch = pDamage->getSpriteBatch().getBatch();
    SW_ASSERT_NOT_NULL( pBatch );
    SW_ASSERT_EQUAL( sw::DamageUIComponent::kMaxGlyphCount, pBatch->getCount() );
    const int32 arrExpected[] = { 1, 2, 3 };
    for ( uint32 glyphIndex = 0; glyphIndex < 3; ++glyphIndex )
    {
        SW_EXPECT_TRUE( pBatch->isEntryVisible( glyphIndex ) );
        const sw::float4 shown = pBatch->getEntry( glyphIndex )._sprite.getUvRect();
        SW_EXPECT_NEAR_EQUAL( glyphs->findFrame( arrExpected[glyphIndex] )->_uvRect._x, shown._x, 1e-4f );
        SW_EXPECT_NEAR_EQUAL( glyphs->findFrame( arrExpected[glyphIndex] )->_uvRect._z, shown._z, 1e-4f );
    }
    for ( uint32 glyphIndex = 3; glyphIndex < sw::DamageUIComponent::kMaxGlyphCount; ++glyphIndex )
        SW_EXPECT_FALSE( pBatch->isEntryVisible( glyphIndex ) );
    // 가운데 정렬: 글자 폭 0.3 이라 -0.3 · 0 · 0.3, 높이는 소유자 자리.
    SW_EXPECT_NEAR_EQUAL( -0.3f, pBatch->getEntry( 0 )._world.getTranslation()._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.3f, pBatch->getEntry( 2 )._world.getTranslation()._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pBatch->getEntry( 1 )._world.getTranslation()._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pBatch->getEntry( 0 )._sprite.getTint()._w, 1.0f / 255.0f );

    // 반쯤 살면 알파가 반이다.
    pDamage->onTick( 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pDamage->getAlpha(), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pBatch->getEntry( 0 )._sprite.getTint()._w, 1.0f / 255.0f );

    // 음수 — '-' 프레임과 한 자리, 나머지는 숨는다(구조 변경 없음).
    pDamage->setDamageValue( -7 );
    SW_EXPECT_TRUE( pBatch->isEntryVisible( 0 ) );
    SW_EXPECT_TRUE( pBatch->isEntryVisible( 1 ) );
    SW_EXPECT_FALSE( pBatch->isEntryVisible( 2 ) );
    SW_EXPECT_NEAR_EQUAL( glyphs->findFrame( sw::DamageUIComponent::kMinusGlyphFrame )->_uvRect._x, pBatch->getEntry( 0 )._sprite.getUvRect()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( glyphs->findFrame( 7 )->_uvRect._x, pBatch->getEntry( 1 )._sprite.getUvRect()._x, 1e-4f );

    // 가장 긴 값(INT32_MIN)도 부호를 뒤집다 넘치지 않고 열한 글자다.
    int32        arrFrame[sw::DamageUIComponent::kMaxGlyphCount] = {};
    const uint32 glyphCount                                      = sw::DamageUIComponent::makeGlyphFrames( std::numeric_limits<int32>::min(), arrFrame );
    SW_ASSERT_EQUAL( 11u, glyphCount );
    const int32 arrMinDigits[] = { sw::DamageUIComponent::kMinusGlyphFrame, 2, 1, 4, 7, 4, 8, 3, 6, 4, 8 };
    for ( uint32 glyphIndex = 0; glyphIndex < glyphCount; ++glyphIndex )
        SW_EXPECT_EQUAL( arrMinDigits[glyphIndex], arrFrame[glyphIndex] );
    SW_EXPECT_EQUAL( 1u, sw::DamageUIComponent::makeGlyphFrames( 0, arrFrame ) );
    SW_EXPECT_EQUAL( 0, arrFrame[0] );

    // 수명이 다하면 오브젝트를 지운다.
    pDamage->onTick( 0.6f );
    SW_EXPECT_TRUE( pHit->isPendingDestroy() );
}

/**
 * @brief [WorldUiTest] 이펙트의 흐림은 같은 오브젝트 스프라이트들의 색 알파에 곱해진다 — 시작할 때의 알파가 기준이다
 * @details 예전에는 `_currentAlpha` 를 계산만 하고 읽는 곳이 없었다(`getCurrentAlpha` 호출 0). 반투명으로 만든 스프라이트(알파 0.8)는 0.8 에서 0 으로
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
    sw::EffectBaseComponent* pEffect = pSpark->addComponent<sw::EffectBaseComponent>();
    SW_ASSERT_NOT_NULL( pEffect );
    SW_ASSERT_TRUE( setReflectedValue( pEffect, "duration", 1.0f ) );
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
