/**
 * @file Engine/Module/ModuleLoader.h
 * @brief 호스트가 엔진에 내주는 모듈 이미지 창구의 인터페이스입니다 — 엔진의 `ModuleLoadStage` · `ModuleUnloadStage` 기동 단계가 호스트 범위에서 찾아 부릅니다.
 * @details 호스트(App · Server)는 엔진 범위의 부모인 호스트 범위에 이 인터페이스를 구현한 서브시스템을 걸어 둡니다(`ModuleLoaderSubsystem`).
 *          엔진은 모듈 DLL 을 어떻게 올리고 내리는지 모르고, 호스트가 있으면 부르고 없으면(테스트 하네스 · 정적 링크 구성) 건너뜁니다.
 *          부르는 쪽은 `locateSubsystem<IModuleLoader>` 로 찾는 읽기 전용 조회입니다.
 */
#pragma once

namespace sw
{
    /**
     * @class IModuleLoader
     * @brief 타입 공급자 모듈을 올리고, 씬이 사라진 뒤 모듈 DLL 을 내리는 호스트의 창구입니다.
     */
    class IModuleLoader
    {
    public:
        virtual ~IModuleLoader() = default;

        /**
         * @brief 타입 공급자 모듈(GameFramework · 키트 · 게임 모듈)을 올려 그 타입을 등록합니다. 실패하면 false 입니다.
         * @details 기동 단계 `ModuleLoadStage` 가 부릅니다. 올릴 것이 없으면 true 입니다(배포 구성은 정적 링크라 리플렉션 단계가 이미 다 모았다).
         */
        [[nodiscard]] virtual bool loadTypeProviders() = 0;
        /**
         * @brief 씬이 모두 사라졌고 엔진 서비스는 아직 살아 있는 지점에 불립니다 — 모듈 DLL 을 내리는 유일하게 알맞은 자리입니다.
         * @details 기동 단계 `ModuleUnloadStage` 의 종료가 부릅니다(엔진 범위 종료의 두 패스 사이). 더 일찍이면 씬이 든 컴포넌트 팩토리 델리게이트가
         *          사라진 코드를 가리키고, 더 늦으면 언로드가 쓰는 서비스와 로거가 이미 없습니다.
         */
        virtual void onScenesReleased() = 0;
    };
} // namespace sw
