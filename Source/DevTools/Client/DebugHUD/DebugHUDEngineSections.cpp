/**
 * @file DebugHUDEngineSections.cpp
 * @brief 엔진 기본 디버그 HUD 섹션입니다. 이미 있는 서비스(프로파일러, 씬, 물리, 오디오, 입력, 카메라, 디버그 값)에서 읽기만 합니다.
 * @details 섹션 본문은 HUD 가 켜져 있고 그 섹션이 켜져 있을 때만 4 Hz 로 불린다. 꺼진 섹션은 아무것도 읽지 않는다.
 *          네트워크 핑은 엔진에 연결 서비스가 없어 여기에 없다. 온라인 키트가 자기 .cpp 에 `SW_DEBUG_HUD_SECTION` 으로 등록한다.
 */
#include "pch.h"

#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryTag.h"
#include "Core/String/StringBuilder.h"

#include "DevTools/Client/Automation/Runner/AutomationRunTitle.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Automation/AutomationRuntime.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DebugHUDSectionFormat.h"
#include "Engine/Console/DebugOverlayState.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct DebugHUDEngineSectionsInternal
        {
            /** @brief 그래프 막대 높이의 상한(밀리초)입니다. 30 FPS 의 두 배이며, 넘는 프레임은 상한에 붙입니다. */
            static constexpr float32 kGraphMaxMilliseconds = 66.7f;
            /** @brief 메모리 섹션이 보이는 태그 수입니다(살아 있는 바이트가 큰 순). */
            static constexpr uint32 kMemoryTagLineCount = 4;
            /** @brief Scenario 섹션의 긴 값(첫 실패 · 앞 단계) 줄 길이의 위쪽 한계(바이트)입니다 — HUD 패널이 화면을 가로지르지 않게. */
            static constexpr uint32 kFailureLineBytes = 72;
            /** @brief Scenario 섹션의 순서 값입니다 — 맨 위(지켜보는 사람이 가장 먼저 읽는 줄). */
            static constexpr int32 kScenarioSectionOrder = -100;

            /** @brief 활성 씬의 오브젝트 매니저입니다(씬이 없으면 nullptr). */
            static GameObjectManager* findActiveObjectManager() { return engine::getSubsystem<SceneManager>().findActiveObjectManager(); }

            /**
             * @brief `objects` 섹션의 수입니다. 구조 세대(`GameObjectManager::getStructureGeneration`)가 같으면 다시 세지 않습니다.
             * @details 생성 · 파괴 · 컴포넌트 붙이기와 떼기가 세대를 올리므로 수가 바뀌는 경우를 놓치지 않는다. 세대는 매니저끼리 겹치지 않아 씬을 바꿔도 맞다.
             */
            struct ObjectCount
            {
                uint64 _structureGeneration{ 0 }; ///< 센 때의 세대. 0 은 나오지 않는 값이다
                uint32 _objectCount{ 0 };
                uint32 _componentCount{ 0 };
            };

            /** @brief 프로파일러 구간 @p slot 의 마지막으로 접힌 프레임 시간(밀리초)입니다. */
            static float64 readLastFrameMilliseconds( uint32 slot )
            {
                return static_cast<float64>( engine::getSubsystem<FrameProfiler>().getLastFrameNanos( slot ) ) / constant::time::kNanosecondsPerMillisecond;
            }

            static const utf8* getGlyphStyleName( InputGlyphStyle style )
            {
                switch ( style )
                {
                    case InputGlyphStyle::KeyboardMouse:
                        return "keyboard/mouse";
                    case InputGlyphStyle::GamepadXbox:
                        return "gamepad (Xbox)";
                    case InputGlyphStyle::GamepadPlayStation:
                        return "gamepad (PlayStation)";
                    case InputGlyphStyle::GamepadSwitch:
                        return "gamepad (Switch)";
                }
                return "?";
            }

            static const utf8* getKeyboardFocusName( InputKeyboardFocus focus )
            {
                switch ( focus )
                {
                    case InputKeyboardFocus::Game:
                        return "game";
                    case InputKeyboardFocus::DevConsole:
                        return "dev console";
                    case InputKeyboardFocus::UI:
                        return "UI text";
                    case InputKeyboardFocus::Tool:
                        return "tool window";
                    case InputKeyboardFocus::Count:
                        return "?";
                }
                return "?";
            }

            static void writeFPS( DebugHUDSectionWriter& writer )
            {
                const uint32 count = writer.getFrameHistoryCount();
                if ( count == 0 )
                    return;
                const float32* pSeconds = writer.getFrameHistory();
                float32        total    = 0.0f;
                float32        worst    = 0.0f;
                float32        arrMillisecond[DebugHUDSectionWriter::kFrameHistoryCount];
                for ( uint32 index = 0; index < count; ++index )
                {
                    total += pSeconds[index];
                    worst                 = MathUtil::max( worst, pSeconds[index] );
                    arrMillisecond[index] = pSeconds[index] * constant::time::kMillisecondsPerSecond;
                }
                const float32 average = total / static_cast<float32>( count );
                writer.addLineFormat( "FPS", "%.1f", average > 0.0f ? 1.0f / average : 0.0f );
                writer.addLineFormat( "frame", "%.2f ms (worst %.2f)", average * constant::time::kMillisecondsPerSecond, worst * constant::time::kMillisecondsPerSecond );
                writer.addGraph( arrMillisecond, count, kGraphMaxMilliseconds );
            }

            static void writeTiming( DebugHUDSectionWriter& writer )
            {
                FrameProfiler&      profiler  = engine::getSubsystem<FrameProfiler>();
                static const uint32 s_gtSlot  = profiler.registerScope( "GT.Frame" );
                static const uint32 s_rtSlot  = profiler.registerScope( "RT.Frame" );
                static const uint32 s_gpuSlot = profiler.registerScope( "GPU.Frame" );
                writer.addLineFormat( "game thread", "%.2f ms", readLastFrameMilliseconds( s_gtSlot ) );
                writer.addLineFormat( "render thread", "%.2f ms", readLastFrameMilliseconds( s_rtSlot ) );
                writer.addLineFormat( "GPU", "%.2f ms", readLastFrameMilliseconds( s_gpuSlot ) );
            }

            static void writeDraw( DebugHUDSectionWriter& writer )
            {
                FrameProfiler&      profiler    = engine::getSubsystem<FrameProfiler>();
                static const uint32 s_drawSlot  = profiler.registerScope( "RT.Draw.indirectCalls" );
                static const uint32 s_batchSlot = profiler.registerScope( "RT.Draw.gpuBatchCount" );
                static const uint32 s_quadSlot  = profiler.registerScope( "UI.CanvasQuads" );
                writer.addLineFormat( "draw calls", "%#", profiler.getLastFrameCount( s_drawSlot ) );
                writer.addLineFormat( "GPU batches", "%#", profiler.getLastFrameCount( s_batchSlot ) );
                const GameObjectManager* pManager = findActiveObjectManager();
                writer.addLineFormat( "instances", "%#", pManager != nullptr ? pManager->getPrimitiveRegistry().getSlotCount() : 0u );
                writer.addLineFormat( "UI quads", "%#", profiler.getLastFrameCount( s_quadSlot ) );
            }

            static void writeMemory( DebugHUDSectionWriter& writer )
            {
                const MemoryProfiler* pProfiler = MemoryProfiler::getActive();
                if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
                {
                    // 태그 집계(Debug 의 MemoryProfiler)가 없는 구성이다. 운영체제 힙만 본다.
                    writer.addLineFormat( "heap", "%.1f MB", static_cast<float64>( MemoryProfiler::getPlatformHeapBytes() ) / constant::memory::kBytesPerMegabyte );
                    return;
                }
                writer.addLineFormat( "live", "%.1f MB", static_cast<float64>( pProfiler->getLiveAllocatedBytes() ) / constant::memory::kBytesPerMegabyte );
                const array<MemoryTag, kMemoryTagCount> arrTag = pProfiler->makeTagOrderByLiveBytes();
                for ( uint32 index = 0; index < kMemoryTagLineCount && index < kMemoryTagCount; ++index )
                {
                    const uint64 bytes = pProfiler->getStats( arrTag[index] )._currentAllocatedBytes;
                    if ( bytes == 0 )
                        break;
                    writer.addLineFormat( MemoryProfiler::getMemoryTagName( arrTag[index] ), "%.1f MB", static_cast<float64>( bytes ) / constant::memory::kBytesPerMegabyte );
                }
            }

            static void writeObjects( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* pManager = findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    writer.addLine( "scene", "none" );
                    return;
                }
                static thread_local ObjectCount s_objectCount{};
                ObjectCount&                    count      = s_objectCount;
                const uint64                    generation = pManager->getStructureGeneration();
                if ( count._structureGeneration != generation )
                {
                    count = ObjectCount{ generation, 0, 0 };
                    pManager->forEachGameObject( [&count]( GameObject* pObject )
                    {
                        if ( pObject == nullptr )
                            return;
                        ++count._objectCount;
                        count._componentCount += static_cast<uint32>( pObject->getComponentCount() );
                    } );
                }
                writer.addLineFormat( "objects", "%#", count._objectCount );
                writer.addLineFormat( "components", "%#", count._componentCount );
            }

            static void writePhysics( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* pManager = findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    writer.addLine( "scene", "none" );
                    return;
                }
                const ScenePhysics& physics = pManager->getSubsystem<sw::ScenePhysics>();
                if ( const IPhysicsBodyScene3D* pScene3D = physics.findScene3D() )
                    writer.addLineFormat( "bodies 3D", "%#", pScene3D->getBodyCount() );
                if ( const IPhysicsBodyScene2D* pScene2D = physics.findScene2D() )
                    writer.addLineFormat( "bodies 2D", "%#", pScene2D->getBodyCount() );
                writer.addLineFormat( "components", "%#", physics.getComponentCount() );
                writer.addLineFormat( "contacts", "%#", static_cast<uint32>( physics.getFrameEvents3D().size() + physics.getFrameEvents2D().size() ) );
                writer.addLineFormat( "steps", "%#", physics.getStepCount() );
            }

            static void writeAudio( DebugHUDSectionWriter& writer )
            {
                const IAudioService* pAudio = engine::findSubsystem<IAudioService>();
                if ( pAudio == nullptr || pAudio->isInitialized() == false )
                {
                    writer.addLine( "service", "none" );
                    return;
                }
                const AudioEngineStats stats = pAudio->getEngine().getStats();
                writer.addLineFormat( "voices", "%# (%# real, %# virtual)", stats._voiceCount, stats._realVoiceCount, stats._virtualVoiceCount );
                writer.addLineFormat( "instances", "%#", stats._instanceCount );
                writer.addLineFormat( "dropped", "%#", stats._droppedEventCount );
            }

            static void writeInput( DebugHUDSectionWriter& writer )
            {
                const InputManager& input = engine::getSubsystem<InputManager>();
                writer.addLine( "device", getGlyphStyleName( input.getActiveGlyphStyle() ) );
                writer.addLine( "keyboard", getKeyboardFocusName( input.getKeyboardFocus() ) );
                if ( const MouseDevice* pMouse = input.getMouse() )
                {
                    const int2 position = pMouse->getPosition();
                    writer.addLineFormat( "mouse", "%#, %#%#", position._x, position._y, input.isMouseLockActive() ? " (locked)" : "" );
                }
                if ( input.isVirtualInputAttached() )
                    writer.addLine( "virtual", input.isOSInputSuppressed() ? "exclusive" : "mixed" );
            }

            static void writeCamera( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* pManager = findActiveObjectManager();
                const CameraComponent*   pCamera  = pManager != nullptr ? pManager->getCameraRegistry().selectCamera( CameraRole::Game ) : nullptr;
                if ( pCamera == nullptr )
                {
                    writer.addLine( "camera", "none" );
                    return;
                }
                const float3 position = pCamera->getCameraPosition();
                const float3 forward  = pCamera->getCameraForward();
                writer.addLineFormat( "position", "%.2f, %.2f, %.2f", position._x, position._y, position._z );
                writer.addLineFormat( "forward", "%.2f, %.2f, %.2f", forward._x, forward._y, forward._z );
                if ( pCamera->isOrthographic() )
                    writer.addLineFormat( "ortho height", "%.2f", pCamera->getOrthoHeight() );
                else
                    writer.addLineFormat( "FOV", "%.1f deg", pCamera->getFieldOfViewY() * MathUtil::kRadianToDegree );
            }

            /**
             * @brief 자동화 시나리오의 진행 · 판정 상태입니다(`IAutomationRuntime::findRunStatus` 를 읽기만 한다 — 실행기는 HUD 를 모른다).
             * @details 통과 · 진행은 중립 색, 실패 · 처리하지 않은 로그만 경고색이다. 체인이면 단계 n/N 과 앞 단계의 결과를 함께 낸다.
             */
            static void writeScenario( DebugHUDSectionWriter& writer )
            {
                const IAutomationRuntime* const  pRuntime = engine::findSubsystem<IAutomationRuntime>();
                const AutomationRunStatus* const pStatus  = pRuntime != nullptr ? pRuntime->findRunStatus() : nullptr;
                if ( pStatus == nullptr )
                {
                    writer.addLine( "scenario", "none" );
                    return;
                }
                const AutomationRunStatus& status = *pStatus;
                if ( status._stageCount > 1 )
                    writer.addLineFormat( "scenario", "%# (%#/%#)", status._scenarioName.c_str(), status._stageIndex + 1, status._stageCount );
                else
                    writer.addLine( "scenario", status._scenarioName );
                writer.addLineFormat( "step", "%# %#", status._stepNumber, status._stepKind.empty() ? "-" : status._stepKind.c_str() );
                const bool bRunning = status._result == AutomationResult::Running;
                const bool bGood    = status._result == AutomationResult::Passed || status._result == AutomationResult::Skipped;
                // 진행 중의 실패는 이미 적혀 끝에서 Failed 가 된다 — 진행 중이어도 경고색이다.
                const bool bWarn = ( bRunning == false && bGood == false ) || status._failureCount > 0;
                writer.addLine( "state", bRunning ? "running" : getEnumName( status._result ), bWarn ? DebugHUDLineStyle::Warning : DebugHUDLineStyle::Neutral );
                if ( status._failureCount > 0 )
                {
                    // 첫 실패 줄만 — HUD 한 줄 폭에 맞춰 자른다(전문은 로그 · 보고에 있다).
                    StringBuilder<constant::buffer::kMax256> failureBuilder;
                    failureBuilder.appendFormat( "%# - %#", status._failureCount, status._firstFailure.c_str() );
                    const string failure{ failureBuilder.view() };
                    writer.addLine( "failures", AutomationRunTitleInput::truncateUTF8( failure, kFailureLineBytes ), DebugHUDLineStyle::Warning );
                }
                // 진행 중에는 모은 Warning · Error 수(허용 판정 전 — 중립), 끝나면 로그 게이트가 처리하지 않은 줄 수(있으면 경고색).
                const bool bUnhandledLog = bRunning == false && status._warningErrorCount > 0;
                writer.addLine( bRunning ? "warn/error" : "unhandled log", to_string( status._warningErrorCount ), bUnhandledLog ? DebugHUDLineStyle::Warning : DebugHUDLineStyle::Neutral );
                writer.addLineFormat( "frame", "%#", status._frame );
                if ( status._previousStageName.empty() == false )
                {
                    const bool                               bPreviousGood = status._previousStageResult == AutomationResult::Passed || status._previousStageResult == AutomationResult::Skipped;
                    StringBuilder<constant::buffer::kMax256> previousBuilder;
                    previousBuilder.appendFormat( "%# %#", status._previousStageName.c_str(), getEnumName( status._previousStageResult ) );
                    const string previous{ previousBuilder.view() };
                    writer.addLine( "previous", AutomationRunTitleInput::truncateUTF8( previous, kFailureLineBytes ), bPreviousGood ? DebugHUDLineStyle::Neutral : DebugHUDLineStyle::Warning );
                }
            }

            static void writeOverlay( DebugHUDSectionWriter& writer )
            {
                // 게임이 쓴 디버그 값(`DebugOverlayState`)이다. 에디터 게임 뷰의 줄과 같은 것이다.
                vector<DebugOverlayRow> listRow;
                engine::getSubsystem<DebugOverlayState>().collectRows( listRow );
                if ( listRow.empty() )
                    writer.addLine( "values", "none" );
                for ( const DebugOverlayRow& row : listRow )
                {
                    writer.addLine( row._key, row._value );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_DEBUG_HUD_SECTION( fps, "fps", "Frame rate", 0, DebugHUDSectionFlag::kDefaultOn, &DebugHUDEngineSectionsInternal::writeFPS );
    SW_DEBUG_HUD_SECTION( timing, "timing", "Thread times", 10, DebugHUDSectionFlag::kDefaultOn | DebugHUDSectionFlag::kUsesFrameProfiler,
                          &DebugHUDEngineSectionsInternal::writeTiming );
    SW_DEBUG_HUD_SECTION( draw, "draw", "Rendering", 20, DebugHUDSectionFlag::kUsesFrameProfiler, &DebugHUDEngineSectionsInternal::writeDraw );
    SW_DEBUG_HUD_SECTION( memory, "memory", "Memory", 30, DebugHUDSectionFlag::kNone, &DebugHUDEngineSectionsInternal::writeMemory );
    SW_DEBUG_HUD_SECTION( objects, "objects", "Scene", 40, DebugHUDSectionFlag::kDefaultOn, &DebugHUDEngineSectionsInternal::writeObjects );
    SW_DEBUG_HUD_SECTION( physics, "physics", "Physics", 50, DebugHUDSectionFlag::kNone, &DebugHUDEngineSectionsInternal::writePhysics );
    SW_DEBUG_HUD_SECTION( audio, "audio", "Audio", 60, DebugHUDSectionFlag::kNone, &DebugHUDEngineSectionsInternal::writeAudio );
    SW_DEBUG_HUD_SECTION( input, "input", "Input", 70, DebugHUDSectionFlag::kNone, &DebugHUDEngineSectionsInternal::writeInput );
    SW_DEBUG_HUD_SECTION( camera, "camera", "Camera", 80, DebugHUDSectionFlag::kNone, &DebugHUDEngineSectionsInternal::writeCamera );
    SW_DEBUG_HUD_SECTION( overlay, "overlay", "Game values", 90, DebugHUDSectionFlag::kDefaultOn, &DebugHUDEngineSectionsInternal::writeOverlay );
    // 맨 위 — 지켜보는 사람이 가장 먼저 읽는 줄이다. 실행이 켠다(사용자 켬 · 끔 · 설정 파일과 무관).
    SW_DEBUG_HUD_SECTION( scenario, "scenario", "Scenario", DebugHUDEngineSectionsInternal::kScenarioSectionOrder, DebugHUDSectionFlag::kAutomationRun, &DebugHUDEngineSectionsInternal::writeScenario );
} // namespace sw
