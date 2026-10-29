#include "pch.h"

#include "Engine/Renderer/Frame/Resource/MaterialPSOBuilder.h"

#include "Core/Concurrency/ScopedLock.h"
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
        if ( _worker.isRunning() == false )
            _worker.start( "MaterialPSOBuilder", DedicatedWorker<uint8>::ProcessDelegate::create<&MaterialPSOBuilder::buildOneOnWorker>( this ) );
        // 작업 하나에 신호 하나다. 그 사이 `buildOnCaller` 가 먼저 꺼내 갔으면 신호는 할 일 없이 끝난다.
        [[maybe_unused]] const bool bAccepted = _worker.submit( uint8{ 0 } );
        SW_ASSERT( bAccepted ); // 방금 띄웠거나 돌고 있다 — `shutdown` 과 같은 스레드(렌더 스레드)가 부른다
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

    void MaterialPSOBuilder::buildOneOnWorker( [[maybe_unused]] uint8& signal, const bool bStopping )
    {
        if ( bStopping )
            return;
        Job         job;
        IRHIDevice* pDevice{ nullptr };
        if ( popJob( job, pDevice ) == false )
            return;
        const RHIPipelineStateHandle handle = pDevice->getResourceFactory()->createPipelineState( job._desc );
        pushResult( std::move( job ), handle );
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
        // 남은 신호는 멈춤으로 처리되어 만들지 않고, 만드는 중인 작업은 끝까지 간다(디바이스가 아직 살아 있다).
        _worker.stop();
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
