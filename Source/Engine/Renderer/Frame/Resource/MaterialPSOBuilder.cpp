#include "pch.h"

#include "Engine/Renderer/Frame/Resource/MaterialPSOBuilder.h"

#include "Core/Concurrency/Futex.h"
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    MaterialPSOBuilder::~MaterialPSOBuilder()
    {
        shutdown( nullptr );
    }

    void MaterialPSOBuilder::submit( IRHIDevice* pDevice, Job&& job, bool bUrgent, bool bThread )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            _pDevice = pDevice;
            ( bUrgent ? _listUrgent : _listLow ).push_back( std::move( job ) );
        }
        if ( bThread == false )
            return;
        if ( _thread.joinable() == false )
            _thread = sw::thread( &MaterialPSOBuilder::run, this );
        _wakeWord.fetch_add( 1, std::memory_order_acq_rel );
        Futex::wakeOne( _wakeWord );
    }

    bool MaterialPSOBuilder::popJob( Job& outJob, IRHIDevice*& outDevice )
    {
        ScopedLock<mutex> lock{ _mutex };
        vector<Job>&      list = _listUrgent.empty() ? _listLow : _listUrgent;
        if ( list.empty() || _pDevice == nullptr )
            return false;
        outJob    = std::move( list.front() );
        outDevice = _pDevice;
        list.erase( list.begin() );
        ++_inFlight;
        return true;
    }

    void MaterialPSOBuilder::pushResult( Job&& job, RHIPipelineStateHandle handle )
    {
        ScopedLock<mutex> lock{ _mutex };
        _listResult.push_back( Result{ job._key, std::move( job._desc ), handle } );
        --_inFlight;
    }

    void MaterialPSOBuilder::run( ThreadStopToken stopToken )
    {
        ThreadName::setCurrentThreadName( "MaterialPSOBuilder" );
        while ( stopToken.stop_requested() == false )
        {
            const uint32 seenWake = _wakeWord.load( std::memory_order_acquire );
            Job          job;
            IRHIDevice*  pDevice{ nullptr };
            if ( popJob( job, pDevice ) == false )
            {
                Futex::wait( _wakeWord, seenWake ); // 그 사이 넣었으면 워드가 바뀌어 바로 돌아온다
                continue;
            }
            const RHIPipelineStateHandle handle = pDevice->getResourceFactory()->createPipelineState( job._desc );
            pushResult( std::move( job ), handle );
        }
    }

    void MaterialPSOBuilder::takeResults( vector<Result>& outListResult )
    {
        ScopedLock<mutex> lock{ _mutex };
        outListResult.clear();
        outListResult.swap( _listResult );
    }

    uint32 MaterialPSOBuilder::buildOnCaller( uint32 budgetMicroseconds )
    {
        const int64 startNanoseconds  = MonotonicClock::nowNanoseconds();
        const int64 budgetNanoseconds = static_cast<int64>( budgetMicroseconds ) * 1000;
        uint32      builtCount{ 0 };
        Job         job;
        IRHIDevice* pDevice{ nullptr };
        while ( popJob( job, pDevice ) )
        {
            const RHIPipelineStateHandle handle = pDevice->getResourceFactory()->createPipelineState( job._desc );
            pushResult( std::move( job ), handle );
            ++builtCount;
            if ( budgetMicroseconds != 0 && MonotonicClock::nowNanoseconds() - startNanoseconds >= budgetNanoseconds )
                break;
        }
        return builtCount;
    }

    uint32 MaterialPSOBuilder::getPendingCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return static_cast<uint32>( _listUrgent.size() + _listLow.size() ) + _inFlight;
    }

    void MaterialPSOBuilder::shutdown( IRHIDevice* pDevice )
    {
        if ( _thread.joinable() )
        {
            _thread.request_stop();
            _wakeWord.fetch_add( 1, std::memory_order_acq_rel );
            Futex::wakeAll( _wakeWord );
            _thread.join();
        }
        vector<Result> listResult;
        {
            ScopedLock<mutex> lock{ _mutex };
            _listUrgent.clear();
            _listLow.clear();
            listResult.swap( _listResult );
            _pDevice = nullptr;
        }
        for ( const Result& result : listResult )
        {
            if ( result._handle != 0 && pDevice != nullptr && pDevice->getResourceFactory() != nullptr )
                pDevice->getResourceFactory()->destroyPipelineState( result._handle );
        }
    }
} // namespace sw
