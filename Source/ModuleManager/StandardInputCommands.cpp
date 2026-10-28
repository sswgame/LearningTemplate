#include "pch.h"

#include "ModuleManager/StandardInputCommands.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/StandardInputReader.h"

namespace sw
{
    SW_LOG_CALLER( "StandardInputCommands" );

    StandardInputCommands::StandardInputCommands()
        : _reader{ nullptr }
        , _listLine{}
    {
    }

    StandardInputCommands::~StandardInputCommands() = default;

    void StandardInputCommands::start()
    {
        if ( _reader != nullptr )
            return;
        _reader = make_unique<StandardInputReader>();
        _reader->start();
    }

    void StandardInputCommands::stop()
    {
        _reader.reset(); // 읽기 스레드를 멈추고 기다린다
        vector<string>{}.swap( _listLine );
    }

    bool StandardInputCommands::poll( const StandardInputCommandDelegate& onCommand )
    {
        if ( _reader == nullptr )
            return false;
        _reader->drainLines( _listLine );
        bool bQuitRequested = false;
        for ( const string& line : _listLine )
        {
            const string_view command = StringUtil::trim( line );
            if ( command == "quit" || command == "stop" )
            {
                SW_LOG_INFO( "Quit requested on standard input" );
                bQuitRequested = true;
                break;
            }
            if ( command.empty() == false && onCommand.isBound() )
                onCommand( command );
        }
        _listLine.clear();
        return bQuitRequested;
    }
} // namespace sw
