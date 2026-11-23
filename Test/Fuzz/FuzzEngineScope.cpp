/**
 * @file FuzzEngineScope.cpp
 * @brief 퍼저 실행 파일의 엔진 범위 — `LLVMFuzzerInitialize` 가 프로세스에 한 번 엔진(`DedicatedServer` 역할, 모듈 없음)을 세우고, 종료 때 역순으로 내립니다.
 * @details 단위 테스트 실행기(`App --test`)처럼 이름 풀 · 로거 · 타입 레지스트리 · 엔진 서비스가 선 상태에서 파서를 부른다 — 입력마다 세우면 exec/s 가 100 아래로 떨어진다
 *          (docs/NEXT.md 퍼저 타깃). 모양은 부하 봇(`Programs/OnlineLoadBot/Main.cpp`)의 호스트 범위와 같고, 게임 모듈은 올리지 않는다.
 *          타깃 파일은 `LLVMFuzzerTestOneInput` 하나만 정의한다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Engine.h"
#include "Engine/Module/ModuleTypeRegistry.h"

#include "ModuleManager/ModuleLoaderSubsystem.h"

namespace sw
{
    namespace
    {
        /** @class FuzzEngineScope @brief 호스트 범위와 엔진을 세우고 소멸자에서 내립니다. */
        class FuzzEngineScope
        {
        public:
            FuzzEngineScope()
                : _host{}
                , _engine{}
                , _listLoadedModule{}
                , _unloadSubscription{}
                , _bEngineStarted{ SW_FALSE }
            {
            }

            ~FuzzEngineScope()
            {
                if ( _bEngineStarted == SW_TRUE )
                    _engine.shutdown( _host );
                else
                    _host.destroyAll();
            }

            FuzzEngineScope( const FuzzEngineScope& )            = delete;
            FuzzEngineScope& operator=( const FuzzEngineScope& ) = delete;

            /** @brief 엔진을 세웁니다. 엔진 인자는 프로그램 이름뿐이다 — libFuzzer 인자(`-runs=` …)가 엔진 인자로 풀리지 않게. 서지 못하면 false 입니다. */
            [[nodiscard]] bool initialize( utf8* pProgramName )
            {
                ModuleLoaderSubsystem* pModuleLoader =
                    ModuleLoaderSubsystem::initializeHostScope( _host, SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &FuzzEngineScope::loadModules, this ), "Fuzz" );
                if ( pModuleLoader == nullptr )
                    return false;
                _unloadSubscription = pModuleLoader->bindScenesReleasedHook( SW_DELEGATE_METHOD( Delegate<void()>, &FuzzEngineScope::unregisterModules, this ) );
                utf8* arrArgument[] = { pProgramName, nullptr };
                _bEngineStarted     = SW_TRUE;
                return _engine.initialize( 1, arrArgument, EngineHostRole::DedicatedServer, &_host );
            }

        private:
            /**
             * @brief 타깃이 부르는 모듈을 의존 순서대로 올리고 타입을 등록합니다(`sw_add_fuzz_target( … MODULES )` 이 `SW_FUZZ_MODULES` 로 준다).
             * @details Core · Engine 의 파서만 부르는 타깃은 목록이 없다. GameFramework 는 정적 초기화에서 이름 풀을 써서 엔진 기동 뒤에만 올라올 수 있다 — 부하 봇과 같은 모양이다.
             */
            [[nodiscard]] bool loadModules()
            {
#if defined( SW_FUZZ_MODULES )
                static constexpr const utf8* kArrModule[] = { SW_FUZZ_MODULES };
                for ( const utf8* pModule : kArrModule )
                {
                    const optional<AbsolutePath> libraryPath = AbsolutePath::tryMake( ModuleLibraryUtil::findModuleLibraryPath( pModule ) );
                    if ( libraryPath.has_value() == false || ModuleLibraryUtil::loadLibraryAt( *libraryPath ) == nullptr )
                    {
                        SW_LOG_ERROR_ID( "Fuzz.CannotLoadModule", "Fuzz: cannot load the module %#", pModule );
                        return false;
                    }
                    _listLoadedModule.push_back( pModule );
                    if ( engine::registerModuleTypes( pModule ) == false )
                        return false;
                }
#endif
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

        private:
            SubsystemScope      _host;
            Engine              _engine;
            vector<const utf8*> _listLoadedModule;
            Subscription        _unloadSubscription;
            uint8               _bEngineStarted;
        };
    } // namespace
} // namespace sw

extern "C" int LLVMFuzzerInitialize( int* pArgc, char*** pArgv )
{
    // 프로세스 수명의 범위 — 정적 소멸이 libFuzzer 종료(exit) 뒤 엔진을 내려 LeakSanitizer 가 엔진 상태를 누수로 보지 않는다.
    static sw::FuzzEngineScope scope;
    if ( scope.initialize( *pArgc > 0 ? ( *pArgv )[0] : nullptr ) == false )
    {
        std::fprintf( stderr, "Fuzz: the engine did not start\n" );
        std::abort();
    }
    return 0;
}
