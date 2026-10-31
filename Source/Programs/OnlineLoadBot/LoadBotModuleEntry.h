/**
 * @file LoadBotModuleEntry.h
 * @brief 부하 봇 모듈(`OnlineLoadBotCore`)과 그것을 올리는 실행 파일(`OnlineLoadBot`)의 약속 — 모듈 이름 · 진입점 심볼 · 진입점 형식 · 종료 코드.
 * @details 실행 파일은 Engine · ModuleManager 만 링크하고, 엔진(이름 풀 · 타입 레지스트리)을 세운 뒤 GameFramework · 키트 · 이 모듈을 이름으로 올려 진입점을 부른다.
 *          GameFramework · 키트 DLL 의 정적 초기화는 이름 풀(`hashed_string`)을 쓰므로 엔진보다 먼저 올라오면(실행 파일이 직접 링크하면) DLL 초기화가 실패해
 *          프로세스가 main 전에 죽는다(0xC0000142) — App · Server 가 모듈을 기동 뒤에 올리는 것과 같은 모양으로 둔다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class ConfigManager;

    struct LoadBotModuleEntry
    {
        static constexpr const utf8* kModuleName  = "OnlineLoadBotCore";
        static constexpr const utf8* kEntrySymbol = "swRunOnlineLoadBot";

        static constexpr int32 kExitOk        = 0;
        static constexpr int32 kExitErrorRate = 1;
        static constexpr int32 kExitUsage     = 2;
        static constexpr int32 kExitStartup   = 3; ///< 엔진 · 모듈이 서지 못했다(실행 파일이 낸다)
        static constexpr int32 kExitReport    = 4; ///< 보고서(`--report`)를 쓰지 못했다 — 결과를 잃은 실행은 성공이 아니다
    };

    /** @brief 봇 진입점 — 봇 인자 전부(@p argv[0] 은 프로그램)와 엔진의 설정 표를 받아 종료 코드를 돌려준다. 엔진이 선 뒤에만 부른다. */
    using PFN_RunOnlineLoadBot = int32 ( * )( int32 argc, utf8** argv, ConfigManager* pConfigManager );
} // namespace sw
