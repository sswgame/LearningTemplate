/**
 * @file DebugHUDEngineSections.cpp
 * @brief 엔진 기본 디버그 HUD 섹션입니다 — 이미 있는 서비스(프로파일러 · 씬 · 물리 · 오디오 · 입력 · 카메라 · 디버그 값)에서 읽기만 합니다.
 * @details 섹션 본문은 HUD 가 켜져 있고 그 섹션이 켜져 있을 때만 4 Hz 로 불린다. 꺼진 섹션은 아무것도 읽지 않는다.
 *          네트워크 핑은 엔진에 연결 서비스가 없어 여기 없다 — 온라인 키트가 자기 .cpp 에 `SW_DEBUG_HUD_SECTION` 으로 단다.
 */
#include "pch.h"

#include "Engine/Console/DebugHUDRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Diagnostics/MemoryProfiler.h"
    #include "Core/Math/MathUtil.h"
    #include "Core/Memory/MemoryTag.h"

    #include "Engine/Audio/AudioEngine.h"
    #include "Engine/Audio/IAudioSystem.h"
    #include "Engine/Common/EngineServices.h"
    #include "Engine/Input/Devices/MouseDevice.h"
    #include "Engine/Input/InputManager.h"
    #include "Engine/Object/Component/CameraComponent.h"
    #include "Engine/Object/GameObject/CameraRegistry.h"
    #include "Engine/Object/GameObject/GameObject.h"
    #include "Engine/Object/GameObject/GameObjectManager.h"
    #include "Engine/Object/GameObject/PrimitiveRegistry.h"
    #include "Engine/Object/GameObject/ScenePhysics.h"
    #include "Engine/Physics/IPhysicsScene.h"
    #include "Engine/Profiling/FrameProfiler.h"
    #include "Engine/Scene/Scene.h"
    #include "Engine/Scene/SceneManager.h"
    #include "Engine/Utility/DebugOverlayState.h"

namespace sw
{
    namespace
    {
        struct DebugHUDEngineSectionsInternal
        {
            /** @brief 그래프 막대 높이의 끝(밀리초) — 30 FPS 의 두 배. 넘는 프레임은 끝에 붙는다. */
            static constexpr float32 kGraphMaxMilliseconds = 66.7f;
            /** @brief 메모리 섹션이 보이는 태그 수입니다(살아 있는 바이트가 큰 순). */
            static constexpr uint32  kMemoryTagLineCount = 4;
            static constexpr float64 kBytesPerMegabyte   = 1024.0 * 1024.0;

            /** @brief 활성 씬의 오브젝트 매니저입니다(씬이 없으면 nullptr). */
            static GameObjectManager* findActiveObjectManager()
            {
                const Scene* pScene = engine::getSceneManager().getActiveScene();
                return pScene != nullptr ? pScene->getObjectManager() : nullptr;
            }

            /** @brief 프로파일러 구간 @p slot 의 마지막으로 접힌 프레임 시간(밀리초)입니다. */
            static float64 readLastFrameMilliseconds( uint32 slot )
            {
                return static_cast<float64>( engine::getFrameProfiler().getLastFrameNanos( slot ) ) / 1.0e6;
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
                    case InputKeyboardFocus::Count:
                        return "?";
                }
                return "?";
            }

