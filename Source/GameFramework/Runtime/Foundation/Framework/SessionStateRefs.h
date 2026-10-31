/**
 * @file SessionStateRefs.h
 * @brief 키트 시뮬레이션이 빌려 쓰는 공유 상태의 포인터 묶음입니다. 키트는 기반 상태(지갑, 가방, 플래그, 시계, 날씨, 일지, 평판, 땅)를 직접 소유하지 않고 이것으로 받습니다.
 * @details 키트를 섞은 게임은 `SessionStateComponent::makeRefs()` 를 쓰고, 키트 하나만 쓰는 게임과 테스트는 자기가 소유한 객체의 주소를 채웁니다.
 *          키트가 쓰지 않는 필드는 nullptr 이어도 된다. 쓰는 필드가 nullptr 이면 그 기능을 끄거나(문서에 적는다) 알리고 시작하지 않는다.
 *          전방 선언만 가지므로 레이어(Framework 2)를 넘지 않는다.
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

    /** @struct SessionStateRefs @brief 빌린 공유 상태입니다. 모두 빌려 쓰므로 키트보다 오래 살아야 합니다. */
    struct SessionStateRefs
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
