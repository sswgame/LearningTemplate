/**
 * @file DynamicModules.h
 * @brief 빌드가 DLL 모듈을 올리고 내리는지의 스위치(`SW_WITH_DYNAMIC_MODULES`) 하나입니다. 무거운 `ModuleLibraryUtil.h` 없이 헤더가 볼 수 있게 따로 둔다.
 */
#pragma once

/**
 * @brief DLL 모듈을 올리고 내리는가입니다. 배포 빌드(`SW_SHIPPING`)는 모듈 없이 정적으로 한 실행 파일이라 0 이다(언리얼 `IS_MONOLITHIC`).
 * @details 0 이면 모듈 언로드 장치(언로드 · 섀도 복사 · 미룬 언로드)와 RHI 백엔드 DLL 로드가 빠진다 — 게임 기동에 쓰이지 않는 코드를
 *          짓지 않는다. 처리기 함수(`onModuleUnloading`)는 남지만 부르는 곳이 없어 링커가 걷어 낸다. 이 스위치를 부르는 코드가 남으면 배포 빌드가 컴파일에서 멈춘다.
 */
#if defined( SW_SHIPPING )
    #define SW_WITH_DYNAMIC_MODULES 0
#else
    #define SW_WITH_DYNAMIC_MODULES 1
#endif
