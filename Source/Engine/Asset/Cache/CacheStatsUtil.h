/**
 * @file CacheStatsUtil.h
 * @brief 캐시 계측(`CacheStats`)의 센 값과 `AssetManager` 의 캐시 목록을 합쳐 캐시마다 한 줄로 남기는 보고입니다.
 * @details 한 줄은 이름 · 항목 수 · 적중 · 실패 · 적중률 · 비우기 횟수입니다. 항목 수는 캐시가 계측에 알린 값(`CacheStatsCounters::addItemCount`)이고, 알리지 않은 캐시는
 *          `IAssetCache::getCachedCount` 로 묻습니다. 계측에 칸이 없는 `IAssetCache`(조회가 없는 어댑터 — 오디오 · 현지화)는 적중 · 실패 자리에 `n/a` 를 찍어
 *          "세지 않는 캐시" 가 보고에서 빠지지 않고 보이게 합니다. 보고는 Info 로그라 Shipping 에서는 본문이 사라집니다.
 *
 *          `-gv_profileFrames=N` 측정 창 보고가 나온 프레임에 `Engine` 이 부릅니다(`FrameProfileSession` 은 Profiling 층이라 에셋 매니저를 볼 수 없어 GPU 메모리 표와 같은 자리에서 잇는다).
 *          적중 · 실패는 창 시작(워밍업 뒤, `CacheStats::markWindowStart`)부터의 값입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct CacheStatsUtil
     * @brief 캐시 계측 보고의 정적 함수 묶음입니다.
     */
    struct SW_API CacheStatsUtil
    {
        /**
         * @brief 캐시마다 한 줄(`[CacheStats]`)을 Info 로그로 남깁니다. 이름 순입니다.
         * @param pTitle 첫 줄에 찍는 보고 이름입니다(문자열 상수).
         */
        static void logReport( const utf8* pTitle );
    };
} // namespace sw
