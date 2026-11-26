/**
 * @file ILoadingScreen.h
 * @brief 씬을 비동기로 바꾸는 동안의 로딩 화면 인터페이스입니다(구현은 클라이언트 전용 `LoadingScreenController`, 세우는 쪽은 `IGameInstanceClientHook`).
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class ScreenTransitionController;

    /** @class ILoadingScreen @brief 씬을 비동기로 바꾸는 동안의 로딩 화면입니다(구현은 클라이언트 전용 `LoadingScreenController`). 게임 스레드에서 부릅니다. */
    class ILoadingScreen
    {
    public:
        virtual ~ILoadingScreen() = default;

        /** @brief gamesettings 가 로딩 화면 문서를 적었으면 true 입니다(없으면 로딩 화면 없이 씬을 바꾼다). */
        [[nodiscard]] virtual bool hasLoadingScreenDocument() const = 0;
        /** @brief 로딩 화면을 띄웁니다. 띄우지 못하면(UI 가 없다) false 입니다. */
        virtual bool beginLoading() = 0;
        /** @brief 매 프레임 로딩 상태 · 진행률과 화면 페이드를 반영합니다. */
        virtual void update( float32 deltaSeconds, bool bLoading, optional<float32> loadProgress, ScreenTransitionController& transition ) = 0;
    };
} // namespace sw
