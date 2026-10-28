/**
 * @file RenderPassAsset.h
 * @brief 렌더 패스 XML 에셋(어태치먼트 템플릿)과 파이프라인 패스 서술 타입을 정의합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Common/Common.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Resource/ResourceID.h"

namespace sw
{
    /**
     * @enum RenderPassType
     * @brief 파이프라인 XML 의 `_type` 이 가리키는 패스 종류입니다.
     * @details 디스패치와 PSO 생성이 같은 값을 본다. 주의: 두 곳이 각자 문자열을 비교하면 받아 주는 표기가 갈려,
     *          디스패치는 되는데 PSO 는 desc 를 찾지 못하고 기본 포맷으로 만들어진다. 리플렉션이
     *          문자열과 값의 변환을 제공하므로 파서도 검증도 이 테이블만 보면 된다.
     * @note 이름은 널리 쓰이는 표기 하나로 통일한다. 아직 개발 중이라 여러 표기를
     *       받아 줄 이유가 없고, 표기가 갈리는 순간 "어느 쪽으로 적었나" 를 매번 확인해야 한다.
     *       XML 에 적히는 철자가 곧 열거자 이름이고, 여기에 없는 표기는 Invalid 로 파싱되어
     *       RenderPipelineAsset::validate 가 잡는다.
     * @note 이름을 바꾸면 XML 을 새 이름으로 다시 쓴다. `ENUM( ValueAlias = "Old:New" )` 는 실제 게임 데이터가 생겨
     *       다시 쓸 수 없게 된 뒤에 쓰는 수단이다.
     */
    ENUM()
    enum class RenderPassType : uint32
    {
        Invalid = 0, ///< 알 수 없는 표기이며 검증에서 오류로 보고한다.

        Shadow,
        DepthPrepass,
        ForwardOpaque,
        GBuffer,
        Lighting,
        Transparent,
        SSAO,
        Bloom,
        /// @brief 블룸의 밝은 부분을 반해상도 첨부(`BloomBright`)에 구합니다(postbloom.hlsl 의 `SW_PASS_BLOOM_DOWNSAMPLE`). Bloom 패스가 그것을 업샘플해 더합니다.
        BloomDownsample,
        Outline,
        TAA,
        Tonemap,
        Present,
        /// @brief 인버티드 헐(inverted hull, 뒤집은 껍질) 외곽선입니다. 씬 메시 중 퍼뮤테이션에 `MATERIAL_OUTLINE` 이 있는 배치만 노멀 방향으로 부풀려 앞면을 컬링해 그립니다(toon.hlsl).
        MeshOutline,
        /**
         * @brief 하늘(`SkyComponent`)입니다. 불투명 패스 뒤 · 반투명 앞에 풀스크린 삼각형을 깊이 1 에 그려, 아무것도 그리지 않은 곳(깊이 클리어 1)만 칠합니다(sky.hlsl).
         *        깊이는 테스트만 하고(LessEqual) 쓰지 않으므로 `_depthAttachment` 가 있어야 합니다(검증). 씬에 하늘이 없으면 그리지 않습니다(배경은 클리어 색).
         */
        Sky,
        /**
         * @brief 화면 2D(UI, 월드 글자)를 주 시점 출력(백버퍼, 게임 뷰 RT, 스크린샷 캡처)에 **기존 내용을 유지한 채(Load)** 그립니다. Present(톤맵) 뒤에 두고,
         *        Swapchain 을 쓰는 마지막 패스여야 합니다(검증). 추가 뷰에서는 그리지 않습니다(UI 는 주 시점에만 있습니다).
         */
        Canvas,
        /**
         * @brief 디퍼드에서 G버퍼로 나타낼 수 없는 머티리얼(퍼뮤테이션에 `MATERIAL_FORWARD_ONLY` — 툰의 계단 · 림 · 맷캡)만 포워드 셰이딩으로 그리는 불투명 패스입니다.
         *        G버퍼 패스는 그 배치를 빼므로 조명 패스 뒤에 두고 같은 장면 깊이를 테스트 · 쓰기 합니다(유니티 URP 디퍼드의 `UniversalForwardOnly` 패스와 같은 자리).
         */
        ForwardOnlyOpaque,
        /**
         * @brief 모듈(게임 · 키트)이 이름으로 등록한 풀스크린 패스입니다. 셰이더는 패스의 `_shaderPath`(필수), 이름은 `_modulePass`(필수)입니다.
         * @details 모듈은 **데이터만** 넘깁니다 — 패스 상수 이름 · 값(`ModuleRenderPassRegistry`). 커맨드 리스트 · 트랜지언트 풀 · `FramePassContext` 는 모듈 경계를
         *          넘지 않고, 렌더 스레드에서 모듈 코드가 돌지 않습니다. 그래서 핫 리로드로 모듈 DLL 이 내려가도 렌더 스레드가 그 코드를 부를 수 없습니다.
         *          입력은 선언한 첨부를 역할로 바인딩하고 첫 출력에 그립니다(일반 풀스크린 패스와 같은 실행). 등록되지 않은 이름이면 오류를 한 번 남기고 그리지 않습니다.
         */
        Module,
        /**
         * @brief 피사계 심도입니다. HDR 색(`SourceColor`)과 장면 깊이(`SceneDepth`)를 읽어 초점면에서 먼 픽셀을 착란원 반지름만큼 흐린 같은 형식의 색을 냅니다(depthoffield.hlsl).
         *        렌즈 값은 뷰 카메라의 `CameraDepthOfFieldValues` 이고 패스 상수 `g_CameraLens` 로 받습니다. 조리개 0 · 직교 · 후처리를 끈 뷰는 패스를 기록하지 않고
         *        출력 이름이 원본을 가리킵니다(`RenderPassTraitFlag::kBypassesToSource` — 출력 타깃도 처음 켜질 때 만듭니다). 디퍼드는 TAA 와 Tonemap 사이, 포워드는 후처리 체인(Present) 앞에 둡니다.
         */
        DepthOfField,
        /**
         * @brief 카메라 모션 블러입니다. 깊이로 복원한 월드 위치를 지난 프레임 뷰-투영(`g_PrevViewProj`)으로 다시 투영해 화면 속도를 구하고, 그 방향으로 원본을 섞습니다(motionblur.hlsl).
         *        사용자 설정 `graphics.motionBlur`(`gv_motionBlur`)를 끄거나 후처리를 끈 뷰, 카메라가 서 있거나 컷인 프레임(속도가 모두 0)은
         *        피사계 심도와 같이 패스를 기록하지 않고 출력 이름이 원본을 가리킵니다(`kBypassesToSource`).
         *        움직이는 물체의 속도 버퍼는 아직 없다(카메라 움직임만 흐린다). 디퍼드는 피사계 심도와 Tonemap 사이에, 포워드는 피사계 심도와 후처리 체인(Present) 사이에 둡니다.
         */
        MotionBlur,
        /**
         * @brief 인스턴스 강조 외곽선입니다(highlightoutline.hlsl). 강조 방식이 Outline 인 인스턴스(`GPUInstance::_highlight`)만 뒤집은 껍질로 깊이 테스트해 그립니다.
         *        상호작용 대상의 외곽선(언리얼 CustomDepth + 외곽선 후처리, 유니티 Render Objects 의 자리)이며 불투명 패스 뒤에 둡니다.
         */
        HighlightOutline,
        /** @brief 감각 모드 외곽선입니다. 강조 방식이 Sense 인 인스턴스만 깊이 테스트 없이 그려 벽 너머에서도 보입니다(반투명 뒤, 톤맵 앞에 둡니다). */
        HighlightSense,
        /**
         * @brief 높이 · 거리 지수 안개입니다(fog.hlsl). 장면 깊이로 시선 위 광학 두께를 구해 선언한 컬러 출력 **위에 블렌드**(프리멀티플라이: 안개색 × 불투명도 + 장면 × (1 - 불투명도))합니다 —
         *        그래서 입력은 장면 깊이뿐이고 출력은 앞 패스가 칠한 색 첨부입니다(복사 첨부를 따로 두지 않는다). 불투명 · 하늘 · 반투명 뒤, 후처리 앞에 둡니다.
         *        씬에 안개(`FogComponent` · 물속)가 없거나 후처리를 끈 뷰 · 표면 값 보기에서는 그리지 않습니다(전용 실행: `FrameRenderer::executeFogPass`).
         *        볼륨 안개를 켠 씬은 볼륨(froxel, `RenderPassType::VolumetricFog` 컴퓨트)의 산란과 투과율을 같은 블렌드로 더합니다.
         */
        Fog,
        /**
         * @brief 장면 색 사본입니다. 원본 색(`SourceColor`)을 선언한 컬러 출력(`SceneColorCopy` 역할)으로 그대로 복사합니다 — 셰이더 · PSO 가 없습니다(`FrameRenderer::executeSceneColorCopyPass`).
         *        반투명 패스는 장면 색에 섞으며 쓰므로 그 색을 읽을 수 없다. 반투명 패스가 이 출력을 같은 역할의 입력으로 선언하면 물의 굴절이 읽습니다(`g_SceneColorCopyIndex`).
         *        하늘 뒤 · 반투명 앞에 둡니다(언리얼의 Scene Color 사본 · 유니티 URP 의 Opaque Texture 와 같은 자리).
         */
        SceneColorCopy,

        // --- 엔진 내부 PSO 슬롯. 파이프라인 XML 에는 나올 수 없다(검증이 거부한다). ---
        ForwardOpaqueNoDepthWrite,
        GPUCull,
        /// @brief GPUScene 인스턴스 애니메이션 컴퓨트(instanceanimation.hlsl)입니다. 인스턴스마다 다른 각속도로 회전시킵니다.
        InstanceAnimation,
        /// @brief 배치 안의 가시 인스턴스를 CPU 정렬 순서(인스턴스 번호)로 되돌리는 컴퓨트(instancesort.hlsl)입니다. 투명 블렌딩 순서를 맞춥니다.
        InstanceSort,
        /// @brief 레스트 정점을 읽어 변형 결과를 쓰는 컴퓨트(meshmorph.hlsl)입니다. 정점 셰이더가 그 결과를 풀링합니다.
        MeshMorph,
        /// @brief 스킨드 메시의 바인드 포즈 정점을 본 팔레트로 블렌딩해 모프 풀의 스킨 구간에 쓰는 컴퓨트(meshskin.hlsl)입니다.
        MeshSkin,
        /// @brief 뷰마다의 라이트 그리드(클러스터드 라이팅의 빛 목록)를 만드는 컴퓨트(lightgrid.hlsl)입니다. 그룹 하나가 뷰 하나입니다.
        LightGrid,
        /// @brief GPU 방출기의 입자 시뮬레이션 컴퓨트(vfxsimulate.hlsl)입니다. 그룹 하나가 방출기 하나이고 간접 인자를 씁니다(`VfxGPUSimulation`).
        VfxSimulation,
        /// @brief GPU 메시 입자를 메시 인스턴스 배치의 인스턴스 칸에 쓰는 컴퓨트(vfxmeshinstance.hlsl)입니다. 인스턴스 애니메이션 · 컬링 앞에 돈다.
        VfxMeshInstance,
        /// @brief 뷰의 지난 프레임 장면 깊이를 R32 실수 RT(깊이 히스토리)로 옮기는 풀스크린(depthhistory.hlsl)입니다. 컴퓨트 프리패스 리스트에서 그린다.
        DepthHistory,
        /**
         * @brief 자동 노출의 측정 · 적응 풀스크린(autoexposure.hlsl)입니다. HDR 원본의 평균 로그 휘도를 재어 지난 EV 에서 목표 EV 로 다가간 값을 뷰의 1x1 R32 실수 RT 에 씁니다.
         *        톤맵하는 패스(Tonemap, 후처리 체인 Present)가 자기 그리기 바로 앞에 같은 리스트로 기록한다(`FrameRenderer::recordAutoExposure`).
         */
        AutoExposure,
        /// @brief FSR 1 의 EASU(fsr1.hlsl) — Present 원본(배율 크기)을 뷰포트 크기 중간 RT 로 키웁니다. Present 가 렌더 패스를 열기 전에 기록한다(`FrameRenderer::recordUpscaleEasu`).
        UpscaleEasu,
        /// @brief FSR 1 의 RCAS(fsr1.hlsl 의 `SW_PASS_FSR_RCAS`) — 중간 RT 를 선명하게 해 Present 대상에 그립니다. 대상 포맷마다 PSO 가 다른 출력 패스다(Present 와 같다).
        UpscaleRcas,
        /**
         * @brief 가림 타일 컴퓨트(occlusiontiles.hlsl)입니다. 뷰의 깊이 히스토리(지난 프레임 장면 깊이)를 16 px 타일마다 최대 깊이 하나로 줄여 구조 버퍼에 씁니다.
         *        컬링(gpucull.hlsl)이 경계 구를 그 프레임의 뷰-투영으로 옮겨 덮는 타일보다 멀면 가려졌다고 본다(`FrameRenderer::recordOcclusionTiles`).
         */
        OcclusionTiles,
        /**
         * @brief 볼륨 안개 컴퓨트(volumetricfog.hlsl)입니다. 뷰의 froxel 격자(화면 칸 × 깊이 조각)마다 안개 밀도와 라이트 산란(방향광 + 라이트 그리드의 점광 · 스폿)을 구하고
         *        깊이 방향으로 누적해 (산란 rgb, 투과율)을 구조 버퍼에 쓴다. 안개 패스(`Fog`)가 그 버퍼를 읽는다(`FrameRenderer::dispatchVolumetricFog`).
         */
        VolumetricFog,
        /**
         * @brief 실시간 라이트 프로브 갱신 컴퓨트(probeupdate.hlsl)입니다. 프로브마다 점유 복셀(`SceneVoxelGrid`)로 광선을 쏘아 SH9 기록을 갱신하고, 소비 셰이더의 프로브 원소로 바꿔
         *        프로브 버퍼(t13)에 쓴다(`RealtimeProbeUpdater`). 라이트 업로드 뒤 · 조명 패스 앞에 한 번 돈다.
         */
        ProbeUpdate,
    };

    /**
     * @brief 파이프라인 출력에 쓰는 예약어입니다. 디바이스가 주는 백버퍼를 가리킵니다.
     * @details `_attachments` 에 선언되지 않는 유일한 출력이라 검증에서 예외로 다룬다.
     */
    inline constexpr const utf8* kSwapchainOutputName = "Swapchain";

    /** @brief 파이프라인 XML 의 `_type` 으로 쓸 수 있는 값인지 확인합니다(내부 슬롯과 Invalid 는 제외합니다). */
    constexpr bool isPipelinePassType( RenderPassType type )
    {
        return type != RenderPassType::Invalid && static_cast<uint32>( type ) <= static_cast<uint32>( RenderPassType::SceneColorCopy );
    }

    /// @brief 렌더 패스 어태치먼트 하나(이름, 포맷, 클리어 색, 클리어 여부)입니다.

    REFLECT()
    struct RenderPassAttachment
    {
        REFLECT_BODY();
        PROPERTY()
        string _name = "ColorAttachment0";

        PROPERTY()
        string _format = "R8G8B8A8_UNORM";

        PROPERTY()
        float4 _clearColor = { 0.1f, 0.2f, 0.3f, 1.0f };

        PROPERTY()
        bool _bClear{ true };

        /**
         * @brief 이 첨부가 패스에 바인딩될 때의 역할입니다(`RenderPassInputRole` 이름: GBufferAlbedo, GBufferNormal, GBufferMaterial, ShadowMap, AmbientOcclusion,
         *        BloomBright, SceneDepth, SourceColor). 비어 있으면 원본 이름(GBufferAlbedo 등)과 포맷으로 정합니다.
         * @details 역할을 **이름으로만** 정하면 G버퍼, 그림자 맵, AO 첨부의 이름을 바꿀 때 Lighting 의 입력 계약이 깨진다(역할 없는 컬러는
         *          모두 SourceColor). 언리얼 RDG 는 패스 파라미터 구조체의 멤버로, 유니티 RenderGraph 는 셰이더 프로퍼티 이름으로 텍스처를 바인딩한다.
         *          어느 쪽도 텍스처의 이름이 바인딩을 정하지 않는다. 여기서는 첨부가 자기 역할을 선언하고, 이름은 파이프라인이 자유롭게 짓는다.
         */
        PROPERTY( SkipIfEmpty )
        string _role;

        /**
         * @brief 첨부 크기는 프레임 크기를 이 값으로 나눈 값(올림)입니다. 1(기본), 2, 4 만 받습니다(`RenderPipelineAsset::validate`).
         * @details 반해상도 후처리(블룸, SSAO)처럼 프레임보다 작게 그려도 되는 타깃용이다(언리얼 RDG 에서 텍스처 Extent 를 뷰 크기의
         *          분수로 잡는 것에 해당한다). 한 패스의 출력(컬러, 깊이)은 모두 같은 나눗수여야 한다. 렌더 패스의 타깃은 크기가 같아야 하기 때문이다.
         *          패스는 출력 첨부의 크기로 뷰포트를 잡고, 입력은 UV 로 읽으므로 크기가 달라도 된다(선형 샘플러가 늘리고 줄인다).
         */
        PROPERTY()
        uint32 _resolutionDivisor{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 첨부 나눗수(`RenderPassAttachment::_resolutionDivisor`)가 받는 값인지 반환합니다(1, 2, 4). */
    constexpr bool isSupportedResolutionDivisor( uint32 divisor )
    {
        return divisor == 1u || divisor == 2u || divisor == 4u;
    }

    /**
     * @brief 렌더 파이프라인 그래프 안의 패스 노드 하나입니다.
     * @details 그래프 연결(입력과 출력)과 선택적인 PSO 설정(셰이더, 엔트리, 상태, 퍼뮤테이션)을 담는다.
     *          RenderPass XML 은 어태치먼트만 다루고, 이 필드들은 파이프라인의 패스에 둔다.
     */
    REFLECT()
    struct RenderGraphPassDesc
    {
        REFLECT_BODY();
        PROPERTY()
        string _name = "Pass";

        PROPERTY()
        string _type = "Opaque";

        PROPERTY()
        vector<string> _listInput;

        PROPERTY()
        vector<string> _listOutput;

        /**
         * @brief 이 패스가 바인딩할 뎁스 첨부 이름입니다. **비어 있으면 뎁스 없이 엽니다.**
         * @details 패스 타입마다 코드에 박으면("GBuffer 면 SceneDepth") "이 패스는 일부러 뎁스를 쓰지 않는다" 를
         *          표현할 방법이 없어, DepthPrepass 를 넣거나 빼는 구성을 바꾸려면 엔진 코드를 고쳐야 한다.
         *          선언으로 두면 파이프라인 XML 만으로 바뀐다.
         * @note 읽기와 쓰기 여부는 `_listInput` 과 `_listOutput` 이 따로 말해 준다. Transparent 는 SceneDepth 를
         *       **입력으로 읽으면서** 뎁스로 바인딩한다(테스트만 하고 쓰지 않는다). 그래서 출력에서
         *       유추하지 않고 별도 필드로 둔다.
         */
        PROPERTY( SkipIfEmpty )
        string _depthAttachment;

        /**
         * @brief `_depthAttachment` 를 intern 해 둔 값입니다. XML 로드 시 RenderPipelineAsset 가 채웁니다.
         * @details 직렬화 대상이 아니다(원본 철자는 `_depthAttachment` 가 가지고 있다). `_resolvedType` 과
         *          같은 이유로 한 번만 해석해 둔다. 이 이름은 패스마다 어태치먼트 클리어 여부를
         *          판정하는 데 쓰이는데, 매 프레임 다시 intern 하면 그때마다 전역 문자열 레지스트리의
         *          샤드 뮤텍스를 잡게 된다.
         */
        hashed_string _resolvedDepthAttachment;

        /**
         * @brief 해석한 첨부 하나(이름, 역할)입니다. 입력 목록과 컬러 출력 목록이 같은 구조를 씁니다.
         * @details 역할 값은 `RenderPassInputRole` 인데 이 헤더가 그 enum 을 모르므로(계약 헤더가 이 헤더를 include 한다) 정수로 둔다.
         *          역할은 첨부의 `_role` 선언, 원본 이름, 포맷 순으로 정한다(`resolveRenderPassInputRole`).
         */
        struct ResolvedAttachment
        {
            hashed_string _attachment;
            uint8         _role{ 0 };
        };

        /**
         * @brief `_listInput` 하나하나의 (첨부 이름, 역할)입니다. XML 로드 시 RenderPipelineAsset 가 채웁니다.
         * @details 직렬화 대상이 아니다. 실행은 이 목록을 그대로 바인딩하고(역할 이름이 곧 셰이더의 `g_<Role>Index` 다),
         *          검증은 패스 타입의 계약(RenderPassInputSignature)과 대조한다. 둘이 같은 해석을 본다.
         */
        vector<ResolvedAttachment> _listResolvedInput;

        /** @brief `_listOutput` 을 intern 해 둔 값입니다. 풀스크린 패스가 "첫 번째로 존재하는 출력" 을 타깃으로 고릅니다. */
        vector<hashed_string> _listResolvedOutput;

        /**
         * @brief `_listOutput` 중 **컬러 첨부**만 선언 순서대로 모은 목록입니다(뎁스 포맷, 스왑체인, 선언되지 않은 이름은 빠집니다). 역할도 함께 가집니다.
         *        XML 로드 시 RenderPipelineAsset 가 채웁니다.
         * @details 지오메트리 패스(ForwardOpaque, GBuffer, Transparent)는 이 목록에서 컬러 타깃을 고른다. GBuffer 는 역할(GBufferAlbedo,
         *          GBufferNormal, GBufferMaterial)로 고르고, 역할이 없으면 선언 순서([0] 알베도, [1] 노멀, [2] 머티리얼)로 고른다. 주의: 첨부 이름(SceneColor, GBufferAlbedo 등)을 코드에
         *          박으면 다른 이름을 쓰는 파이프라인에서 없는 첨부를 열고, 없는 첨부의 핸들 0은 백버퍼라 화면에 그린다.
         */
        vector<ResolvedAttachment> _listResolvedColorOutput;

        /**
         * @brief `_type="Module"` 패스가 가리키는 모듈 패스 이름입니다(`ModuleRenderPassRegistry` 의 키). 그 타입에서는 필수이고 다른 타입에 적으면 검증 오류입니다.
         */
        PROPERTY( SkipIfEmpty )
        string _modulePass;

        /** @brief `_modulePass` 를 intern 해 둔 값입니다. XML 로드 시 RenderPipelineAsset 가 채웁니다(직렬화하지 않음). PSO 와 상수의 키입니다. */
        hashed_string _resolvedModulePass;

        /** @brief HLSL 경로(engine/... 또는 common/...)입니다. 비면 FrameRenderer 가 패스 타입의 기본 셰이더를 씁니다. */
        PROPERTY( SkipIfEmpty )
        string _shaderPath;

        /** @brief 정점 셰이더 진입점입니다. 비면 스테이지 기본값을 씁니다(`resolveEntryPoint`, getShaderStageInfo 테이블). */
        PROPERTY( SkipIfEmpty )
        string _vertexEntryPoint;

        /** @brief 픽셀 셰이더 진입점입니다. 비면 스테이지 기본값입니다. */
        PROPERTY( SkipIfEmpty )
        string _pixelEntryPoint;

        PROPERTY( SkipIfEmpty )
        string _computeEntryPoint;

        PROPERTY( SkipIfEmpty )
        string _geometryEntryPoint;

        PROPERTY( SkipIfEmpty )
        string _hullEntryPoint;

        PROPERTY( SkipIfEmpty )
        string _domainEntryPoint;

        PROPERTY( SkipIfEmpty )
        string _meshEntryPoint;

        PROPERTY( SkipIfEmpty )
        string _amplificationEntryPoint;

        /** @brief 셰이더 매크로(퍼뮤테이션)입니다. "NAME" 또는 "NAME=VALUE" 형태로 적습니다. */
        PROPERTY()
        vector<string> _listPermutation;

        PROPERTY()
        string _cullMode = "Back"; ///< None, Front, Back 중 하나

        /**
         * @brief `_type` 을 해석한 값입니다. XML 로드 시 RenderPipelineAsset 가 채웁니다.
         * @details 직렬화 대상이 아니다(원본 철자는 `_type` 이 그대로 가지고 있다). 디스패치와 PSO
         *          생성이 같은 값을 보게 하려고 한 번만 해석해 둔다(두 곳이 각자 문자열을 비교하면 받아 주는
         *          표기가 갈려 어긋난다).
         */
        RenderPassType _resolvedType{ RenderPassType::Invalid };

        PROPERTY()
        bool _bEnableDepthTest{ true };

        PROPERTY()
        bool _bEnableDepthWrite{ true };

        PROPERTY()
        bool _bEnableBlend{ false };

        /**
         * @brief 이 패스가 도는 최소 후처리 품질(`gv_postQuality`, 사용자 설정 graphics.postQuality 의 선택지 순번 0 낮음 ~ 3 최고)입니다. 0 이면 늘 돕니다.
         * @details 품질이 이보다 낮으면 후처리를 끈 뷰와 같이 건너뛴다(원본을 넘기거나 기본 클리어 — SSAO 는 가림 없음). 그래서 건너뛸 수 있는 타입
         *          (`RenderPassTraitFlag::kSkipsWithoutPostProcess`)만 0 보다 큰 값을 받는다(검증이 막는다). 언리얼 sg.PostProcessQuality 에 해당한다.
         */
        PROPERTY()
        int32 _minPostQuality{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief RHI 렌더 패스 템플릿의 어태치먼트 묶음 서술입니다.
     * 프레임 구성(패스 그래프)은 RenderPipelineDesc 가 담당합니다(RenderPipelineAsset.h).
     */
    REFLECT()
    struct RenderPassDesc
    {
        REFLECT_BODY();
        PROPERTY()
        string _name = "DefaultMainPass";

        PROPERTY()
        vector<RenderPassAttachment> _listAttachment;
    };
} // namespace sw

namespace sw
{
    /// @brief RenderPass XML 에셋(어태치먼트 템플릿)입니다.
    class SW_API RenderPassAsset
    {
    public:
        /** @brief 빈 렌더 패스 서술을 만듭니다. */
        RenderPassAsset() = default;
        /** @brief 가상 소멸자입니다. */
        virtual ~RenderPassAsset() = default;

        /** @brief 복사를 금지합니다. */
        RenderPassAsset( const RenderPassAsset& ) = delete;
        /** @brief 대입을 금지합니다. */
        RenderPassAsset& operator=( const RenderPassAsset& ) = delete;

        /** @brief XML 파일에서 렌더 패스 디스크립터를 로드합니다. */
        [[nodiscard]] bool loadFromXMLFile( const ResourceID& resourceID );

        /** @brief 렌더 패스 디스크립터를 XML 파일로 저장합니다. */
        [[nodiscard]] bool saveToXMLFile( string_view assetRelativePath ) const;

        /** @brief XML 로드를 비동기 작업으로 예약합니다. */
        TaskHandle loadFromXMLFileAsync( string_view assetRelativePath );

        const RenderPassDesc& getDesc() const { return _desc; }
        RenderPassDesc&       getDesc() { return _desc; }

    private:
        /** @brief 비동기 로드 태스크 본문입니다. TaskArgs 에는 this 와 경로 문자열이 들어 있습니다. */
        static void loadFromXMLFileAsyncJob( const TaskArgs& args );

    private:
        RenderPassDesc _desc;
    };
} // namespace sw
