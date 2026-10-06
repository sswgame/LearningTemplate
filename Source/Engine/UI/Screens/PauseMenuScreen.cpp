#include "pch.h"

#include "Engine/UI/Screens/PauseMenuScreen.h"

#include "Engine/UI/Screens/OptionsMenuScreen.h"
#include "Engine/UI/UiSystem.h"

namespace sw
{
    PauseMenuScreen::PauseMenuScreen( const UiScreenDesc& desc, unique_ptr<Widget> root )
        : UiScreen{ desc, std::move( root ) }
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
            if ( UiSystem* pUi = getUiSystem(); pUi != nullptr )
                (void)OptionsMenuScreen::open( *pUi ); // 핸들은 쓰지 않는다 — 열지 못하면 open 이 오류를 남긴다
            return true;
        }
        return UiScreen::onCommand( command, source );
    }
} // namespace sw
