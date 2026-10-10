#include "pch.h"

#include "Engine/UI/Screen/PauseMenuScreen.h"

#include "Engine/UI/Screen/OptionsMenuScreen.h"
#include "Engine/UI/UISystem.h"

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
                (void)OptionsMenuScreen::open( *pUI ); // 핸들은 쓰지 않는다 — 열지 못하면 open 이 오류를 남긴다
            return true;
        }
        return UIScreen::onCommand( command, source );
    }
} // namespace sw
