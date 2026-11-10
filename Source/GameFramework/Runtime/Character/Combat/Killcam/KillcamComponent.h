/**
 * @file KillcamComponent.h
 * @brief 킬캠 — 판정이 끝낸 발을 고정 용량으로 기록하고, 고른 발을 그 오브젝트의 카메라로 연출 재생합니다(탄 따라가기 → 명중 클로즈업 → 게임 카메라로 돌아감).
 * @details **연출 전용 재생**이다: 기록(`KillcamShot`)만 읽고 피해 · 물리 · 입력 · 네트워크를 다시 돌리지 않는다. 화면은 엔진 카메라 레지스트리의 컷 카메라(`CameraRegistry::setCutCamera`)로 덮고,
 *          끝나거나 건너뛰면(`skip`) 푼다 — 시퀀서 카메라 컷과 같은 길이라 게임 카메라가 원래 규칙으로 돌아간다. 길이는 설정의 상한에서 자른다(`KillcamSettings::_maxSeconds`).
 *          서버 권위 게임은 서버가 정한 명중을 받아 `recordShot` 에 넣고, 관전 · 재접속은 기록이 고정 용량이라 마지막 몇 발만 남는다(오래된 발은 덮인다).
 *          배치: 씬에 카메라(`CameraComponent`, 역할 Custom)와 이 컴포넌트를 단 오브젝트 하나 — 카메라가 없으면 재생은 오류로 멈춘다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Combat/Killcam/KillcamShot.h"

namespace sw
{
    class GameObjectManager;

    /** @class KillcamComponent @brief 킬캠 기록과 연출 재생입니다. 파일 머리말 참고. */
    REFLECT( Category = "Combat", DisplayName = "Killcam", Tooltip = "Records resolved shots and replays one through this object's camera (bullet follow, hit close-ups)" )
    class SW_GF_API KillcamComponent : public Component
    {
    public:
        REFLECT_BODY();

        KillcamComponent();
        virtual ~KillcamComponent() override = default;

        /** @brief 씬의 컴포넌트 레지스트리에 오릅니다 — 쏘는 쪽이 기록할 킬캠을 씬을 훑지 않고 찾는다(`findInScene`). */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레지스트리에서 내립니다. */
        void onUnregister( GameObjectManager& manager ) override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 판정이 끝낸 발 하나를 기록합니다. 가득 차면 가장 오래된 발을 덮는다. */
        void recordShot( const KillcamShot& shot );
        /** @brief 쓰러뜨린 발 가운데 가장 최근 것을 재생합니다. 없거나 카메라가 없으면(오류) false 입니다. */
        bool playLastKill();
        /** @brief 기록 @p shot 을 재생합니다. 오브젝트에 카메라가 없으면 오류를 남기고 false 입니다. */
        bool play( const KillcamShot& shot );
        /** @brief 재생을 끝내고 게임 카메라를 돌려줍니다(건너뛰기). */
        void skip();

        bool   isPlaying() const { return _bPlaying == SW_TRUE; }
        uint32 getShotCount() const { return static_cast<uint32>( _listShot.size() ); }
        /** @brief 가장 최근 기록의 명중 수입니다(감시 값 — 관통탄이면 2 이상). */
        uint32 getLastHitCount() const;
        /** @brief 지금 재생 순간의 단계입니다(재생 중이 아니면 Done). */
        KillcamPhase           getPhase() const { return _pose._phase; }
        const KillcamSettings& getSettings() const { return _settings; }

        /** @brief 씬의 첫 킬캠(등록 순서)입니다(쏘는 쪽이 기록을 넣을 곳). 없으면 nullptr 입니다 — 킬캠이 없는 씬은 기록하지 않는다. 레지스트리를 읽어 씬을 훑지 않는다. */
        static KillcamComponent* findInScene( const GameObjectManager& manager );

    private:
        /** @brief 지금 순간의 카메라를 오브젝트에 놓습니다. */
        void applyPose();

    private:
        PROPERTY( Category = "Killcam", DisplayName = "Settings", Tooltip = "Follow speed, distances, hold and the longest killcam" )
        KillcamSettings _settings;
        PROPERTY( Category = "Killcam", DisplayName = "Shot Capacity", Min = 1, Max = 64, Tooltip = "Recent shots kept; older shots are overwritten" )
        uint32 _shotCapacity;

        vector<KillcamShot>    _listShot; ///< 기록(가장 오래된 것이 앞)
        KillcamShot            _playing;  ///< 재생 중인 발(사본 — 기록이 덮여도 끝까지 간다)
        KillcamPose            _pose;     ///< 지금 순간
        float32                _elapsed;  ///< 재생한 시간(초)
        uint8                  _bPlaying : 1;
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw
