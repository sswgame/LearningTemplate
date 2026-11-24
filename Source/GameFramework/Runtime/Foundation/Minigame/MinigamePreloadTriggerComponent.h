/**
 * @file MinigamePreloadTriggerComponent.h
 * @brief 활성자가 들어오면 미니게임의 맵이 쓸 파일을 미리 읽는 볼륨입니다 — 입구(`MinigameTriggerComponent`)보다 넓게 둘러 들어가기 전에 읽기를 앞당깁니다.
 */
#pragma once
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/TriggerComponent.h"

namespace sw
{
    /**
     * @class MinigamePreloadTriggerComponent
     * @brief 활성자가 들어오면 `_minigame` 의 맵을 미리 읽게 합니다(`MinigameFlow::preload`). 들어가지는 않는다 — 지나쳐도 게임 상태가 바뀌지 않는다.
     * @details 언리얼의 스트리밍 볼륨 · 레벨 프리스트리밍, 유니티 `Addressables.DownloadDependenciesAsync` 를 문 앞 트리거에서 부르는 자리다. 파일 읽기는 화면 · 저장 장치의 일이라 월드마다 판정한다(Local).
     *          게임 인스턴스(흐름)가 없는 월드(에디터 편집 씬)에서는 할 일이 없다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "Minigame Preload Trigger", Tooltip = "Starts reading a minigame's map files when the activator comes near its entrance" )
    class SW_GF_API MinigamePreloadTriggerComponent : public TriggerComponent
    {
    public:
        REFLECT_BODY();

        MinigamePreloadTriggerComponent();
        ~MinigamePreloadTriggerComponent() override = default;

        /** @brief 미리 읽을 미니게임 이름을 정합니다. 코드로 만든 볼륨이 씁니다. 씬에 놓은 볼륨은 PROPERTY 가 채웁니다. */
        void setMinigame( const hashed_string& minigame ) { _minigame = minigame; }

    protected:
        [[nodiscard]] bool onVolumeActivated( const GameObject& activator ) override;

    private:
        PROPERTY( Category = "Minigame", DisplayName = "Minigame", Tooltip = "Minigame whose map files are read ahead (<pack>/minigames/<name>.minigame.xml)" )
        hashed_string _minigame;
    };
} // namespace sw
