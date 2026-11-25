#include "pch.h"

#include "Engine/Graphics/RHI/Backend/RHIPipelineCacheFile.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Foundation/EngineSubsystems.h"

namespace sw
{
    string RHIPipelineCacheFile::makeRelativePath( string_view prefix, uint32 vendorID, uint32 deviceID, uint64 driverVersion )
    {
        StringBuilder<constant::buffer::kMax128> path;
        path.appendFormat( "ShaderCache/%#_%08x_%08x_%016x.bin", prefix, vendorID, deviceID, driverVersion );
        return string{ path.c_str() };
    }

    RHIPipelineCacheFile::RHIPipelineCacheFile()
        : _path{}
        , _serialize{}
        , _saveIntervalNanoseconds{ 0 }
        , _lastSaveNanoseconds{ 0 }
        , _lastSavedBytes{ 0 }
        , _saveStage{}
    {
    }

    void RHIPipelineCacheFile::open( const AbsolutePath& path, float64 saveIntervalSeconds, RHIPipelineCacheSerializeDelegate serialize, vector<uint8>& outBytes )
    {
        SW_ASSERT( saveIntervalSeconds > 0.0 && serialize.isBound() );
        _path                    = path;
        _serialize               = std::move( serialize );
        _saveIntervalNanoseconds = MonotonicClock::secondsToNanoseconds( saveIntervalSeconds );
        outBytes.clear();
        if ( FileUtil::exists( _path ) && FileUtil::readFile( _path, outBytes ) == false )
            outBytes.clear(); // 못 읽으면 빈 캐시로 시작한다(이유는 readFile 이 남겼다)
        _lastSaveNanoseconds = MonotonicClock::nowNanoseconds();
        _lastSavedBytes      = static_cast<uint64>( outBytes.size() );
    }

    bool RHIPipelineCacheFile::isIntervalPassed() const
    {
        return isOpen() && engine::areEngineSubsystemsBound() && MonotonicClock::nowNanoseconds() - _lastSaveNanoseconds >= _saveIntervalNanoseconds;
    }

    void RHIPipelineCacheFile::saveIfDue( uint64 cacheBytes )
    {
        if ( isIntervalPassed() == false )
            return;
        const int64 nowNanoseconds = MonotonicClock::nowNanoseconds();
        if ( isPeriodicSaveDue( nowNanoseconds, _lastSaveNanoseconds, _saveIntervalNanoseconds, cacheBytes, _lastSavedBytes ) )
        {
            TaskManager& taskManager = engine::getSubsystem<TaskManager>();
            if ( taskManager.isStageComplete( _saveStage ) == false )
                return; // 앞 저장이 아직 쓰는 중이다(느린 디스크) — 시각을 옮기지 않아 다음 프레임에 다시 본다
            TaskHandle saveTask = taskManager.emplaceTask( "RHIPipelineCacheSave", SW_DELEGATE_METHOD( TaskDelegate, &RHIPipelineCacheFile::write, this ) );
            taskManager.addToStage( _saveStage, saveTask, "RHIPipelineCacheSave" );
            saveTask.submit();
            _lastSavedBytes = cacheBytes;
        }
        _lastSaveNanoseconds = nowNanoseconds;
    }

    void RHIPipelineCacheFile::saveAndClose()
    {
        if ( isOpen() == false )
            return;
        if ( engine::areEngineSubsystemsBound() )
            engine::getSubsystem<TaskManager>().waitStage( _saveStage, "RHIPipelineCacheFile::saveAndClose" );
        write();
        _serialize = RHIPipelineCacheSerializeDelegate{};
    }

    void RHIPipelineCacheFile::write()
    {
        // 캐시를 내지 못하면(오류는 백엔드가 남겼다) 저장만 건너뛴다 — 다음 실행이 파이프라인을 새로 만들 뿐이다.
        vector<uint8> listCacheData;
        if ( _serialize( listCacheData ) == false || listCacheData.empty() )
            return;
        // 새 파이프라인이 없던 실행은 같은 바이트다 — 다시 쓰지 않는다. 드라이버가 같은 내용에 다른 바이트를 내면 쓴다.
        vector<uint8> listSavedData;
        const bool    bUnchanged = FileUtil::exists( _path ) && FileUtil::readFile( _path, listSavedData ) && listSavedData.size() == listCacheData.size() &&
                                Memory::compare( listSavedData.data(), listCacheData.data(), listCacheData.size() ) == 0;
        if ( bUnchanged )
            SW_LOG_INFO_ID( "RHI.PipelineCacheUnchanged", "Pipeline cache unchanged (%# bytes) - not saved.", listCacheData.size() );
        else if ( FileUtil::ensureParentDirectoryExists( _path ) && FileUtil::writeFile( _path, listCacheData.data(), static_cast<uint64>( listCacheData.size() ) ) )
            SW_LOG_INFO_ID( "RHI.SavedPipelineCacheBytes", "Saved pipeline cache (%# bytes).", listCacheData.size() );
        else
            SW_LOG_WARNING_ID( "RHI.CouldNotSavePipelineCache", "Could not save the pipeline cache - the next run builds pipelines from scratch" );
    }
} // namespace sw
