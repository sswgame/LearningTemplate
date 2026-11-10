/**
 * @file DecalRainWashSystem.h
 * @brief 비가 데칼을 씻는 프레임 시스템입니다 — 날씨 값(`wetness`) × 지붕 판정을 `DecalComponent::applyRainWash` 에 넣습니다.
 * @details 데칼은 엔진 타입이라 날씨(GameFramework)를 모른다 — 날씨를 여는 `WeatherDirectorComponent` 가 플레이 시작에 붙이고 끝에 뗀다(강수를 가진 쪽이 그 효과도 갖는다).
 *          씻기는지 · 얼마나 빨리는 데칼 데이터(`_rainWashPerSecond`)가 밝힌다 — 0 인 데칼(칠한 선 · 그을음)은 건너뛴다. 캐릭터 젖음(`SurfaceExposureSystem`)과 같은 비 · 지붕 규칙이다.
 *          지붕 광선은 데칼마다 매 프레임 쏘지 않는다 — 시간을 쌓아 `kWashIntervalSeconds` 마다 한 번 넣는다(피 · 흙이 씻기는 빠르기는 초 단위다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class DecalRainWashSystem
     * @brief 매니저 하나에 하나씩 붙는 데칼 비 씻김입니다. 파일 머리말 참고.
     */
    class SW_GF_API DecalRainWashSystem final : public ISceneMainFrameSystem
    {
    public:
        /** @brief 매니저의 프레임 시스템 키입니다. */
        static constexpr const utf8* kFrameSystemKey = "DecalRainWashSystem";
        /** @brief 씻김을 넣는 간격(초)입니다. */
        static constexpr float32 kWashIntervalSeconds = 0.5f;
        /** @brief 지붕을 찾는 광선 길이(m)입니다 — 캐릭터 노출의 기본 지붕 거리와 같다. */
        static constexpr float32 kShelterDistance = 20.0f;

        DecalRainWashSystem();

        /** @brief 매니저에 붙은 시스템입니다. 없으면 nullptr 입니다. */
        static DecalRainWashSystem* find( const GameObjectManager& manager );
        /** @brief 매니저의 시스템을 반환합니다. 없으면 붙입니다(게임 스레드, 틱 밖). */
        static DecalRainWashSystem& ensureFor( GameObjectManager& manager );
        /** @brief 시스템을 뗍니다(날씨가 닫힐 때). */
        static void release( GameObjectManager& manager );

        /** @brief 시간을 쌓고, 간격이 차면 씻김(`runWashPass`)을 틱 뒤 큐에 넣습니다. */
        void runMain( GameObjectManager& manager, float32 deltaTime ) override;
        /** @brief 틱 뒤 큐에 일을 넣기만 한다 — 씬 데이터를 읽거나 쓰지 않는다. */
        SceneFrameAccess getFrameAccess() const override;
        /** @brief 씻기는 데칼마다 지금 비 세기(지붕 아래면 0)로 @p seconds 동안 씻습니다. 게임 스레드, 틱 밖(테스트도 부른다). */
        void runWashPass( GameObjectManager& manager, float32 seconds );

    private:
        vector<ComponentHandle> _listHandleScratch;  ///< 도는 동안의 핸들 사본(재사용 버퍼)
        float32                 _pendingSeconds;     ///< 아직 넣지 않은 플레이 시간
        hashed_string           _precipitationValue; ///< 강수 날씨 값 이름(프레임마다 intern 하지 않게 한 번 만든다)
    };
} // namespace sw
