#include "pch.h"

#include "ModuleManager/DeviceChangeSequence.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Foundation/QuitRequest.h"
#include "Engine/Presentation/IEngineRenderDriver.h"

namespace sw
{
    bool DeviceChangeSequence::apply( IEngineRenderDriver& renderDriver, QuitRequest& quitRequest ) const
    {
        if ( renderDriver.hasPendingBackendChange() == false )
            return true;

        // 엔진 몫(`Backend swap took`)과 모듈 몫(게임 · 에디터를 다시 세우고 씬 상태를 되돌린다)을 함께 남긴다 - 교체 시간의 대부분이 모듈 몫이던 때 엔진 줄만 보였다.
        constexpr uint64 kMicrosPerMilli = static_cast<uint64>( constant::time::kMicrosecondsPerMillisecond );
        const Stopwatch  swapWatch;
        _suspendModules();
        const uint64 suspendMicroseconds = swapWatch.getElapsedMicroseconds();

        const BackendChangeResult result             = renderDriver.applyPendingBackendChange();
        const uint64              engineMicroseconds = swapWatch.getElapsedMicroseconds();
        if ( result == BackendChangeResult::Failed )
        {
            SW_LOG_ERROR_ID( "DeviceChangeSequence.NoUsableDevice", "The device change left no usable device - the modules are not recreated; quitting with exit code %#",
                             static_cast<int32>( ProcessExitCode::DeviceLost ) );
            quitRequest.request( ProcessExitCode::DeviceLost );
            return false;
        }

        const bool   bModulesRecreated = _recreateModules();
        const uint64 totalMicroseconds = swapWatch.getElapsedMicroseconds();
        SW_LOG_INFO_ID( "DeviceChangeSequence.Took", "Backend swap with modules took %#ms (suspend modules %#ms, engine %#ms, recreate modules %#ms)", totalMicroseconds / kMicrosPerMilli,
                        suspendMicroseconds / kMicrosPerMilli, ( engineMicroseconds - suspendMicroseconds ) / kMicrosPerMilli, ( totalMicroseconds - engineMicroseconds ) / kMicrosPerMilli );
        if ( bModulesRecreated == false )
        {
            // 장치는 섰지만 게임(이나 에디터) 인스턴스가 없다 — 인스턴스 없이 도는 화면은 쓸 수 없다(필수 값이 없으면 멈춘다).
            SW_LOG_ERROR_ID( "DeviceChangeSequence.ModulesNotRecreated", "The modules could not be recreated on the new device - quitting with exit code %#", static_cast<int32>( ProcessExitCode::DeviceLost ) );
            quitRequest.request( ProcessExitCode::DeviceLost );
            return false;
        }
        if ( result == BackendChangeResult::Restored )
        {
            SW_LOG_ERROR_ID( "DeviceChangeSequence.RestoredPreviousBackend", "The requested backend could not be created - restored the previous backend and recreated the modules." );
            return false;
        }
        return true;
    }
} // namespace sw
