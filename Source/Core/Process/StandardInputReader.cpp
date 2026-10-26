#include "pch.h"

#include "Core/Process/StandardInputReader.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/thread.h"

namespace sw
{
    StandardInputReader::StandardInputReader()
        : _thread{}
        , _mutex{}
        , _listLine{}
        , _partialLine{}
        , _stopRequested{ 0 }
        , _endOfInput{ 0 }
    {
    }

    StandardInputReader::~StandardInputReader()
    {
        stop();
    }

    void StandardInputReader::start()
    {
        if ( _thread.joinable() )
            return;
        _stopRequested.store( 0 );
        _thread = sw::thread( [this]()
        { readLoop(); } );
    }

    void StandardInputReader::stop()
    {
        _stopRequested.store( 1 );
        if ( _thread.joinable() )
            _thread.join();
        // 버퍼도 놓는다 — 엔진 종료의 누수 보고(기동 뒤 기준선)보다 먼저 멈추는 쪽이 읽고 남긴 용량을 늘어난 메모리로 찍지 않게.
        vector<string>{}.swap( _listLine );
        string{}.swap( _partialLine );
    }

    void StandardInputReader::drainLines( vector<string>& outListLine )
    {
        ScopedLock<mutex> lock{ _mutex };
        for ( string& line : _listLine )
        {
            outListLine.push_back( std::move( line ) );
        }
        _listLine.clear();
    }

    void StandardInputReader::readLoop()
    {
        ThreadName::setCurrentThreadName( "StandardInput" );
        string line;
        while ( _stopRequested.load() == 0 )
        {
            if ( readLinePlatform( line ) == false )
            {
                if ( _endOfInput.load() != 0 )
                    return; // 입력이 닫혔다 — 프로세스는 계속 돈다(신호 · 서비스 정지 · 다른 길로 끝낸다)
                continue;
            }
            ScopedLock<mutex> lock{ _mutex };
            _listLine.push_back( line );
        }
    }

    bool StandardInputReader::takeLine( string& outLine )
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
