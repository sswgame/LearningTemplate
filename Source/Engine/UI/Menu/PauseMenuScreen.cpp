#include "pch.h"

#include "Engine/UI/Menu/PauseMenuScreen.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/UI/Menu/OptionsMenuScreen.h"
#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"
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
    PauseMenuScreen::PauseMenuScreen( const UIScreenDef& desc, unique_ptr<Widget> root )
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
            if ( getUISystem() == nullptr )
                return false; // 화면 스택 밖의 화면이다 — 화면 디스패치가 경고한다
            UIGlobalCommandRegistry& commands = engine::getSubsystem<UIGlobalCommandRegistry>();
            if ( commands.hasCommand( command ) == false )
                return false; // 처리하는 쪽이 없다 — 화면 디스패치가 경고한다
            close();
            return commands.tryExecute( command, source );
        }
        return UIScreen::onCommand( command, source );
    }

    void PauseMenuScreen::onTick( float32 deltaSeconds )
    {
        UIScreen::onTick( deltaSeconds );
        if ( getUISystem() == nullptr )
            return;
        const UIGlobalCommandRegistry& commands          = engine::getSubsystem<UIGlobalCommandRegistry>();
        static const hashed_string     s_arrButtonName[] = { hashed_string( PauseMenuScreenInternal::kArrSessionButton[0] ), hashed_string( PauseMenuScreenInternal::kArrSessionButton[1] ) };
        static_assert( SW_COUNT_OF( s_arrButtonName ) == SW_COUNT_OF( PauseMenuScreenInternal::kArrSessionButton ) );
        for ( const hashed_string& buttonName : s_arrButtonName )
        {
            Widget* pButton = getTree().findWidgetByName( buttonName );
            if ( pButton != nullptr )
                pButton->setVisibility( commands.hasCommand( buttonName ) ? WidgetVisibility::Visible : WidgetVisibility::Collapsed );
        }
    }
} // namespace sw
