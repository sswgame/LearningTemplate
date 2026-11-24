/**
 * @file EngineScope.h
 * @brief 엔진 범위의 읽기 전용 로케이터입니다 — 어느 층에서 include 해도 되는 잎 헤더입니다.
 * @details 서비스 창구(`engine::getSubsystem<T>()`, `EngineServices.h`)는 엔진 · 앱 · 테스트 전용이고, 모듈은 자기 범위의 창구(`game::findSubsystem<T>()` ·
 *          `editor::findSubsystem<T>()`)를 쓴다. 그 창구가 호스트 범위를 받지 못한 실행(쿠커 · 테스트 하네스)에서 엔진 범위로 내려갈 때 이 함수만 본다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    enum class GameInstanceBoundary : uint8; // `Core/Subsystem/Subsystem.h`

    class ISubsystemLocator;

    namespace engine
    {
        /** @brief 바인딩한 엔진 범위의 로케이터입니다(씬 범위의 부모로도 넘긴다). 바인딩 전이면 nullptr 입니다. */
        SW_API const ISubsystemLocator* getEngineSubsystems();
        /**
         * @brief @p typeHash 가 `HostOnly` 로 등록한 엔진 서비스면 true 입니다. 게임 창구가 호스트 범위 없이 엔진 범위로 내려갈 때 그 칸을 거른다 —
         *        묶인 게임 모듈이 못 찾는 서비스를 묶이지 않은 실행(테스트 · 단계 사이)에서만 찾아 결함이 가려지지 않게(`game::findSubsystem`).
         */
        SW_API bool isHostOnlyService( uint64 typeHash );
        /**
         * @brief 게임 인스턴스의 수명 경계를 엔진 · 모듈 서브시스템에 알립니다 — `SubsystemLifetime::GameInstance` 로 등록한 것이 상태를 비운다. 게임 스레드에서 부릅니다.
         * @details 게임 다시 시작(`GameModuleManager::restartGame`) · 타이틀 복귀(`GameSceneFlow`) · PIE 시작과 멈춤이 모두 이 함수 하나를 부른다. 자동화 단계는 게임 다시 시작이다.
         *          모듈 핫 리로드 · 장치 교체는 월드와 연결을 잇는 인스턴스 재생성이라 경계가 아니다(부르지 않는다). 엔진이 묶이지 않았으면 할 일이 없다.
         */
        SW_API void notifyGameInstanceBoundary( GameInstanceBoundary boundary );
    } // namespace engine
} // namespace sw
