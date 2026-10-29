/**
 * @file DevOnly.h
 * @brief 개발 빌드에만 있는 함수(핫 리로드 같은)를 런타임 등록 줄에 넘기는 자리 `SW_DEV_ONLY_FUNCTION` 입니다.
 * @details 배포 빌드는 nullptr 이라 그 함수가 인스턴스화되지도 참조되지도 않는다 — 템플릿은 생기지 않고, 템플릿이 아닌 함수의 본문은 개발 전용 파일
 *          (`*Reload.cpp` · `*DevCommands.cpp`, `SW_DEV_ONLY_SOURCE_PATTERNS`)에 둘 수 있다. 빌드 스위치는 여기 한 곳이다(쓰는 곳마다 `#if` 를 두지 않는다).
 */
#pragma once

#if defined( SW_SHIPPING )
    #define SW_DEV_ONLY_FUNCTION( pFunction ) nullptr
#else
    #define SW_DEV_ONLY_FUNCTION( pFunction ) pFunction
#endif
