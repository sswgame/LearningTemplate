/**
 * @file EngineScope.h
 * @brief 엔진 범위의 읽기 전용 로케이터입니다 — 어느 층에서 include 해도 되는 잎 헤더입니다.
 * @details 서비스 창구(`engine::getSubsystem<T>()`, `EngineServices.h`)는 엔진 · 앱 · 테스트 전용이고, 모듈은 자기 범위의 창구(`game::findSubsystem<T>()` ·
 *          `editor::findSubsystem<T>()`)를 쓴다. 그 창구가 호스트 범위를 받지 못한 실행(쿠커 · 테스트 하네스)에서 엔진 범위로 내려갈 때 이 함수만 본다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    class ISubsystemLocator;

    namespace engine
    {
        /** @brief 바인딩한 엔진 범위의 로케이터입니다(씬 범위의 부모로도 넘긴다). 바인딩 전이면 nullptr 입니다. */
        SW_API const ISubsystemLocator* getEngineSubsystems();
    } // namespace engine
} // namespace sw
