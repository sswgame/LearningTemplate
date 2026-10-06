#include "pch.h"

#include "Server/ServerConsole.h"

#include "Core/Concurrency/ThreadName.h"

namespace sw
{
    ServerConsole::ServerConsole()
        : _thread{}
        , _mutex{}
        , _listLine{}
        , _partialLine{}
        , _stopRequested{ 0 }
        , _endOfInput{ 0 }
    {
    }

    ServerConsole::~ServerConsole()
    {
        stop();
    }

    void ServerConsole::start()
    {
        if ( _thread.joinable() )
            return;
        _stopRequested.store( 0 );
        _thread = std::thread( [this]()
        { readLoop(); } );
    }

    void ServerConsole::stop()
    {
        _stopRequested.store( 1 );
        if ( _thread.joinable() )
            _thread.join();
    }

    void ServerConsole::drainLines( vector<string>& outListLine )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        for ( string& line : _listLine )
            outListLine.push_back( std::move( line ) );
        _listLine.clear();
    }

    void ServerConsole::readLoop()
    {
        ThreadName::setCurrentThreadName( "ServerConsole" );
        string line;
        while ( _stopRequested.load() == 0 )
        {
            if ( readLinePlatform( line ) == false )
            {
                if ( _endOfInput.load() != 0 )
                    return; // 입력이 닫혔다 — 서버는 계속 돈다(신호 · 서비스 정지 · 콘솔 밖 명령으로 끝낸다)
                continue;
            }
            std::scoped_lock<mutex> lock{ _mutex };
            _listLine.push_back( line );
        }
    }

    bool ServerConsole::takeLine( string& outLine )
    {
        const size_t newline = _partialLine.find( '\n' );
        if ( newline == string::npos )
            return false;
        outLine = _partialLine.substr( 0, newline );
        _partialLine.erase( 0, newline + 1 );
        if ( outLine.empty() == false && outLine.back() == '\r' )
            outLine.pop_back();
        return true;
    }
} // namespace sw
