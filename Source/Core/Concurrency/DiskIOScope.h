/**
 * @file DiskIOScope.h
 * @brief 디스크를 읽고 쓰지 않아야 하는 스레드 구간(렌더 스레드의 패킷 실행)을 표시하는 범위 객체입니다. 파일 입구(`FileUtil` · `AsyncFileHandle`)가 구간 안의 읽기 · 쓰기를 단언으로 막습니다.
 * @details 디스크 한 번이 프레임을 통째로 멈춘다(느린 저장 장치 · 백신 검사). 평균에 드러나지 않아 측정으로는 찾기 어렵다 — 그 스레드가 파일을 만지는 순간 멈춰 알린다.
 *          구간 안에서 파일이 필요한 일은 다른 스레드에 맡긴다(비동기 로드 · 워커 태스크 · `ShaderCache` 의 로드 스레드). 구간 안에서 기다리며 다른 태스크를 도와 돌리는 것은
 *          그 태스크의 IO 다 — `TaskManager` 가 태스크를 돌리는 동안 구간을 푼다(`DiskIOAllowedScope`).
 *          유니티의 `Application.backgroundLoadingPriority` 처럼 예산을 두지 않는다 — 렌더 스레드의 디스크 IO 는 하나도 없어야 한다(언리얼도 렌더 스레드에서 파일을 열지 않는다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @class DiskIOForbiddenScope @brief 이 스레드에서 디스크 IO 를 금지합니다(겹쳐 걸 수 있다). 파일 머리말 참고. */
    class SW_API DiskIOForbiddenScope
    {
    public:
        DiskIOForbiddenScope();
        ~DiskIOForbiddenScope();

        DiskIOForbiddenScope( const DiskIOForbiddenScope& )            = delete;
        DiskIOForbiddenScope& operator=( const DiskIOForbiddenScope& ) = delete;

        /** @brief 이 스레드가 지금 디스크 IO 를 하지 않아야 하면 true 입니다. 파일이 필요한 일을 다른 스레드에 맡길지 이것으로 정합니다. */
        [[nodiscard]] static bool isActive();

    private:
        bool _bOuter; ///< NOLINT(sw-bool-flag): 값 하나뿐인 범위 객체다
    };

    /** @class DiskIOAllowedScope @brief 이 스레드의 금지를 잠시 풉니다 — 기다리는 스레드가 남의 태스크를 도와 돌리는 동안(`TaskManager`)입니다. */
    class SW_API DiskIOAllowedScope
    {
    public:
        DiskIOAllowedScope();
        ~DiskIOAllowedScope();

        DiskIOAllowedScope( const DiskIOAllowedScope& )            = delete;
        DiskIOAllowedScope& operator=( const DiskIOAllowedScope& ) = delete;

    private:
        bool _bOuter; ///< NOLINT(sw-bool-flag): 값 하나뿐인 범위 객체다
    };
} // namespace sw
