/**
 * @file GameTimeScale.h
 * @brief 게임 시간 배율(슬로 모션 · 빨리 감기)입니다. 호스트의 프레임 시간이 이 값을 곱해 게임 업데이트 · 씬 틱 · 고정 스텝으로 흐릅니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/Subsystem.h"

namespace sw
{
    /** @brief 게임 시간을 멈추는 까닭입니다(`GameTimeScale::addPauseRequest`). */
    enum class GamePauseReason : uint8
    {
        Player = 0, ///< 플레이어가 멈췄다(정지 메뉴 · 포토 모드). 시간을 다른 기계와 함께 쓰는 동안은 듣지 않는다
        Host,       ///< 호스트가 붙잡았다(시나리오 시작 전). 늘 듣는다
    };
    constexpr uint32 kGamePauseReasonCount = 2;

    /**
     * @class GameTimeScale
     * @brief 게임 시간 배율입니다. 값은 전역 변수 `gv_timeScale` 하나에 있습니다(에디터 툴바 · 전역 변수 패널 · 콘솔 `timescale` 이 같은 값을 씁니다).
     * @details 0 이면 게임 시간이 멈춥니다(에디터 UI 와 에디터 카메라는 자기 시간으로 계속 돕니다). 범위 밖은 `kMinScale` · `kMaxScale` 로 자릅니다.
     *          정지 요청 수와 이번 프레임의 실제 경과는 이 서비스(서비스 범위 — `engine::` · `game::` · `editor::getSubsystem<GameTimeScale>()`)가 든다(정적 칸이 아니다).
     */
    class SW_API GameTimeScale final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "GameTimeScale";
        static constexpr float32     kMinScale      = 0.0f;
        static constexpr float32     kMaxScale      = 16.0f;

        GameTimeScale();
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 지금 배율입니다(범위로 자른 값). 정지 요청이 하나라도 걸려 있으면 0 입니다. */
        float32 get() const;
        /** @brief 정한 배율입니다(범위로 자른 값, 정지 요청과 무관). 요청이 모두 풀리면 `get` 이 이 값이 된다. */
        float32 getSetting() const;
        /** @brief 듣는 정지 요청이 하나라도 걸려 있으면 true 입니다(정지 메뉴 · 로딩 화면 — 멈춘 편집기에서는 첫 Play 까지 로딩 화면이 남는다). */
        bool isPaused() const;
        /** @brief 배율을 정합니다(범위로 자릅니다). 정지 요청과 따로 남습니다 — 요청이 모두 풀리면 이 값으로 돌아갑니다. */
        void set( float32 scale );
        /**
         * @brief 게임 시간을 멈추는 요청을 하나 겁니다. 돌려준 구독이 요청의 주인이며, 풀리거나 사라지면 요청도 풀립니다. 요청은 쌓이고, 하나라도 있으면 멈춥니다.
         * @details `gv_timeScale` 은 바꾸지 않습니다(에디터 · 콘솔이 정한 배율이 남는다). 걸기와 풀기를 따로 부르게 두면 건 쪽이 사라지는 길마다 풀기를 되풀이해 적어야 하고, 하나를 빠뜨리면 게임이 영영 멈춘다.
         */
        [[nodiscard]] Subscription addPauseRequest( GamePauseReason reason );
        /**
         * @brief 이 세계의 시간을 다른 기계와 함께 씁니다(네트워크 세션). 쥔 동안 `GamePauseReason::Player` 요청은 걸려 있어도 듣지 않습니다.
         * @details 한 기계만 멈추면 서버와 다른 플레이어는 계속 흘러 그 기계의 시간만 뒤처진다(언리얼은 클라이언트의 `SetPause` 를 거절한다). 요청은 남아 세션이 끝나면 다시 듣는다.
         */
        [[nodiscard]] Subscription shareWorldTime();
        /** @brief 지금 걸린 정지 요청 수입니다(듣지 않는 요청도 센다). */
        uint32 getPauseRequestCount() const;
        /**
         * @brief 이번 프레임의 실제 경과(배율 · 정지와 무관, 최대 델타로 자름)를 적습니다. `Engine::beginFrame` 이 호스트에게 받은 값으로 부릅니다
         *        (App 은 `FrameTime::_unscaledDeltaTime`, 서버는 틱 길이). 엔진 없이 게임 인스턴스를 돌리는 쪽은 이 서비스를 세워 직접 적습니다.
         * @details 게임이 멈춰도 도는 것, 곧 런타임 UI(정지 메뉴의 애니메이션 · 탐색 반복) · 화면 전환 페이드 · 로딩 화면이 읽습니다(유니티 `Time.unscaledDeltaTime`).
         */
        void setUnscaledDeltaTime( float32 deltaSeconds );
        /**
         * @brief 이번 프레임의 실제 경과입니다. 적은 적이 없으면 단언으로 멈춥니다.
         * @details 대신할 값(ImGui 벽시계 · 배율 걸린 경과)을 받지 않습니다 — 받으면 호출부마다 다른 시간을 고르고, 시나리오 녹화(벽시계)와 재생(고정 스텝)이 어긋납니다.
         */
        float32 getUnscaledDeltaTime() const;

    private:
        /** @brief 정지 요청 하나의 구독이 풀렸습니다(@p reason 은 `GamePauseReason`). */
        void releasePauseRequest( uint64 reason );
        /** @brief 시간을 함께 쓰는 구독이 풀렸습니다. */
        void releaseWorldTimeShare( uint64 unusedID );

    private:
        atomic<uint32>   _arrPauseRequestCount[kGamePauseReasonCount]; ///< 까닭마다 걸린 정지 요청 수(`addPauseRequest`)
        atomic<uint32>   _worldTimeShareCount;                         ///< 시간을 함께 쓰는 쪽의 수(`shareWorldTime`)
        float32          _unscaledDeltaSeconds;                        ///< 이번 프레임의 실제 경과(-1 = 아직 적지 않았다)
        SubscriptionList _pauseSubscriptions;
        SubscriptionList _worldTimeShareSubscriptions;

        SW_SERVICE_SLOT( GameTimeScale, Required );
    };
} // namespace sw
