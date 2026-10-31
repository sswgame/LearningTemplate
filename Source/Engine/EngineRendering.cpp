#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/AutomationCaptureUtil.h"
#include "Engine/Engine.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/ScreenshotRequestQueue.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

// 엔진 루프의 렌더러 몫(창 크기 · 렌더 스레드 · 렌더 패킷 · 백엔드 교체)이다. 렌더러를 짓는 타깃(Game · Client)의 구현이고, 전용 서버(SW_TARGET_TYPE=Server —
// `Renderer/` 를 짓지 않는다)는 `EngineRenderingServer.cpp` 를 CMake 가 고른다(`Source/Engine/CMakeLists.txt`). 파일 안 `SW_WITH_CLIENT_CODE` 분기 대신 빌드 단위로 가른다.
namespace sw
{
    SW_LOG_CALLER( "Engine" );

    void Engine::onMainWindowResize( const uint32 width, const uint32 height )
    {
        if ( _pRHI == nullptr || _pRHI->hasDevice() == false )
            return;
        // 렌더 스레드가 따로 돌면 그 스레드가 beginFrame/endFrame 으로 스왑체인과 렌더 타깃을 계속 건드리고 있을 수 있다. 창 메시지 스레드가 바로
        // resize() 를 부르면 같은 자원에 대한 스레드 간 레이스가 된다. waitIdle() 로 큐를 비우고 GPU 까지 쉬게 한 뒤 크기를 바꾼다.
        if ( _pRenderThread != nullptr )
            _pRenderThread->waitIdle( RHIWaitIdleReason::Resize );
        _pRHI->getDevice().resize( width, height );
    }

    void Engine::drainRenderThreadPackets()
    {
        if ( _pRenderThread != nullptr )
            _pRenderThread->drainPackets();
    }

    void Engine::submitRenderPacket( float32 deltaTime, const HostViewTargets& views, const ViewCameraProviderDelegate& sceneViewCameraProvider )
    {
        // 경로로 잡힌 머티리얼(메시의 저장된 참조)의 GPU 초기화는 게임 스레드가 하지 않는다 — 렌더 스레드가 셋업 구간(기록 전)에 프레임당 상한 안에서 한다
        // (`MaterialCache::initializePending`, bindless 등록이 지난 프레임의 병렬 기록과 겹치지 않는다). 패킷 빌더는 `Ready` 가 아닌 머티리얼의 배치를 건너뛴다.
        if ( _pRHI != nullptr && _pRHI->hasDevice() )
        {
            // 밉 스트리밍이 바꿔 끼운 옛 텍스처를 게임 스레드에서 해제한다 — 이미 만든 패킷이 옛 SRV 번호를 쓰는 동안은 `releaseHandle` 이 미룬다.
            engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().releaseRetiredTextures( &_pRHI->getDevice() );
        }

        // 패킷 · 씬 스냅샷은 FrameRenderer 단계가 만든다. 그 단계가 서지 않았으면(헤드리스 작업) 낼 패킷이 없다.
        if ( _pPacketBuilder == nullptr || _pPacketBuilder->isInitialized() == false )
            return;

        Scene* pActiveScene = _pSceneManager != nullptr ? _pSceneManager->getActiveScene() : nullptr;

        // 패킷 채우기의 순서는 여기서 보인다: 열기 → 관찰자 → 씬 · 뷰 → 캔버스 → 제출. 씬 · 뷰는 위의 씬 틱 · 디버그 그리기 · UI 갱신 **뒤**라
        // 이번 프레임의 게임 상태를 싣고, 캔버스는 UI 갱신이 칠한 목록을 싣는다.
        BLOCK( "RenderFramePacket 제출" )
        {
            SW_MEMORY_SCOPE( RenderCPU );
            RenderFramePacket& packet = _pPacketBuilder->beginPacket();
            // 이 프레임을 찍어 달라고 맡긴 것(`ScreenshotRequestQueue::putForNextPacket`)은 다음에 그리는 이 패킷에 싣는다.
            if ( ScreenshotRequestQueue* const pMailbox = engine::findSubsystem<ScreenshotRequestQueue>() )
                (void)pMailbox->takeForPacket( packet._screenshotPath );

            // 시작 인자 캡처(`-gv_screenshotSettledFrames`)가 기다리는 동안만 판정한다 — 첫 씬이 플레이를 시작하기 전(로딩 화면을 열기 전)은 자리 잡지 않았다.
            if ( RenderThread::needsCaptureReadiness() )
            {
                const GameObjectManager* pManager = pActiveScene != nullptr ? pActiveScene->getObjectManager() : nullptr;
                packet._bReadyForCapture          = pManager != nullptr && pManager->hasBegunPlay() && AutomationCaptureUtil::isReadyForCapture( pManager );
            }

            _pPacketBuilder->populateScene( packet, views, sceneViewCameraProvider, pActiveScene, deltaTime, *_pRHI, _pFrameRenderer );
            // 미리보기 씬(에디터 툴 창)은 씬마다 자기 스냅샷 · 렌더 타깃으로 — 활성 씬과 섞이지 않는다.
            if ( _pSceneManager != nullptr )
                _pPacketBuilder->populateScenePreviews( packet, views, *_pSceneManager );
            _pPacketBuilder->populateCanvas( packet, views.isSceneViewMain(), _pUISystem, _pFontService, _pRHI );
            _pPacketBuilder->resolveMarkerInstanceSlots( packet, _pUISystem, pActiveScene );

            if ( _pRenderThread != nullptr )
            {
                // GT 가 여기서 기다린다면 그것은 렌더 스레드가 밀린 것이다. 링이 차면 submit 이 막는다.
                SW_PROFILE_SCOPE( "GT.Packet.submit" );
                _pRenderThread->submit( packet );
            }
        }
    }

