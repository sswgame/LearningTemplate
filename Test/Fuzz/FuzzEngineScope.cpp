/**
 * @file FuzzEngineScope.cpp
 * @brief 퍼저 실행 파일의 엔진 범위 — `LLVMFuzzerInitialize` 가 프로세스에 한 번 엔진(`DedicatedServer` 역할, 모듈 없음)을 세우고, 종료 때 역순으로 내립니다.
 * @details 단위 테스트 실행기(`App --test`)처럼 이름 풀 · 로거 · 타입 레지스트리 · 엔진 서비스가 선 상태에서 파서를 부른다 — 입력마다 세우면 exec/s 가 100 아래로 떨어진다
 *          (docs/NEXT.md 퍼저 타깃). 모양은 부하 봇(`Programs/OnlineLoadBot/Main.cpp`)의 호스트 범위와 같고, 게임 모듈은 올리지 않는다.
 *          타깃 파일은 `LLVMFuzzerTestOneInput` 하나만 정의한다.
 */
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Engine.h"

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
                if ( ModuleLoaderSubsystem::initializeHostScope( _host, SW_DELEGATE_METHOD( ModuleTypeLoaderDelegate, &FuzzEngineScope::loadModules, this ), "Fuzz" ) == nullptr )
                    return false;
                utf8* arrArgument[] = { pProgramName, nullptr };
                _bEngineStarted     = SW_TRUE;
                return _engine.initialize( 1, arrArgument, EngineHostRole::DedicatedServer, &_host );
            }

        private:
            /** @brief 올릴 모듈이 없다 — 퍼저 타깃은 Core · Engine 의 파서만 부른다(GameFramework 타깃이 생기면 부하 봇처럼 이름으로 올린다). */
            [[nodiscard]] bool loadModules() { return true; }

        private:
            SubsystemScope _host;
            Engine         _engine;
            uint8          _bEngineStarted;
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
