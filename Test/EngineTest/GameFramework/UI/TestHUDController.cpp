#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Base/UI/HUD/HUDControllerComponent.h"

#include "TestFramework/TestFramework.h"

// HUDControllerTest — 오브젝트가 플레이하는 동안 HUD 문서를 HUD 층에 열고 HUD 뷰모델을 거는 컴포넌트. 문서는 메모리 문서로 넣는다. 디바이스 없음(nogpu).

namespace
{
    /**
     * @brief 손으로 지은 리플렉션 표를 가진 테스트 HUD 뷰모델입니다(테스트 타깃에 코드젠이 없다).
     * @details HUD 컨트롤러는 칸을 모른다 — 게임이 고른 뷰모델을 걸 뿐이다. 이 테스트의 칸(상태 글 · 수 · 비율)은 어느 게임의 것도 아니다.
     */
    class TestGameHUDViewModel : public sw::UIViewModel
    {
    public:
        TestGameHUDViewModel()
            : sw::UIViewModel{}
            , _status{}
            , _count{ 0 }
            , _ratio{ 0.0f }
        {
        }

        const sw::TypeInfo* getTypeInfo() const override { return &getStaticTypeInternal(); }

        void setStatus( const sw::string& status ) { (void)setField( _status, status, "_status" ); }
        void setCount( int32 count ) { (void)setField( _count, count, "_count" ); }
        void setRatio( float32 ratio ) { (void)setField( _ratio, ratio, "_ratio" ); }

        sw::string _status;
        int32      _count;
        float32    _ratio;

    private:
        static const sw::TypeInfo& getStaticTypeInternal()
        {
            static sw::TypeInfo s_info = makeTypeInternal();
            return s_info;
        }

        static sw::TypeInfo makeTypeInternal()
        {
            sw::TypeInfo info{};
            info._name               = sw::hashed_string( "TestGameHUDViewModel" );
            info._fullyQualifiedName = sw::hashed_string( "TestGameHUDViewModel" );
            info._listProperty.push_back( { sw::hashed_string( "_status" ), sw::hashed_string( "string" ), SW_OFFSET_OF( TestGameHUDViewModel, _status ) } );
            info._listProperty.push_back( { sw::hashed_string( "_count" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( TestGameHUDViewModel, _count ) } );
            info._listProperty.push_back( { sw::hashed_string( "_ratio" ), sw::hashed_string( "float32" ), SW_OFFSET_OF( TestGameHUDViewModel, _ratio ) } );
            // 기반을 적어 둔다 — `getOrCreateViewModel` 의 `castTo` 가 이 타입을 `UIViewModel` 의 자손으로 확인한다.
            info._parentFQN = sw::UIViewModel::StaticType()->_fullyQualifiedName;
            return info;
        }
    };
} // namespace

/**
 * @brief [HUDControllerTest] HUD 문서는 HUD 층 화면으로 열리고(포커스 · 게임 입력을 가져가지 않는다) 이름으로 위젯을 찾으며, UI 시스템을 풀면 닫힌다
 * @details `findWidget<T>( 이름 )` 은 바인딩으로 닿지 않는 일에 쓴다. 변이: `bindUISystem` 이 옛 화면을 닫지 않으면 마지막 단언(화면 0)이 진다.
 */
SW_TEST_CASE( HUDControllerTest, OpensTheHUDDocumentOnTheHUDLayer )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::UISystem ui;
    SW_ASSERT_TRUE( ui.initialize( input, nullptr ) );
    ui.getDocumentCache().registerMemoryDocument( "test/hud.ui.xml", "<UIDocument _schemaVersion=\"1\">\n"
                                                                     "\t<UIScreenDesc _layer=\"HUD\" _bTakesFocus=\"false\" _bShowCursor=\"false\" />\n"
                                                                     "\t<CanvasPanel>\n"
                                                                     "\t\t<TextWidget _name=\"Ammo\" />\n"
                                                                     "\t</CanvasPanel>\n"
                                                                     "</UIDocument>\n" );
    {
        sw::GameObjectManager manager;
        sw::GameObject*       pPlayer = manager.createGameObject( sw::hashed_string( "Player" ) );
        SW_ASSERT_NOT_NULL( pPlayer );
        sw::HUDControllerComponent* pHUD = pPlayer->addComponent<sw::HUDControllerComponent>();
        SW_ASSERT_NOT_NULL( pHUD );
        pHUD->setDocumentPath( "test/hud.ui.xml" );
        pHUD->bindUISystem( &ui );

        const sw::UIScreen* pScreen = pHUD->getScreen();
        SW_ASSERT_NOT_NULL( pScreen );
        SW_EXPECT_TRUE( pScreen->getDesc()._layer == sw::UILayer::HUD );
        SW_EXPECT_TRUE( ui.getActiveScreen() == nullptr ); // HUD 는 포커스를 받지 않는다
        SW_EXPECT_FALSE( ui.isGameInputBlocked() );
        sw::TextWidget* pAmmo = pHUD->findWidget<sw::TextWidget>( sw::hashed_string( "Ammo" ) );
        SW_ASSERT_NOT_NULL( pAmmo );
        pAmmo->setText( "30 / 90" );
        SW_EXPECT_TRUE( pHUD->findWidget<sw::TextWidget>( sw::hashed_string( "Missing" ) ) == nullptr );

        pHUD->bindUISystem( nullptr );
        SW_EXPECT_TRUE( pHUD->getScreen() == nullptr );
        ui.update( 1.0f / 60.0f, sw::UIViewport{
                                     sw::float2{ 1920.0f, 1080.0f }
        } );
        SW_EXPECT_EQUAL( 0u, ui.getScreenCount() );
    }
    ui.shutdown();
    input.shutdown();
}

