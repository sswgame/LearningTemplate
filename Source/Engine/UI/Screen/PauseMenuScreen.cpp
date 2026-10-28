#include "pch.h"

#include "Engine/UI/Screen/PauseMenuScreen.h"

#include "Engine/UI/Screen/OptionsMenuScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

namespace sw
{
    namespace
    {
        struct PauseMenuScreenInternal
        {
            /** @brief 전역 명령 표에 있을 때만 보이는 버튼입니다. 위젯 이름이 명령 이름과 같습니다. */
            static constexpr const utf8* kArrSessionButton[] = { UIGlobalCommandRegistry::kReturnToTitle, UIGlobalCommandRegistry::kQuitGame };
        };
    } // namespace
} // namespace sw

namespace sw
{
    PauseMenuScreen::PauseMenuScreen( const UIScreenDesc& desc, unique_ptr<Widget> root )
        : UIScreen{ desc, std::move( root ) }
    {
    }

    PauseMenuScreen::~PauseMenuScreen() = default;

    bool PauseMenuScreen::onCommand( const hashed_string& command, Widget& source )
    {
        if ( command == hashed_string( "Resume" ) )
        {
            close();
            return true;
        }
        if ( command == hashed_string( "OpenOptions" ) )
        {
            if ( UISystem* pUI = getUISystem(); pUI != nullptr )
                (void)OptionsMenuScreen::open( *pUI ); // 핸들은 쓰지 않는다. 열지 못하면 open 이 오류를 남긴다
            return true;
        }
        // 세션을 떠나는 명령은 메뉴를 닫고 전역 명령 표에 넘긴다 — 타이틀 씬이나 종료 뒤에 정지 메뉴(와 그 게임 시간 정지)가 남지 않게 한다.
        for ( const utf8* pSessionCommand : PauseMenuScreenInternal::kArrSessionButton )
        {
            if ( command != hashed_string( pSessionCommand ) )
                continue;
            UISystem* pUI = getUISystem();
            if ( pUI == nullptr || pUI->getGlobalCommands().hasCommand( command ) == false )
                return false; // 처리하는 쪽이 없다 — 화면 디스패치가 경고한다
            close();
            return pUI->getGlobalCommands().tryExecute( command, source );
        }
        return UIScreen::onCommand( command, source );
    }

    void PauseMenuScreen::onTick( float32 deltaSeconds )
    {
        UIScreen::onTick( deltaSeconds );
        const UISystem* pUI = getUISystem();
        if ( pUI == nullptr )
            return;
        for ( const utf8* pButtonName : PauseMenuScreenInternal::kArrSessionButton )
        {
            Widget* pButton = getTree().findWidgetByName( hashed_string( pButtonName ) );
            if ( pButton != nullptr )
                pButton->setVisibility( pUI->getGlobalCommands().hasCommand( hashed_string( pButtonName ) ) ? WidgetVisibility::Visible : WidgetVisibility::Collapsed );
        }
    }
} // namespace sw
