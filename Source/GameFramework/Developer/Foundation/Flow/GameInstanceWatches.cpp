/**
 * @file GameInstanceWatches.cpp
 * @brief `GameInstance` 의 개발 명령 · 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Map/InputMapBundle.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Network/NetMessageChannel.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Runtime/Foundation/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Flow/GameInstanceClientHook.h"
#include "GameFramework/Runtime/Foundation/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/Flow/IContentConfiguration.h"
#include "GameFramework/Runtime/Foundation/Flow/IGame.h"
#include "GameFramework/Runtime/Foundation/Flow/ILoadingScreen.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettingsOverride.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureComponents.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Save/GameLocalStore.h"
#include "GameFramework/Runtime/Foundation/Save/GameSaveSlots.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDataWatcher.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Progression/PlayerStatsSubsystem.h"
#include "GameFramework/Runtime/Online/HTTP/TelemetryUploadAdapter.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"

namespace sw
{
    namespace
    {
        struct GameInstanceDevInternal
        {
            /** @brief `Save.CapturePeakMs` 가 읽는 백분위입니다 — p100(창 안 최대). */
            static constexpr uint32 kPeakPercent = 100;

            /**
             * @brief 감시 값 `Save.CaptureMs` · `Save.CapturePeakMs` 입니다 — 세이브 찍기 구간의 마지막 프레임 시간 · 측정 창 p100(밀리초).
             * @details 프로파일러가 없거나 꺼져 있으면 값이 없다(`Scene.ComponentTickMs` 와 같은 길). 마지막 프레임 값은 찍은 프레임이 지나면 0 이다.
             */
            template <bool bPeak>
            [[nodiscard]] static bool readCaptureMilliseconds( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                FrameProfiler* const pProfiler = game::findSubsystem<FrameProfiler>();
                if ( pProfiler == nullptr || pProfiler->isEnabled() == false )
                    return false;
                const uint32 slot  = pProfiler->registerScope( GameInstance::kSaveCaptureScopeName );
                const uint64 nanos = bPeak ? pProfiler->getPercentileNanos( slot, kPeakPercent ) : pProfiler->getLastFrameNanos( slot );
                outValue           = static_cast<float64>( nanos ) / static_cast<float64>( constant::time::kNanosecondsPerMillisecond );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( saveCaptureMs, "Save.CaptureMs", "Game-thread milliseconds of the save-game capture in the last profiled frame (profiler scope GT.Save.capture; no value while the profiler is off)",
                         &GameInstanceDevInternal::readCaptureMilliseconds<false> );
    SW_AUTOMATION_WATCH( saveCapturePeakMs, "Save.CapturePeakMs", "Longest save-game capture in game-thread milliseconds over the profiler window (p100 of GT.Save.capture; no value while the profiler is off)",
                         &GameInstanceDevInternal::readCaptureMilliseconds<true> );
} // namespace sw
