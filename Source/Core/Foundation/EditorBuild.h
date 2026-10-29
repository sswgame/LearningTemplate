/**
 * @file EditorBuild.h
 * @brief 빌드에 편집기가 있는지의 스위치(`SW_WITH_EDITOR`) 하나입니다(언리얼 `WITH_EDITOR`).
 */
#pragma once

/**
 * @brief 편집기(EditorModule · 편집기 확장)가 있는 빌드인가입니다. 배포 빌드(`SW_SHIPPING`)는 편집기를 짓지 않아 0 이다.
 * @details Engine · Core 에는 편집기 기능 함수를 두지 않는다 — 편집기만 쓰는 코드는 `Source/Editor` · `<키트>/Editor/` 로 간다.
 *          편집기 DLL 로 옮길 수 없는 것(내릴 수 없는 곳에 살아야 하는 OS 스레드 · 비공개 상태를 만지는 저작 함수)만 이 스위치로 감싸고,
 *          배포 빌드는 그 파일을 짓지 않거나 그 구역을 뺀다. 게임 코드가 그 함수를 부르면 배포 빌드가 컴파일에서 멈춘다.
 *          새 편집기 전용 함수가 Engine · Core 에 생기는 것은 린트 `editor-only-api` 가 막는다(형식으로 막을 수 없는 틈).
 */
#if defined( SW_SHIPPING )
    #define SW_WITH_EDITOR 0
#else
    #define SW_WITH_EDITOR 1
#endif