            static void writeFps( DebugHUDSectionWriter& writer )
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
                    arrMillisecond[index] = pSeconds[index] * 1000.0f;
                }
                const float32 average = total / static_cast<float32>( count );
                writer.addLineFormat( "FPS", "%.1f", average > 0.0f ? 1.0f / average : 0.0f );
                writer.addLineFormat( "frame", "%.2f ms (worst %.2f)", average * 1000.0f, worst * 1000.0f );
                writer.addGraph( arrMillisecond, count, kGraphMaxMilliseconds );
            }

            static void writeTiming( DebugHUDSectionWriter& writer )
            {
                FrameProfiler&      profiler  = engine::getFrameProfiler();
                static const uint32 s_gtSlot  = profiler.registerScope( "GT.Frame" );
                static const uint32 s_rtSlot  = profiler.registerScope( "RT.Frame" );
                static const uint32 s_gpuSlot = profiler.registerScope( "GPU.Frame" );
                writer.addLineFormat( "game thread", "%.2f ms", readLastFrameMilliseconds( s_gtSlot ) );
                writer.addLineFormat( "render thread", "%.2f ms", readLastFrameMilliseconds( s_rtSlot ) );
                writer.addLineFormat( "GPU", "%.2f ms", readLastFrameMilliseconds( s_gpuSlot ) );
            }

            static void writeDraw( DebugHUDSectionWriter& writer )
            {
                FrameProfiler&      profiler    = engine::getFrameProfiler();
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
                    // 태그 집계(Debug 의 MemoryProfiler)가 없는 구성 — 운영체제 힙만 본다.
                    writer.addLineFormat( "heap", "%.1f MB", static_cast<float64>( MemoryProfiler::getPlatformHeapBytes() ) / kBytesPerMegabyte );
                    return;
                }
                writer.addLineFormat( "live", "%.1f MB", static_cast<float64>( pProfiler->getLiveAllocatedBytes() ) / kBytesPerMegabyte );
                const array<MemoryTag, kMemoryTagCount> arrTag = pProfiler->makeTagOrderByLiveBytes();
                for ( uint32 index = 0; index < kMemoryTagLineCount && index < kMemoryTagCount; ++index )
                {
                    const uint64 bytes = pProfiler->getStats( arrTag[index] )._currentAllocatedBytes.load( std::memory_order_relaxed );
                    if ( bytes == 0 )
                        break;
                    writer.addLineFormat( MemoryProfiler::getMemoryTagName( arrTag[index] ), "%.1f MB", static_cast<float64>( bytes ) / kBytesPerMegabyte );
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
                uint32 objectCount    = 0;
                uint32 componentCount = 0;
                pManager->forEachGameObject( [&objectCount, &componentCount]( GameObject* pObject )
                {
                    if ( pObject == nullptr )
                        return;
                    ++objectCount;
                    componentCount += static_cast<uint32>( pObject->getComponentCount() );
                } );
                writer.addLineFormat( "objects", "%#", objectCount );
                writer.addLineFormat( "components", "%#", componentCount );
            }

            static void writePhysics( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* pManager = findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    writer.addLine( "scene", "none" );
                    return;
                }
                const ScenePhysics& physics = pManager->getScenePhysics();
                if ( const IPhysicsScene3D* pScene3D = physics.findScene3D() )
                    writer.addLineFormat( "bodies 3D", "%#", pScene3D->getBodyCount() );
                if ( const IPhysicsScene2D* pScene2D = physics.findScene2D() )
                    writer.addLineFormat( "bodies 2D", "%#", pScene2D->getBodyCount() );
                writer.addLineFormat( "components", "%#", physics.getComponentCount() );
                writer.addLineFormat( "contacts", "%#", static_cast<uint32>( physics.getFrameEvents3D().size() + physics.getFrameEvents2D().size() ) );
                writer.addLineFormat( "steps", "%#", physics.getStepCount() );
            }

            static void writeAudio( DebugHUDSectionWriter& writer )
            {
                const AudioEngineStats stats = engine::getAudioSystem().getEngine().getStats();
                writer.addLineFormat( "voices", "%# (%# real, %# virtual)", stats._voiceCount, stats._realVoiceCount, stats._virtualVoiceCount );
                writer.addLineFormat( "instances", "%#", stats._instanceCount );
                writer.addLineFormat( "dropped", "%#", stats._droppedEventCount );
            }

            static void writeInput( DebugHUDSectionWriter& writer )
            {
                const InputManager& input = engine::getInputManager();
                writer.addLine( "device", getGlyphStyleName( input.getActiveGlyphStyle() ) );
                writer.addLine( "keyboard", getKeyboardFocusName( input.getKeyboardFocus() ) );
                if ( const MouseDevice* pMouse = input.getMouse() )
                {
                    const int2 position = pMouse->getPosition();
                    writer.addLineFormat( "mouse", "%#, %#%#", position._x, position._y, input.isMouseLockActive() ? " (locked)" : "" );
                }
                if ( input.isVirtualInputAttached() )
                    writer.addLine( "virtual", input.isOsInputSuppressed() ? "exclusive" : "mixed" );
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

            static void writeOverlay( DebugHUDSectionWriter& writer )
            {
                // 게임이 쓴 디버그 값(`DebugOverlayState`) — 에디터 게임 뷰의 줄과 같은 것이다.
                vector<DebugOverlayRow> listRow;
                engine::getDebugOverlayState().collectRows( listRow );
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
    SW_DEBUG_HUD_SECTION( fps, "fps", "Frame rate", 0, DebugHUDSectionFlag::kDefaultOn, &DebugHUDEngineSectionsInternal::writeFps );
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
} // namespace sw

#endif
