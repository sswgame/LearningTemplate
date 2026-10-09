/**
 * @file GameStateRefs.h
 * @brief 키트 시뮬레이션이 빌려 쓰는 공유 상태의 포인터 묶음입니다 — 키트는 기반 상태(지갑 · 가방 · 플래그 · 시계 · 날씨 · 일지 · 평판 · 땅)를 제 것으로 들지 않고 이것으로 받습니다.
 * @details 섞인 게임은 `GameStateComponent::makeRefs()`, 키트 하나만 쓰는 게임 · 시험은 자기가 든 객체의 주소를 채웁니다. 키트가 쓰지 않는 칸은 nullptr 이어도 된다 —
 *          쓰는 칸이 nullptr 이면 그 기능을 끄거나(문서에 적는다) 알리고 시작하지 않는다. 앞 선언만 들어 층(Framework 2)을 넘지 않는다.
 */
#pragma once

namespace sw
{
    class GameFlags;
    class Inventory;
    class LandRegistry;
    class QuestLog;
    class ReputationState;
    class Wallet;
    class WeatherSystem;
    class WorldClock;

    /** @struct GameStateRefs @brief 빌린 공유 상태 — 모두 빌려 씁니다(키트보다 오래 살아야 한다). */
    struct GameStateRefs
    {
        Wallet*          _pWallet{ nullptr };
        Inventory*       _pInventory{ nullptr }; ///< 플레이어 가방
        GameFlags*       _pFlags{ nullptr };
        WorldClock*      _pClock{ nullptr };
        WeatherSystem*   _pWeather{ nullptr };
        QuestLog*        _pQuestLog{ nullptr };
        ReputationState* _pReputation{ nullptr };
        LandRegistry*    _pLand{ nullptr };
    };
} // namespace sw
