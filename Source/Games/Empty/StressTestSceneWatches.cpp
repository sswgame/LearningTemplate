/**
 * @file StressTestSceneWatches.cpp
 * @brief 벤치 감시 값입니다. 측정 구간 안의 정해진 프레임에 한 번 일어나는 일(새 한글 글리프 수백 개, 비동기 씬 로드)과
 *        월드 공간 UI 대량(RTS 이름과 HP 막대)을 켜고, 그 비용을 프로파일 목록(`GT.Frame` 의 max와 p99, `GT.UI.*`, `RT.Canvas.*`)에서 읽을 수 있게 합니다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/WidgetLayoutSlot.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UI/World/WidgetComponent.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "Games/Empty/StressTestScene.h"

namespace sw
{
    /**
     * @brief `-gv_stressTestUiNameplates=K`: 앞쪽 벤치 큐브 K 개에 RTS 이름(이름 한 줄과 HP 막대)를 붙입니다.
     * @details 월드 공간 UI 를 대량으로 그리는 비용을 측정합니다. 마커 투영과 그리기 목록 만들기는 게임 스레드에서, 캔버스 업로드와 그리기는 렌더 스레드에서 합니다.
     *          기준은 RTS 유닛 2,000개입니다. 게임 스레드 `GT.UI.*` 의 합이 0.5ms 를 넘으면 투영, 컬링, 간접 드로우를 컴퓨트로 옮길지 결정합니다.
     *          HP 는 큐브마다 다른 고정값입니다. 바뀌지 않는 트리 출력을 재사용하는 방식이 어디까지 적용되는지도 함께 봅니다.
     *          `-gv_stressTestUiMarkers` 와 함께 주면 이름이 우선합니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_stressTestUiNameplates, 0, "앞쪽 벤치 큐브 K 개에 RTS 이름표(이름 글 + HP 막대)를 붙인다 (0=사용 안 함)" );

    /**
     * @brief `-gv_stressTestUiHangulGlyphs=N`: `gv_stressTestWatchFrame` 프레임에 서로 다른 한글 음절 N 개로 된 긴 글을 화면에 띄웁니다.
     * @details 새 글리프 수백 개를 처음 그리는 프레임(래스터화, SDF, 아틀라스 업로드)의 최악 시간을 측정합니다. 프로파일 목록의 `GT.Frame` max 와 `RT.Canvas.Upload` max 를 봅니다.
     *          히치가 4ms 를 넘으면 SDF 를 컴퓨트(점프 플러딩)로 만들지 결정합니다.
     *          `-lang=ko` 로 실행해야 합니다. 영어 문화권의 대체 글꼴 체인에는 한글 글꼴이 없어서 글자가 두부(□)로만 그려지고, 그러면 측정이 되지 않습니다. 시스템에도 한글 글꼴이 있어야 합니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_stressTestUiHangulGlyphs, 0, "감시 값 프레임에 서로 다른 한글 음절 N 개의 긴 글을 띄운다(새 글리프 첫 프레임 측정, 0=사용 안 함)" );

    /**
     * @brief `-gv_stressTestSceneLoadPath=<리소스 경로>`: `gv_stressTestWatchFrame` 프레임에 이 씬을 비동기로 엽니다.
     * @details 워커가 파싱하고 오브젝트를 생성하는 동안과, 활성 씬을 바꾸는 프레임의 최악 시간을 측정합니다(`GT.Frame` 의 p99와 max). 로드 자체에 걸린 시간은
     *          로그의 `'Scene.load.parse'` 와 `'Scene.load.instantiate'` 에 나옵니다. 쿠킹한 결과를 측정하려면 `--cooked-dir` 로 쿠킹 폴더를 마운트합니다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_stressTestSceneLoadPath, "", "감시 값 프레임에 이 씬을 비동기로 연다(로드 중 최악 프레임 측정, 비우면 사용 안 함)" );

    /**
     * @brief `-gv_stressTestWatchFrame=F`: 한 번 일어나는 감시 값(한글 글, 씬 로드)을 거는 벤치 프레임입니다.
     * @details 프로파일 측정 창은 워밍업(60 프레임) 뒤에 시작하므로 그보다 뒤여야 표에 잡힌다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_stressTestWatchFrame, 180, "한 번 일어나는 벤치 감시 값(한글 글 · 씬 로드)을 거는 프레임(측정 창 워밍업 60 뒤)" );
} // namespace sw

namespace sw
{
    namespace
    {
        struct StressTestSceneWatchesInternal
        {
            static constexpr uint32 kHangulFirst = 0xAC00u; ///< '가'
            static constexpr uint32 kHangulCount = 11172u;  ///< 현대 한글 음절 수(U+AC00..U+D7A3)
            /// 음절을 고르는 보폭이다. 음절 수와 서로소라 N 개가 모두 다르고, 받침 · 모음이 고루 섞인다.
            static constexpr uint32 kHangulStride = 37u;
            /// 몇 음절마다 띄어 쓸지다. 줄 바꿈이 단어 경계에서 일어나게 한다.
            static constexpr uint32 kHangulWordLength = 6u;

            static void setFixedSize( Widget& widget, float32 width, float32 height )
            {
                WidgetLayoutSlot slot = widget.getLayoutSlot();
                slot._widthOverride   = width;
                slot._heightOverride  = height;
                widget.setLayoutSlot( slot );
            }

            /** @brief 서로 다른 한글 음절 @p count 개로 된 글입니다(같은 실행이면 늘 같은 글). */
            static string makeHangulText( uint32 count )
            {
                string text;
                text.reserve( static_cast<size_t>( count ) * 4u );
                for ( uint32 index = 0; index < count; ++index )
                {
                    if ( index > 0 && ( index % kHangulWordLength ) == 0 )
                        text.push_back( ' ' );
                    StringUtil::appendUTF8( text, kHangulFirst + ( index * kHangulStride ) % kHangulCount );
                }
                return text;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool StressTestScene::attachNameplate( GameObject& object, uint32 index ) const
    {
        if ( gv_stressTestUiNameplates <= 0 || index >= static_cast<uint32>( gv_stressTestUiNameplates ) )
            return false;
        WidgetComponent* pPlate = object.addComponent<WidgetComponent>();
        if ( pPlate == nullptr )
            return false;
        unique_ptr<BoxPanel> column = make_unique<BoxPanel>();
        column->setOrientation( UIOrientation::Vertical );
        column->setSpacing( _settings._nameplateSpacing );
        unique_ptr<TextWidget> name = make_unique<TextWidget>();
        name->setLocalized( false );
        name->setText( formatToString( "Unit %#", index ) );
        StressTestSceneWatchesInternal::setFixedSize( *name, _settings._nameplateWidth, _settings._nameplateTextHeight );
        (void)column->addChild( std::move( name ) );
        unique_ptr<ProgressBarWidget> health = make_unique<ProgressBarWidget>();
        health->setPercent( 0.2f + 0.8f * static_cast<float32>( ( index * 7u ) % 10u ) / 10.0f );
        health->setFillColor( _settings._nameplateFillColor );
        health->setBackgroundColor( _settings._nameplateBackgroundColor );
        StressTestSceneWatchesInternal::setFixedSize( *health, _settings._nameplateWidth, _settings._nameplateBarHeight );
        (void)column->addChild( std::move( health ) );
        pPlate->setSpace( WidgetSpace::Screen );
        pPlate->setWorldOffset( float3{ 0.0f, _settings._nameplateHeight, 0.0f } );
        pPlate->setContent( std::move( column ) );
        return true;
    }

    void StressTestScene::updateProbes( GameObjectManager* pObjects )
    {
        ++_probeFrame;
        if ( _probeFrame != static_cast<uint32>( MathUtil::max( gv_stressTestWatchFrame, 1 ) ) )
            return;

        if ( gv_stressTestUiHangulGlyphs > 0 && pObjects != nullptr )
        {
            // 화면 마커는 주 씬 컴포넌트의 월드 점을 투영한다. 없으면 마커가 접혀 글리프를 하나도 만들지 않는다(격자 가운데인 원점에 둔다).
            GameObject*      pObject = pObjects->createGameObject( hashed_string( "StressTestHangulText" ) );
            SceneComponent*  pAnchor = ( pObject != nullptr ) ? pObject->addComponent<SceneComponent>() : nullptr;
            WidgetComponent* pText   = ( pAnchor != nullptr ) ? pObject->addComponent<WidgetComponent>() : nullptr;
            if ( pText != nullptr )
            {
                unique_ptr<TextWidget> text = make_unique<TextWidget>();
                text->setLocalized( false );
                text->setText( StressTestSceneWatchesInternal::makeHangulText( static_cast<uint32>( gv_stressTestUiHangulGlyphs ) ) );
                StressTestSceneWatchesInternal::setFixedSize( *text, _settings._hangulTextWidth, 0.0f ); // 높이는 줄 바꿈한 글이 정한다
                pText->setSpace( WidgetSpace::Screen );
                pText->setContent( std::move( text ) );
                _listBenchExtra.push_back( pText->getHandle() );
                SW_LOG_INFO( "[Bench] probe frame %#: %# Hangul syllables on screen", _probeFrame, gv_stressTestUiHangulGlyphs );
            }
        }

        if ( gv_stressTestSceneLoadPath.empty() == false )
        {
            SceneManager*               pSceneManager = game::findSubsystem<SceneManager>();
            [[maybe_unused]] const bool bRequested    = pSceneManager != nullptr && pSceneManager->requestLoadAsync( gv_stressTestSceneLoadPath, SceneLoadKind::Travel );
            SW_LOG_INFO( "[Bench] probe frame %#: async scene load '%#' %#", _probeFrame, gv_stressTestSceneLoadPath.c_str(), bRequested ? "requested" : "failed" );
        }
    }
} // namespace sw
