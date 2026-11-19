#include "pch.h"

#include "Engine/UI/Menu/MainMenuScreen.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/LocText.h"
#include "Engine/UI/Menu/OptionsMenuScreen.h"
#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

namespace sw
{
    MainMenuScreen::MainMenuScreen( const UIScreenDef& desc, unique_ptr<Widget> root )
        : UIScreen{ desc, std::move( root ) }
    {
    }

    MainMenuScreen::~MainMenuScreen() = default;

    bool MainMenuScreen::onCommand( const hashed_string& command, Widget& source )
    {
        UISystem* pUI = getUISystem();
        if ( pUI == nullptr )
            return false; // 화면 스택 밖의 화면이다 — 화면 디스패치가 경고한다
        if ( command == hashed_string( kOpenOptionsCommand ) )
        {
            (void)OptionsMenuScreen::open( *pUI ); // 핸들은 쓰지 않는다. 열지 못하면 open 이 오류를 남긴다
            return true;
        }
        if ( command == hashed_string( UIGlobalCommandRegistry::kQuitGame ) )
        {
            ConfirmDialogDesc desc{};
            // 키를 넘긴다 — 바인딩이 지금 문화권으로 푼다(원문은 engine.strings.xml).
            desc._title      = SW_LOCKEYREF( "UIMenu", "QuitTitle" );
            desc._message    = SW_LOCKEYREF( "UIMenu", "QuitMessage" );
            desc._acceptText = SW_LOCKEYREF( "UIMenu", "QuitAccept" );
            desc._cancelText = SW_LOCKEYREF( "UIMenu", "Cancel" );
            // 메뉴 포인터 대신 핸들을 쥔다 — 답을 받기 전에 씬이 바뀌어 메뉴가 닫혔으면 아무것도 하지 않는다.
            const UIScreenHandle menu = getHandle();
            (void)ConfirmDialogScreen::open( *pUI, desc, ConfirmDialogScreen::ResultDelegate::create( [pUI, menu]( ConfirmDialogResult result )
            { onQuitConfirmed( *pUI, menu, result ); } ) );
            return true;
        }
        if ( command == hashed_string( UIGlobalCommandRegistry::kStartGame ) )
        {
            // 처리 함수가 없으면 false — 화면 디스패치가 전역 표에서 다시 찾고 경고한다. 메뉴는 닫지 않는다: 새 씬이 서면 타이틀 씬의 컴포넌트가 닫는다.
            const UIGlobalCommandRegistry& commands = engine::getSubsystem<UIGlobalCommandRegistry>();
            return commands.tryExecute( command, source );
        }
        return UIScreen::onCommand( command, source );
    }

    void MainMenuScreen::onTick( float32 deltaSeconds )
    {
        UIScreen::onTick( deltaSeconds );
        if ( getUISystem() == nullptr )
            return;
        static const hashed_string s_quitName( UIGlobalCommandRegistry::kQuitGame );
        Widget*                    pButton = getTree().findWidgetByName( s_quitName );
        if ( pButton != nullptr )
            pButton->setVisibility( engine::getSubsystem<UIGlobalCommandRegistry>().hasCommand( s_quitName ) ? WidgetVisibility::Visible : WidgetVisibility::Collapsed );
    }

    void MainMenuScreen::onQuitConfirmed( UISystem& ui, UIScreenHandle menu, ConfirmDialogResult result )
    {
        UIScreen* pMenu = ui.findScreen( menu );
        if ( result != ConfirmDialogResult::Accepted || pMenu == nullptr || pMenu->isClosing() )
            return;
        static const hashed_string s_quitName( UIGlobalCommandRegistry::kQuitGame );
        Widget*                    pButton = pMenu->getTree().findWidgetByName( s_quitName );
        if ( pButton != nullptr )
            (void)engine::getSubsystem<UIGlobalCommandRegistry>().tryExecute( s_quitName, *pButton ); // 표에 없으면 단추가 접혀 여기 오지 않는다
    }
} // namespace sw
