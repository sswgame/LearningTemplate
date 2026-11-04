/**
 * @file Engine/Foundation/EngineCoreServiceSubsystems.cpp
 * @brief Core 층 형식의 서브시스템(이벤트 · 압축 코덱 · 태스크 · 비동기 파일 IO) 등록입니다.
 * @details Core 는 엔진의 등록 틀(`EngineSubsystemRegistration.h`)을 볼 수 없으므로(층이 아래다) 그 형식의 등록은 엔진 바닥 층의 이 파일에 둔다.
 *          다른 서브시스템은 제 .cpp 에 등록한다.
 */
#include "pch.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    SW_ENGINE_SUBSYSTEM( EventDispatcher, []( SubsystemScope& scope, engine::EngineSubsystemBuild& build )
    {
        build._pServiceParts->_pEventDispatcher = &scope.emplaceSubsystem<EventDispatcher>();
    } );

    SW_ENGINE_SUBSYSTEM( CompressionCodecRegistry, []( SubsystemScope& scope, engine::EngineSubsystemBuild& build )
    {
        build._pStartupParts->_pCodecRegistry = &scope.emplaceSubsystem<CompressionCodecRegistry>();
    } );

    SW_ENGINE_SUBSYSTEM_DEFAULT( TaskManager, TaskManager );

    // 완료 콜백(팩 해제 · CRC · 스트리밍 완료 기록)은 태스크 워커에서 돈다 — IO 스레드는 다음 읽기를 거는 일만 한다. 태스크 매니저는 앞 자리다.
    SW_ENGINE_SUBSYSTEM( AsyncFileIO, []( SubsystemScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        AsyncFileIOConfig fileIOSettings{};
        fileIOSettings._pTaskManager = &scope.get<TaskManager>();
        scope.emplaceSubsystem<AsyncFileIO>().setSubsystemSettings( fileIOSettings );
    } );
} // namespace sw
