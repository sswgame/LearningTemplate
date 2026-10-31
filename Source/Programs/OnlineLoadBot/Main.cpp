/**
 * @file Main.cpp
 * @brief 온라인 부하 테스트 봇의 실행 파일 — 엔진(`DedicatedServer`)을 세우고, GameFramework · 키트 · 봇 모듈(`OnlineLoadBotCore`)을 이름으로 올려 봇 진입점을 부릅니다.
 * @details - 인자는 대시 수로 가른다: `-이름`(대시 하나 — `-log-file=<path>` · `-gv_…`)은 엔진 인자로 엔진이 읽고(모르면 기동 오류), `--이름`(대시 둘)은 봇 인자로
 *            봇 모듈이 읽는다(`LoadBotMain.cpp`). 종료 코드는 `LoadBotModuleEntry` 의 것이고, 엔진 · 모듈이 서지 못하면 3 이다.
 *          - 이 실행 파일은 Engine · ModuleManager 만 링크한다. GameFramework · 키트 DLL 은 정적 초기화에서 이름 풀(`hashed_string`)을 쓰므로, 실행 파일이 직접 링크해
 *            main 전에 올라오면 DLL 초기화가 실패하고(0xC0000142) 로그 한 줄 없이 죽는다(Windows 는 오류 대화상자를 띄워 CTest 가 시간 초과까지 기다렸다).
 *            App · Server 처럼 엔진 기동 단계 `ModuleTypes` 에서 의존 순서대로 올리고, 올릴 때마다 그 모듈 이름으로 타입을 등록한다.
 *          - 프로세스 바닥은 전용 서버(`ServerApp`)와 같은 모양이다 — 호스트 범위(모듈 라이브러리 창구) 위에 `Engine`(`DedicatedServer`)을 세운다. 서버 키트가 쓰는 엔진 서비스와
 *            타입 레지스트리(설정의 범위 검사 · 리플렉션)가 이 엔진에 있다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Engine.h"
#include "Engine/Module/ModuleTypeRegistry.h"

#include "ModuleManager/ModuleLoaderSubsystem.h"

#include "Programs/OnlineLoadBot/LoadBotModuleEntry.h"

SW_LOG_CALLER( "OnlineLoadBot" );
namespace sw
{
    namespace
    {
        /**
         * @brief 프로세스 바닥 — 전용 서버(`ServerApp`)와 같은 모양의 호스트 범위와 `Engine`(`DedicatedServer`)을 세우고 역순으로 내립니다.
         * @details 호스트 범위(모듈 라이브러리 창구)가 엔진 범위의 부모이고, 엔진이 부트스트랩(이름 풀 · 로거 · 크래시 핸들러) · 서비스 · 기동 단계를 세운다.
         *          엔진 기동 단계 `ModuleTypes` 가 창구의 로더(`loadModules`)를 불러 봇이 쓰는 모듈을 올리고 그 타입을 등록한다.
         *          main 의 맨 처음에 두어 hashed_string 을 든 객체가 모두 사라진 뒤에 이름 풀이 내려가게 한다(로거는 비동기라 내리지 않으면 마지막 줄을 잃는다).
         *          게임 모듈 · 서버 설정 · 서버 서비스는 세우지 않는다 — 봇은 서버 조립(`LoadBotLocalServer`)을 스스로 연다.
         */
        class ToolHostScope
        {
        public:
            ToolHostScope()
                : _host{}
                , _engine{}
                , _listLoadedModule{}
                , _unloadSubscription{}
                , _pBotModule{ nullptr }
                , _bEngineStarted{ SW_FALSE }
            {
            }

            ~ToolHostScope()
            {
                // `Engine::shutdown` 은 일부만 서도 된다. 엔진 범위가 사라진 뒤 부트스트랩(종료 누수 보고)보다 먼저 호스트 범위를 내린다.
                if ( _bEngineStarted == SW_TRUE )
                    _engine.shutdown( _host );
                else
                    _host.destroyAll();
            }

            ToolHostScope( const ToolHostScope& )            = delete;
            ToolHostScope& operator=( const ToolHostScope& ) = delete;

            /**
             * @brief 호스트 범위와 엔진을 세웁니다. 엔진의 `ModuleTypes` 단계에서 봇이 쓰는 모듈을 모두 올립니다.
             * @details 엔진에는 프로그램 이름과 엔진 인자(@p listEngineArgument, 대시 하나)만 넘긴다 — 봇 인자(`--scenario=` …)가 엔진의 표준 인자
             *          (`-scenario` 자동화 시나리오 …)로 풀리지 않게 한다. 로거가 서기 전의 실패는 표준 오류로 알린다.
             * @return 서지 못하면 false 입니다(폴백 없음 — 호출자가 멈춘다).
             */
            [[nodiscard]] bool initialize( vector<utf8*>& listEngineArgument )
            {
                ModuleLoaderSubsystem* pModuleLoader =
                    ModuleLoaderSubsystem::initializeHostScope( _host, SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &ToolHostScope::loadModules, this ), "OnlineLoadBot" );
                if ( pModuleLoader == nullptr )
                {
                    std::fprintf( stderr, "OnlineLoadBot: the host scope did not start\n" );
                    return false;
                }
                // 엔진 종료의 모듈 내리기 자리(`ModuleUnloadStage`)에서 등록을 역순으로 거둔다 — 모듈 서브시스템(GameFramework · 키트의 캐시)이 엔진보다 먼저 내려간다.
                _unloadSubscription             = pModuleLoader->bindScenesReleasedHook( SW_DELEGATE_METHOD( Delegate<void()>, &ToolHostScope::unregisterModules, this ) );
                const int32 engineArgumentCount = static_cast<int32>( listEngineArgument.size() );
                listEngineArgument.push_back( nullptr );
                _bEngineStarted = SW_TRUE;
                if ( _engine.initialize( engineArgumentCount, listEngineArgument.data(), EngineHostRole::DedicatedServer, &_host ) == false )
                {
                    std::fprintf( stderr, "OnlineLoadBot: the engine did not start (dedicated server role)\n" );
                    return false;
                }
                return true;
            }

            /** @brief 봇 모듈의 진입점을 봇 인자(@p listBotArgument, 맨 앞은 프로그램 이름)로 부릅니다(`initialize` 뒤에만). */
            [[nodiscard]] int32 runBot( vector<utf8*>& listBotArgument )
            {
                const auto pfnRun = reinterpret_cast<PFN_RunOnlineLoadBot>( ModuleLibraryUtil::getDynamicSymbol( _pBotModule, LoadBotModuleEntry::kEntrySymbol ) );
                if ( pfnRun == nullptr )
                {
                    SW_LOG_ERROR( "OnlineLoadBot: module %# does not export %#", LoadBotModuleEntry::kModuleName, LoadBotModuleEntry::kEntrySymbol );
                    return LoadBotModuleEntry::kExitStartup;
                }
                const int32 botArgumentCount = static_cast<int32>( listBotArgument.size() );
                listBotArgument.push_back( nullptr );
                return pfnRun( botArgumentCount, listBotArgument.data(), _engine.getConfigManager() );
            }

        private:
            /**
             * @brief 봇이 쓰는 모듈 — 의존이 먼저다(GameFramework → 키트, 런타임 키트 → 같은 기능의 서버 키트 → 봇 모듈). 목록은 CMake 가 준다(`SW_LOADBOT_MODULES`).
             * @details 서버 키트는 `--local-server` 가 아니어도 올린다 — 인자는 봇 모듈이 엔진 기동 뒤에 읽고, 올려 두기만 한 서버 키트는 하는 일이 없다.
             */
            static constexpr const utf8* kArrModule[] = { SW_LOADBOT_MODULES };

            /**
             * @brief 모듈을 하나씩 올려 정적 등록자가 서게 하고, 올릴 때마다 그 모듈 이름으로 리플렉션 타입을 등록합니다(전역 등록자 머리가 그 모듈 것만 남게).
             * @details 하나라도 못 올리면 false 입니다(기동 멈춤 — `ModuleLibraryUtil::loadLibraryAt` 이 Win32 오류를 남긴다). 등록은 엔진 종료 때 `unregisterModules` 가 거둔다.
             */
            [[nodiscard]] bool loadModules()
            {
                for ( const utf8* pModule : kArrModule )
                {
                    const std::optional<AbsolutePath> libraryPath = AbsolutePath::tryMake( ModuleLibraryUtil::findModuleLibraryPath( pModule ) );
                    void* const                       pHandle     = libraryPath.has_value() ? ModuleLibraryUtil::loadLibraryAt( *libraryPath ) : nullptr;
                    if ( pHandle == nullptr )
                    {
                        SW_LOG_ERROR( "OnlineLoadBot: cannot load the module %#", pModule );
                        return false;
                    }
                    _listLoadedModule.push_back( pModule ); // 올렸으면 등록이 일부 실패해도 내릴 때 거둔다
                    if ( engine::registerModuleTypes( pModule ) == false )
                        return false;
                    if ( string_view{ pModule } == LoadBotModuleEntry::kModuleName )
                        _pBotModule = pHandle;
                }
                if ( _pBotModule == nullptr )
                {
                    SW_LOG_ERROR( "OnlineLoadBot: the module list has no %#", LoadBotModuleEntry::kModuleName );
                    return false;
                }
                return true;
            }

            /** @brief 올린 모듈의 등록을 역순으로 거둡니다. 라이브러리는 프로세스가 끝날 때까지 둔다(정적 소멸자가 그 코드를 부른다). */
            void unregisterModules()
            {
                for ( auto moduleIt = _listLoadedModule.rbegin(); moduleIt != _listLoadedModule.rend(); ++moduleIt )
                {
                    engine::unregisterModuleTypes( *moduleIt );
                }
                _listLoadedModule.clear();
            }

            SubsystemScope      _host;
            Engine              _engine;
            vector<const utf8*> _listLoadedModule;
            Subscription        _unloadSubscription;
            void*               _pBotModule;
            uint8               _bEngineStarted;
        };
    } // namespace
} // namespace sw

int32 main( int32 argc, utf8* argv[] )
{
    using namespace sw;
    ToolHostScope hostScope;
    // 대시 하나는 엔진 인자, 대시 둘은 봇 인자다. 둘 다 맨 앞에 프로그램 이름을 둔다.
    vector<utf8*> listEngineArgument{ argv[0] };
    vector<utf8*> listBotArgument{ argv[0] };
    for ( int32 argumentIndex = 1; argumentIndex < argc; ++argumentIndex )
    {
        const string_view argument{ argv[argumentIndex] };
        const bool        bBotArgument = argument.size() >= 2 && argument[0] == '-' && argument[1] == '-';
        ( bBotArgument ? listBotArgument : listEngineArgument ).push_back( argv[argumentIndex] );
    }
    if ( hostScope.initialize( listEngineArgument ) == false )
        return LoadBotModuleEntry::kExitStartup;
    return hostScope.runBot( listBotArgument );
}