    bool Engine::applyPendingBackendChange()
    {
        if ( _pRHI == nullptr )
            return false;

        const RHIBackend requested = _pRHI->consumePendingBackendChange();
        const RHIBackend previous  = _pRHI->getCommittedBackend();
        if ( requested == previous )
            return true;

        SW_LOG_INFO( "Soft-recreating RHI: %# → %#",
                     RHI::getBackendTypeName( previous ),
                     RHI::getBackendTypeName( requested ) );
        // 교체의 단계마다 든 시간을 남긴다 - 무엇이 교체를 무겁게 하는지(내리기 · 디바이스 · 자원 다시 올리기 · 단계 다시 세우기) 실행마다 보인다.
        const Stopwatch swapWatch;

        BLOCK( "디바이스에 매인 단계 내리기" )
        {
            // RHI 에 (간접으로라도) 의존하는 단계(씬의 디바이스 · 라이브 셰이더 · 렌더 스레드 · 렌더러)를 기동 표의 역순으로, 기동과 같은
            // 본문으로 내린다. 씬 스냅샷 빌더가 든 머티리얼 · 인스턴스도 FrameRenderer 단계의 종료가 옛 디바이스가 살아 있을 때 놓는다.
            _startup.getCollection().shutdownDependentsOf( *_pRHISubsystem );

            // 옛 디바이스의 GPU 자원은 recreateDevice 안의 shutdown 이 레지스트리에 통보하며 거둔다.
            _pRHI->getDevice().waitIdle( RHIWaitIdleReason::BackendSwap );
            engine::getSubsystem<ShaderCache>().clearCache();
        }

        const uint64 shutdownMicroseconds = swapWatch.getElapsedMicroseconds();
        if ( _pRHI->recreateDevice( requested ) == false )
        {
            SW_LOG_ERROR( "recreateDevice failed; restoring previous backend %#",
                          RHI::getBackendTypeName( previous ) );
            gv_rhiBackend = previous;
            if ( _pRHI->recreateDevice( previous ) == false )
            {
                SW_LOG_ERROR( "Failed to restore previous RHI backend — device is gone." );
                return false;
            }
            (void)rebindSceneAfterDeviceRecreate(); // 되돌린 장치에 장면을 다시 묶는다 — 이 경로는 이미 실패로 답한다
            return false;
        }

        const uint64 recreateMicroseconds = swapWatch.getElapsedMicroseconds();
        string       rebindTiming;
        if ( rebindSceneAfterDeviceRecreate( &rebindTiming ) == false )
            return false;
        SW_LOG_INFO( "Backend swap took %#ms (shut down %#ms, device %#ms, %#)", swapWatch.getElapsedMicroseconds() / 1000, shutdownMicroseconds / 1000,
                     ( recreateMicroseconds - shutdownMicroseconds ) / 1000, rebindTiming.c_str() );
        // 교체 뒤 디바이스에 매인 설정이 새 디바이스를 따르는지 한 줄로 남긴다(AppSmokeTest.BackendSwapFollowsTheNewDevice 가 읽는다).
        SW_LOG_INFO( "Active backend is now %# (merge batches across materials %#, native bindless sampling %#)", RHI::getBackendTypeName( _pRHI->getCommittedBackend() ),
                     _pPacketBuilder->isMergeBatchesAcrossMaterials() ? 1 : 0,
                     _pRHI->getDevice().supportsNativeBindlessSampling() ? 1 : 0 );
        return true;
    }
} // namespace sw
