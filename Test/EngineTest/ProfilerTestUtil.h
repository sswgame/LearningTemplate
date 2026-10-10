/**
 * @file ProfilerTestUtil.h
 * @brief 외부 프로파일러 출력을 흉내 내는 기록용 출력입니다(`ProfilerBackendTest` · `GPUTimelineExporterTest`).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Profiling/IProfilerBackend.h"

namespace test
{
    /**
     * @class RecordingProfilerBackend
     * @brief 받은 호출을 순서대로 적습니다. 구간 · 프레임 · 그래프는 이름을, GPU 구간은 "+이름@시각" · "-@시각" 으로 적습니다.
     * @details 시험 중에도 다른 스레드(워커 · 로거)가 활성 출력을 지날 수 있으므로 기록은 잠금으로 지키고, 시험은 자기가 낸 이름만 찾습니다.
     */
    class RecordingProfilerBackend final : public sw::IProfilerBackend
    {
    public:
        const utf8* getBackendName() const override { return "Recording"; }
        bool        isViewerConnected() const override { return false; }

        uint64 beginZone( const sw::ProfileZoneSite& site ) override
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            _listEvent.push_back( sw::string( "begin " ) + site._pName );
            // 토큰은 이름 표의 번호다 — 닫을 때 어느 구간인지 이름으로 적는다(다른 스레드의 구간과 섞여도 시험이 제 것을 찾는다).
            _listZoneName.push_back( site._pName );
            return _listZoneName.size() - 1;
        }

        void endZone( uint64 zoneToken ) override
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            const sw::string            name = zoneToken < _listZoneName.size() ? _listZoneName[zoneToken] : sw::string( "?" );
            _listEvent.push_back( sw::string( "end " ) + name );
        }

        void markFrame( const utf8* pFrameName ) override
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            _listEvent.push_back( sw::string( "frame " ) + ( pFrameName != nullptr ? pFrameName : "main" ) );
        }

        void plotValue( const utf8* pPlotName, float64 value ) override
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            _listEvent.push_back( sw::string( "plot " ) + pPlotName + "=" + sw::to_string( static_cast<int64>( value ) ) );
        }

        void onAllocate( const void*, size_t, const utf8* ) override {}
        void onFree( const void*, const utf8* ) override {}

        uint32 createGPUContext( sw::ProfilerGPUBackend, const utf8* pName, int64 gpuNanos ) override
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            _listEvent.push_back( sw::string( "context " ) + ( pName != nullptr ? pName : "" ) + "@" + sw::to_string( gpuNanos ) );
            return _gpuContextCount++;
        }

        void syncGPUClock( uint32, int64 gpuNanos ) override
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            _listEvent.push_back( sw::string( "sync@" ) + sw::to_string( gpuNanos ) );
        }

        void beginGPUZone( uint32, const sw::ProfileZoneSite& site, int64 gpuBeginNanos ) override
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            _listEvent.push_back( sw::string( "+" ) + site._pName + "@" + sw::to_string( gpuBeginNanos ) );
        }

        void endGPUZone( uint32, int64 gpuEndNanos ) override
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            _listEvent.push_back( sw::string( "-@" ) + sw::to_string( gpuEndNanos ) );
        }

        /** @brief 지금까지의 기록 사본입니다. */
        sw::vector<sw::string> copyEvents() const
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            return _listEvent;
        }

        /** @brief @p text 와 같은 기록의 수입니다. */
        uint32 countEvent( const sw::string& text ) const
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            uint32                      count{ 0 };
            for ( const sw::string& event : _listEvent )
            {
                if ( event == text )
                    ++count;
            }
            return count;
        }

    private:
        mutable sw::mutex      _mutex;
        sw::vector<sw::string> _listEvent;
        sw::vector<sw::string> _listZoneName;
        uint32                 _gpuContextCount{ 0 };
    };
} // namespace test
