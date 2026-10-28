/**
 * @file ControlSceneRewindWorld.h
 * @brief 되감기 세션(`IntentRewindSession`)이 다루는 장면 하나입니다 — 게임 인스턴스의 스냅샷으로 키를 찍고 되돌리고, 기록된 의도를 키 틱부터 다시 틉니다.
 * @details - 키: `GameInstance::captureSnapshot( Snapshot )` 의 바이트. 되돌리면 장면의 오브젝트가 모두 다시 만들어지고, 조종 시스템도 장면과 함께 떨어졌다 다시 붙으므로
 *            틱 번호(`ControlSystem::setTick`)를 키 틱으로 맞춘다.
 *          - 의도: 되돌린 뒤 기록(`ControlIntentHistory`)의 트랙을 키 틱 번째부터 다시 싣는다(`IntentPlayback::start` 의 `firstIndex`). 재생의 틱 번호는 **재생을 시작한 틱을 0 으로**
 *            센 값이라, 기록은 그 장면을 처음부터 같은 고정 프레임으로 돌릴 때만 같은 궤적이다(`ControlIntentHistory`).
 *          - 한 틱: 장면의 `update` 를 고정 프레임 시간으로 한 번 부른다. 장면 하나를 이 세계가 돌리는 동안 호스트는 같은 장면을 돌리지 않는다(되감는 프레임의 화면은 멈춘 그림).
 *          리플레이 씬(플레이 씬을 건드리지 않고 스냅샷으로 띄운 별도 장면)은 그 장면의 관리자와 게임 인스턴스를 넘기면 같은 길로 돈다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/Base/Character/Control/Intent/IntentRewindSession.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ControlIntentHistory;
    class GameInstance;
    class GameObjectManager;

    /**
     * @class ControlSceneRewindWorld
     * @brief 장면 하나를 `IIntentRewindWorld` 로 보이게 하는 짝입니다. 넘긴 참조들은 이 객체보다 오래 살아야 합니다.
     */
    class SW_GF_API ControlSceneRewindWorld final : public IIntentRewindWorld
    {
    public:
        /**
         * @param instance 스냅샷을 찍고 되돌릴 게임 인스턴스(활성 장면이 @p manager 의 것이어야 한다)
         * @param manager 돌릴 장면의 오브젝트 관리자
         * @param history 다시 틀 의도 기록(재생 파일이거나 라이브 기록). 되돌린 뒤 이 기록의 트랙이 키 틱부터 다시 선다
         * @param fixedDeltaSeconds 한 틱의 시간(초). 기록과 같은 값이어야 한다
         */
        ControlSceneRewindWorld( GameInstance& instance, GameObjectManager& manager, const ControlIntentHistory& history, float32 fixedDeltaSeconds );

        uint32             getTick() const override;
        [[nodiscard]] bool captureKey( vector<uint8>& outBytes ) override;
        [[nodiscard]] bool restoreKey( uint32 keyTick, const vector<uint8>& bytes ) override;
        void               stepTick() override;

    private:
        GameInstance&               _instance;
        GameObjectManager&          _manager;
        const ControlIntentHistory& _history;
        float32                     _fixedDeltaSeconds;
    };
} // namespace sw
