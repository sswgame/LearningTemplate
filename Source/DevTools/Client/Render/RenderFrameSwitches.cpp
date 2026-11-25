/**
 * @file DevTools/Client/Render/RenderFrameSwitches.cpp
 * @brief 프레임 렌더 설정 사본(`RenderFrameSettings`)의 진단 · 비교 칸을 덮는 개발 변수들입니다 — 실행 인자 · 콘솔이 바꾸고 다음 프레임의 패킷부터 든다.
 * @details 런타임은 사본의 기본값(배포 동작)만 안다. 변수가 런타임 파일에 있으면 배포 실행 파일이 컬링 · 배치 묶기를 끄는 인자를 받았다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Renderer/Frame/Packet/RenderFrameSettings.h"

namespace sw
{
    namespace
    {
        const RenderFrameSettings kDefaultFrameSettings{};
    } // namespace

    /**
     * @brief `-gv_gpuCulling=0` 이면 GPU 컬링 컴퓨트 디스패치를 건너뜁니다(인다이렉트 드로우는 그대로 실행합니다).
     * @details 간접 인자는 GPUScene 이 CPU 에서 이미 채워 두므로, 이 디스패치만 빼면 "컴퓨트가 인자를 망치는가" 를 백엔드별로 가를 수 있다. 기본은 켬이다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_gpuCulling, kDefaultFrameSettings._bGPUCulling != SW_FALSE, "GPU 컬링 컴퓨트 디스패치 (false=건너뜀, 진단용)" );
    /**
     * @brief `-gv_meshMorphDebugMode=<모드>`: 메시 모프 진단입니다. SkipDispatch 의 정답은 레스트 포즈와 같은 이미지다 — 컴퓨트가 아예 돌지 않으므로 "컴퓨트가 범인인가" 를 가른다.
     */
    SW_TEST_GLOBAL_VARIABLE( MeshMorphDiagMode, gv_meshMorphDebugMode, kDefaultFrameSettings._meshMorphDebugMode, "메시 모프 진단 (Normal 평소 / ForceOn 강제 켬 / SkipDispatch 디스패치 생략)" );
    /**
     * @brief `-gv_drawMerge=0` 이면 같은 PSO 와 머티리얼의 연속 배치를 멀티 드로우 하나로 묶지 않고 배치마다 한 번씩 부릅니다.
     * @details 묶은 이미지와 묶지 않은 이미지가 같아야 한다. 다르면 배치 테이블(g_SwBatches)이나 드로우 ID 가 틀린 것이다. 기본은 묶는 것이다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_drawMerge, kDefaultFrameSettings._bDrawMerge != SW_FALSE, "씬 배치 멀티 드로우 묶기 (false=배치마다 호출, 진단용)" );
    /**
     * @brief `-gv_asyncCompute=0` 이면 `_queue="AsyncCompute"` 프레임 패스도 그래픽스 큐에서 돕니다(시작 인자 — 파이프라인을 묶을 때 계획한다).
     * @details 비동기 큐가 이득인지는 측정으로 정한다(같은 장면에서 켜고 끈 GPU 프레임 시간). 능력이 없는 백엔드는 이 값과 상관없이 그래픽스 큐다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_asyncCompute, kDefaultFrameSettings._bAsyncCompute != SW_FALSE, "프레임 패스의 비동기 컴퓨트 큐 (false=모두 그래픽스 큐, 측정용)" );
    /**
     * @brief `-gv_transientAliasing=0` 이면 뷰 그래프의 트랜지언트 첨부가 텍스처를 나누지 않습니다(시작 인자 — 콜백을 묶을 때 계획한다).
     * @details 나눈 그림과 나누지 않은 그림이 같아야 한다 — 다르면 계획이 겹치는 수명을 놓쳤다. 메모리는 `RT.Frame.transientAliases` 가 센다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_transientAliasing, kDefaultFrameSettings._bAliasTransients != SW_FALSE, "뷰 그래프 트랜지언트 첨부의 텍스처 나눔 (false=첨부마다 텍스처, 진단용)" );
    /**
     * @brief `-gv_vertexPool=0` 이면 씬 메시 정점을 한 풀 버퍼에 모으지 않고 메시마다 자기 정점 버퍼로 그립니다.
     * @details 풀을 켠 이미지와 끈 이미지는 같아야 한다. 다르면 간접 인자의 startVertex 나 SV_VertexID 의 API 차이를 잘못 다룬 것이다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_vertexPool, kDefaultFrameSettings._bVertexPool != SW_FALSE, "씬 메시 정점 풀 (false=메시마다 정점 버퍼, 진단용)" );
    /**
     * @brief `-gv_psoAsync=0` 이면 머티리얼 변형 PSO 를 빌더 스레드가 아니라 렌더 스레드가 셋업 구간에서 모두 만듭니다(옛 동작 - A/B 측정 · 진단용).
     * @details 기본은 빌더 스레드입니다(`MaterialPSOBuilder`). 스레드를 쓸 수 없는 백엔드(GL)는 이 값과 상관없이 렌더 스레드가 프레임당 시간 예산 안에서 만듭니다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_psoAsync, kDefaultFrameSettings._bPSOAsync != SW_FALSE, "머티리얼 변형 PSO 를 빌더 스레드가 만든다 (false=렌더 스레드가 동기로 만듦, A/B 측정용)" );
    /**
     * @brief `-gv_rhiImmediateSubmit=1`: 커맨드 리스트를 프레임 끝에 모아 제출하지 않고 잘릴 때마다 바로 제출합니다.
     * @details 두 모드 모두 기록 순서가 곧 실행 순서다 — 즉시 모드는 제출 '시점'만 다르다. 제출 횟수가 늘어 오버헤드가 크지만 GPU 오류(DEVICE_HUNG, 검증 레이어)가 어느 제출에서 났는지 좁히기 쉽다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_rhiImmediateSubmit, kDefaultFrameSettings._bRHIImmediateSubmit != SW_FALSE, "RHI 커맨드 리스트를 프레임 끝에 모아 제출하지 않고 즉시 제출 (디버깅용, 오버헤드 큼)" );

    namespace
    {
        struct RenderFrameSwitchesInternal
        {
            static void apply( RenderFrameSettings& inoutSettings )
            {
                inoutSettings._bGPUCulling         = gv_gpuCulling ? SW_TRUE : SW_FALSE;
                inoutSettings._meshMorphDebugMode  = gv_meshMorphDebugMode;
                inoutSettings._bDrawMerge          = gv_drawMerge ? SW_TRUE : SW_FALSE;
                inoutSettings._bAsyncCompute       = gv_asyncCompute ? SW_TRUE : SW_FALSE;
                inoutSettings._bAliasTransients    = gv_transientAliasing ? SW_TRUE : SW_FALSE;
                inoutSettings._bVertexPool         = gv_vertexPool ? SW_TRUE : SW_FALSE;
                inoutSettings._bPSOAsync           = gv_psoAsync ? SW_TRUE : SW_FALSE;
                inoutSettings._bRHIImmediateSubmit = gv_rhiImmediateSubmit ? SW_TRUE : SW_FALSE;
            }
        };
    } // namespace

    SW_RENDER_FRAME_SETTINGS_OVERRIDE( DevToolsSwitches, &RenderFrameSwitchesInternal::apply );
} // namespace sw
