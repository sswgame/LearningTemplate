/**
 * @file Engine/CoreServices/EngineCoreServiceSubsystems.cpp
 * @brief Core 층 형식의 서브시스템(이벤트 · 압축 코덱 · 태스크 · 비동기 파일 IO) 등록입니다.
 * @details Core 는 엔진의 등록 틀(`EngineSubsystemRegistration.h`)을 볼 수 없으므로(층이 아래다) 그 형식의 등록은 Core 서비스의 서브시스템 곁(`Engine/CoreServices`)인 이 파일에 둔다.
 *          서비스 칸 선언(`SW_SERVICE_SLOT`)은 Core 헤더에 있고 칸 정의 · 등록이 여기 있다. 다른 서브시스템은 제 .cpp 에 등록한다.
 */
#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Compression/EngineCompressionCodecs.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    namespace
    {
        /** @brief 엔진 코덱(LZ4 · Zstd · Zlib)의 등록 구독을 듭니다. 레지스트리 바로 뒤에 서서 먼저 내려간다. */
        class EngineCompressionCodecs final : public ISubsystem
        {
        public:
            static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::EngineMisc; ///< 할당 용도(`ISubsystem::getSubsystemMemoryTag`)
            static constexpr const utf8* kSubsystemName      = "EngineCompressionCodecs";

            explicit EngineCompressionCodecs( CompressionCodecRegistry& registry )
                : _listSubscription{ EngineCompressionCodecUtil::registerAll( registry ) }
            {
            }

            SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
            void                shutdownSubsystem() override {}
            const utf8*         getSubsystemName() const override { return kSubsystemName; }

        private:
            vector<Subscription> _listSubscription;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( EventDispatcher, GameVisible, Services, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        build._pServiceParts->_pEventDispatcher = &scope.emplaceSubsystem<EventDispatcher>();
    } );

    SW_ENGINE_SERVICE_SUBSYSTEM( CompressionCodecRegistry, GameVisible, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        build._pStartupParts->_pCodecRegistry = &scope.emplaceSubsystem<CompressionCodecRegistry>();
        (void)scope.emplaceSubsystem<EngineCompressionCodecs>( *build._pStartupParts->_pCodecRegistry );
    } );

    // 워커 수는 명령줄 `-workers=N` 이 정한다(없으면 코어 수). 명령줄은 코어 범위에 있어 상위 범위로 올라가며 찾는다 — 공장 범위의 먼저 만들 타입이 아니다.
    SW_ENGINE_SERVICE_SUBSYSTEM( TaskManager, HostOnly, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        TaskManager&              taskManager = scope.emplaceSubsystem<TaskManager>();
        const CommandLineManager& commandLine = requireSubsystem<CommandLineManager>( scope.getScope() );
        int32                     workerCount = 0;
        if ( commandLine.getArgument( CommandLineArgument::WORKERS, workerCount ) == false )
            return;
        if ( workerCount < 1 )
            SW_FATAL( "-workers=%# must be 1 or more", workerCount );
        taskManager.setSubsystemWorkerCount( static_cast<uint32>( workerCount ) );
    } );

    // 완료 콜백(팩 해제 · CRC · 스트리밍 완료 기록)은 태스크 워커에서 돈다 — IO 스레드는 다음 읽기를 거는 일만 한다. 공장이 태스크 매니저를 찾으므로 그것을 먼저 만든다.
    SW_ENGINE_SERVICE_SUBSYSTEM( AsyncFileIO, HostOnly, Startup, ( TaskManager ), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        AsyncFileIOConfig fileIOSettings{};
        fileIOSettings._pTaskManager = &scope.get<TaskManager>();
        scope.emplaceSubsystem<AsyncFileIO>().setSubsystemSettings( fileIOSettings );
    } );
} // namespace sw