/**
 * @brief [HUDControllerTest] 게임이 고른 뷰모델을 `getOrCreateViewModel<T>` 로 걸고 세터만 부르면 문서의 `{bind:필드}` 가 위젯 칸에 잇는다 —
 *        화면을 연 뒤에 걸어도 바로 잇고, 닫았다 다시 열어도 마지막 값이 바로 보인다
 * @details HUD 컨트롤러는 게임 중립이다 — 칸은 게임의 뷰모델이 가진다. 변이: `installViewModel` 이 떠 있는 화면에 걸지 않으면 첫 단언부터 진다.
 */
SW_TEST_CASE( HUDControllerTest, ViewModelDrivesTheHUDDocument )
{
    struct HUDTestUtil
    {
        static void runFrame( sw::InputManager& input, sw::UISystem& ui )
        {
            constexpr float32 kFrameSeconds = 1.0f / 60.0f;
            input.beginFrame( kFrameSeconds );
            ui.processInput( kFrameSeconds );
            ui.update( kFrameSeconds, sw::UIViewport{
                                          sw::float2{ 1920.0f, 1080.0f }
            } );
        }
    };
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::UISystem ui;
    SW_ASSERT_TRUE( ui.initialize( input, nullptr ) );
    ui.getDocumentCache().registerMemoryDocument( "test/boundhud.ui.xml",
                                                  "<UIDocument _schemaVersion=\"1\">\n"
                                                  "\t<UIScreenDesc _layer=\"HUD\" _bTakesFocus=\"false\" _bShowCursor=\"false\" />\n"
                                                  "\t<CanvasPanel>\n"
                                                  "\t\t<TextWidget _name=\"Status\" _text=\"{bind:_status}\" _bLocalized=\"false\" />\n"
                                                  "\t\t<TextWidget _name=\"Count\" _text=\"{bind:_count}\" _bLocalized=\"false\" />\n"
                                                  "\t\t<ProgressBarWidget _name=\"Bar\" _percent=\"{bind:_ratio}\" />\n"
                                                  "\t</CanvasPanel>\n"
                                                  "</UIDocument>\n" );
    {
        sw::GameObjectManager manager;
        sw::GameObject*       pPlayer = manager.createGameObject( sw::hashed_string( "Player" ) );
        SW_ASSERT_NOT_NULL( pPlayer );
        sw::HUDControllerComponent* pHUD = pPlayer->addComponent<sw::HUDControllerComponent>();
        SW_ASSERT_NOT_NULL( pHUD );
        pHUD->setDocumentPath( "test/boundhud.ui.xml" );
        pHUD->bindUISystem( &ui );
        SW_EXPECT_TRUE( pHUD->findViewModel() == nullptr ); // 게임이 걸기 전에는 뷰모델이 없다

        TestGameHUDViewModel* pViewModel = pHUD->getOrCreateViewModel<TestGameHUDViewModel>();
        SW_ASSERT_NOT_NULL( pViewModel );
        SW_EXPECT_TRUE( pHUD->getOrCreateViewModel<TestGameHUDViewModel>() == pViewModel ); // 두 번째는 같은 것을 준다
        pViewModel->setStatus( "Riding" );
        pViewModel->setCount( 12 );
        pViewModel->setRatio( 0.724f );
        HUDTestUtil::runFrame( input, ui );

        const auto findText = [pHUD]( const utf8* pName ) -> sw::string
        {
            const sw::TextWidget* pText = pHUD->findWidget<sw::TextWidget>( sw::hashed_string( pName ) );
            return pText != nullptr ? pText->getText() : sw::string( "<missing>" );
        };
        SW_EXPECT_STREQ( "Riding", findText( "Status" ).c_str() );
        SW_EXPECT_STREQ( "12", findText( "Count" ).c_str() );
        const sw::ProgressBarWidget* pBar = pHUD->findWidget<sw::ProgressBarWidget>( sw::hashed_string( "Bar" ) );
        SW_ASSERT_NOT_NULL( pBar );
        SW_EXPECT_NEAR_EQUAL( 0.724f, pBar->getPercent(), 1e-4f );

        pViewModel->setCount( 11 );
        HUDTestUtil::runFrame( input, ui );
        SW_EXPECT_STREQ( "11", findText( "Count" ).c_str() );

        // 닫았다 다시 열면 새 화면이 지금 값을 바로 받는다(뷰모델이 화면보다 오래 산다).
        pHUD->bindUISystem( nullptr );
        HUDTestUtil::runFrame( input, ui );
        SW_EXPECT_EQUAL( 0u, ui.getScreenCount() );
        pHUD->bindUISystem( &ui );
        HUDTestUtil::runFrame( input, ui );
        SW_EXPECT_STREQ( "11", findText( "Count" ).c_str() );
        SW_EXPECT_STREQ( "Riding", findText( "Status" ).c_str() );
        pHUD->bindUISystem( nullptr );
    }
    ui.shutdown();
    input.shutdown();
}
