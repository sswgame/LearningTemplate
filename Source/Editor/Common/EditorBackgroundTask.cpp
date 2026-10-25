#include "pch.h"

#include "Editor/Common/EditorBackgroundTask.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include <chrono>
#include <thread>

namespace sw::editor
{
    void EditorBackgroundJobTracker::track( const TaskHandle& handle )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        VectorUtil::removeIf( _listHandle, []( const TaskHandle& tracked )
        { return tracked.isCompleted(); } );
        _listHandle.push_back( handle );
    }

    uint32 EditorBackgroundJobTracker::getPendingCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        uint32                  count{ 0 };
        for ( const TaskHandle& tracked : _listHandle )
        {
            if ( tracked.isCompleted() == false )
                ++count;
        }
        return count;
    }

    bool EditorBackgroundJobTracker::waitIdle( uint32 timeoutMs )
    {
        constexpr uint32 kPollMilliseconds = 1; // 종료 때 한 번 도는 대기라 깨우기 신호를 따로 두지 않고 짧게 끊어 본다
        const Stopwatch  stopwatch;
        while ( getPendingCount() > 0 )
        {
            if ( stopwatch.getElapsedMilliseconds() >= static_cast<int64>( timeoutMs ) )
                return getPendingCount() == 0;
            std::this_thread::sleep_for( std::chrono::milliseconds( kPollMilliseconds ) );
        }
        return true;
    }

    void submitEditorBackgroundJob( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args )
    {
        TaskManager* pTaskManager = editor::findSubsystem<TaskManager>();
        if ( pTaskManager == nullptr )
        {
            delegate( args );
            return;
        }

        TaskHandle handle = pTaskManager->emplaceTask( name, delegate, args );
        // 추적기는 에디터 컨텍스트가 만든 상태 저장소에 산다. 컨텍스트가 없으면(테스트) 종료에서 기다릴 주인도 없으므로 올리지 않는다.
        if ( EditorStateStore* pStore = editor::findSubsystem<EditorStateStore>() )
            pStore->get<EditorBackgroundJobTracker>().track( handle );
        handle.submit();
    }
} // namespace sw::editor
