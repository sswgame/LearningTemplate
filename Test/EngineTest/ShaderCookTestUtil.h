/**
 * @file ShaderCookTestUtil.h
 * @brief 셰이더 쿠킹 스탬프 · 도메인 테스트가 함께 쓰는 우선순위입니다(`ShaderCookStampTest` · `ShaderCookerTest` · `ShaderDomainTest`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace test
{
    /**
     * @brief 실제 설정과 같은 리소스 우선순위입니다(`Config/Engine/EngineConfig.json` 의 `_listResourcePriority`). 도메인 종류만 다르게 만들 때 이 목록을 고쳐 쓴다.
     * @details 스탬프를 쓰고 읽는 테스트는 이 목록을 **직접 넘긴다**. `ResourcePaths::getSearchPriority()` 는 프로세스 전역이라 같은 프로세스의 앞선 케이스가
     *          바꿔 두면(비면) 스탬프를 쓰지 않거나 헤더 판정이 달라져, 병렬 실행에서 한 번씩 지는 간헐 실패가 되었다.
     */
    inline sw::vector<sw::string> makeShaderConfigPriority()
    {
        return { "game", "common", "engine", "editor" };
    }
} // namespace test
