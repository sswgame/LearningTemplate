/**
 * @file DynamicModules.h
 * @brief 빌드가 DLL 모듈을 올리고 내리는지의 스위치(`SW_WITH_DYNAMIC_MODULES`) 하나입니다. 무거운 `ModuleLibraryUtil.h` 없이 헤더가 볼 수 있게 따로 둔다.
 */
#pragma once

/**
 * @brief DLL 모듈을 올리고 내리는가입니다. 모듈을 정적으로 한 실행 파일에 링크하는 구성(`SW_STATIC_MODULES` — 배포 전부, 웹 · Android 개발 구성
 *        Web-Release · Android-Release)은 0 이다(언리얼 `IS_MONOLITHIC`). CMake 옵션 `SW_STATIC_MODULES` 가 정의한다(cmake/Modules/Build/BuildLayout.cmake).
 * @details 0 이면 모듈 언로드 장치(언로드 · 섀도 복사 · 미룬 언로드), RHI 백엔드 DLL 로드, 핫 리로드(`IHostDevLayer` 의 리로드 창구 · DevTools `ModuleReload/`),
 *          에디터 모듈 올리기가 빠진다. 쓰는 코드가 남으면 그 구성이 컴파일에서 멈춘다. 배포인지(`SW_SHIPPING`: 진단 · 개발 도구 · 테스트)와는 따로다.
 */
#if defined( SW_STATIC_MODULES )
    #define SW_WITH_DYNAMIC_MODULES 0
#else
    #define SW_WITH_DYNAMIC_MODULES 1
#endif

#if defined( SW_SHIPPING ) && SW_WITH_DYNAMIC_MODULES
    #error "A shipping build links its modules statically - SW_SHIPPING needs SW_STATIC_MODULES (cmake/Modules/Config/BuildOptions.cmake)"
#endif
