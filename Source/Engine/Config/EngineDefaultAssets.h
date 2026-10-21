/**
 * @file EngineDefaultAssets.h
 * @brief enginedefaultassets.xml 에서 읽는 엔진 셸 경로입니다(Scene / App / FrameRenderer / RHI).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Base/DataPath.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) EngineDefaultAssets — 씬 폴백 머티리얼 · 셸 InputMap · 파이프라인 · 셰이더 폴백
    //    게임플레이 경로는 GameSettings, 에디터 도구 경로는 EditorToolDefaults
    // ------------------------------------------------------------------------------
    /**
     * @brief enginedefaultassets.xml 의 엔진 셸 경로입니다.
     * @details 읽기는 `XMLSerializer` 가 PROPERTY 그래프로 합니다. 필드를 하나 추가하면 읽기가 저절로
     *          따라옵니다(필드마다 손으로 읽으면 한 줄을 빠뜨릴 때 값이 조용히 기본값으로 남습니다).
     *          값의 정본은 `enginedefaultassets.xml` 하나다 — 칸은 `DataPath` 라 헤더에 값을 적을 수 없고, 모든 칸이 `Required` 다.
     *          표는 데이터에서 읽어서만 얻는다(`load` · `EngineDefaultAssetsSubsystem`). 기본 생성한 표를 정본처럼 쓰는 길이 없다.
     *          `Abstract` 는 리플렉션이 생성자를 등록하지 않게 한다 — 리플렉션으로도 만들 수 없다.
     */
    REFLECT( Abstract )
    struct SW_API EngineDefaultAssets
    {
        REFLECT_BODY();
        PROPERTY( Required )
        DataPath _defaultMaterial; ///< 씬 폴백 머티리얼
        PROPERTY( Required )
        DataPath _missingMaterial; ///< 못 읽은 머티리얼 대신 쓰는 마젠타 체커
        PROPERTY( Required )
        DataPath _missingTexture; ///< 못 읽은 텍스처 대신 샘플하는 마젠타 체커
        PROPERTY( Required )
        DataPath _shellInputMap; ///< App 셸 InputMap
        PROPERTY( Required )
        DataPath _uiInputMap; ///< 런타임 UI 의 행동 맵 — 탐색 · 확인 · 뒤로 · 탭(`UISystem`, Shipping 에도 있다)
        PROPERTY( Required )
        DataPath _photoModeInputMap; ///< 포토 모드 자유 카메라의 행동 맵 — 이동 · 오르내림 · 둘러보기 · 기울이기 · 줌 · 노출(`CameraDirectorComponent`, 포토 모드 동안만 읽는다)
        PROPERTY( Required )
        DataPath _userSettingsSchema; ///< 플레이어 옵션 메뉴의 엔진 설정 스키마(`UserSettingsManager`)
        PROPERTY( Required )
        DataPath _telemetrySchema; ///< 엔진 텔레메트리 사건 스키마(`TelemetryService`)
        PROPERTY( Required )
        DataPath _cultureTable; ///< 문화권 표 — 복수형 규칙 · 숫자 · 날짜 형식 · 쓰기 방향 · 글꼴 대체(`LocalizationManager`)
        PROPERTY( Required )
        DataPath _localizationProject; ///< 엔진 문자열(설정 메뉴 등)의 로컬라이제이션 프로젝트
        PROPERTY( Required )
        DataPath _fontCatalog; ///< 글꼴 카탈로그 — 저장소 글꼴 가족 · 시스템 글꼴 가족(`FontService`)
        PROPERTY( Required )
        DataPath _uiScaleSettings; ///< 런타임 UI 배율 규칙 — 해상도 → 배율(`UIScaleSettings`, 팩 설정 `PackEngineSettings` 가 덮어쓴다)
        PROPERTY( Required )
        DataPath _uiThemes; ///< 런타임 UI 테마 목록 — 테마 이름 → 스타일 시트들(`UIThemeCatalog`, 팩 설정이 덮어쓴다)
        PROPERTY( Required )
        DataPath _uiOptionsMenu; ///< 옵션 메뉴 문서. 설정 스키마에서 탭과 행을 만드는 화면(`OptionsMenuScreen`, 팩 설정이 덮어쓴다)
        PROPERTY( Required )
        DataPath _uiPauseMenu; ///< 일시정지 메뉴 문서 — 팩 설정이 `_bUIPauseMenu` 를 켤 때 `UI.Pause` 가 연다(`PauseMenuScreen`)

        // 엔진 UI 문서 — 옵션 메뉴의 행 견본 · 확인 창, 알림 · 자막 화면, 디버그 HUD(`UIEngineDocuments` 로 UI 시스템에 넘긴다)
        PROPERTY( Required )
        DataPath _uiOptionsRowBool; ///< 옵션 메뉴 행 견본: 불리언 설정(체크 상자)
        PROPERTY( Required )
        DataPath _uiOptionsRowInt; ///< 옵션 메뉴 행 견본: 정수 설정
        PROPERTY( Required )
        DataPath _uiOptionsRowFloat; ///< 옵션 메뉴 행 견본: 실수 설정
        PROPERTY( Required )
        DataPath _uiOptionsRowEnum; ///< 옵션 메뉴 행 견본: 고르기 설정(마우스 · 키보드 — 콤보 상자)
        PROPERTY( Required )
        DataPath _uiOptionsRowEnumPad; ///< 옵션 메뉴 행 견본: 고르기 설정(패드 탐색 — 좌우로 넘기기)
        PROPERTY( Required )
        DataPath _uiOptionsRowKeyBinding; ///< 옵션 메뉴 행 견본: 키 바인딩 설정(단추)
        PROPERTY( Required )
        DataPath _uiOptionsRowString; ///< 옵션 메뉴 행 견본: 글 설정
        PROPERTY( Required )
        DataPath _uiSettingsConfirm; ///< 바꾼 설정을 되돌리기 전 카운트다운 확인 창
        PROPERTY( Required )
        DataPath _uiSettingsUnsaved; ///< 저장하지 않은 설정을 두고 닫을 때의 확인 창
        PROPERTY( Required )
        DataPath _uiKeyRebind; ///< 키 바인딩을 받는 창
        PROPERTY( Required )
        DataPath _uiNotifications; ///< 알림 화면(`UINotificationService`)
        PROPERTY( Required )
        DataPath _uiNotificationEntry; ///< 알림 한 줄 견본
        PROPERTY( Required )
        DataPath _uiSubtitles; ///< 자막 화면(`UISubtitleService`)
        PROPERTY( Required )
        DataPath _uiDebugHUD; ///< 디버그 HUD 설정 창(`DebugHUD`, 개발 빌드)
        PROPERTY( Required )
        DataPath _debugHUDSettingsSchema; ///< 디버그 HUD 설정 스키마(`DebugHUD`, 개발 빌드)

        // 엔진 설정 표
        PROPERTY( Required )
        DataPath _uiResolutionPresets; ///< UI 미리보기 해상도 프리셋 표(`UIResolutionPresetTable`)
        PROPERTY( Required )
        DataPath _netEmulationPresets; ///< 네트워크 에뮬레이션 프리셋 표(`NetEmulationPresetTable`)

        // 컴포넌트 기본 머티리얼 — 컴포넌트의 머티리얼 칸이 비면 이것을 쓴다(칸에 적으면 그것)
        PROPERTY( Required )
        DataPath _tileMapMaterial; ///< 타일맵 렌더러(`TileMapRendererComponent`) — 픽셀 아트 스프라이트 머티리얼
        PROPERTY( Required )
        DataPath _foliageMaterial; ///< 풀 · 나무 층(`FoliageComponent`)
        PROPERTY( Required )
        DataPath _terrainMaterial; ///< 지형(`TerrainComponent`)
        PROPERTY( Required )
        DataPath _waterMaterial; ///< 물(`WaterBodyComponent`)

        PROPERTY( Required )
        DataPath _defaultForwardPipeline; ///< 포워드 렌더 파이프라인(프레임 그래프)
        PROPERTY( Required )
        DataPath _defaultDeferredPipeline; ///< 디퍼드 렌더 파이프라인
        PROPERTY( Required )
        DataPath _defaultRenderPass; ///< 기본 렌더 패스 바인드 틀

        PROPERTY( Required )
        DataPath _shaderShadowDepth; ///< 그림자 깊이 패스
        PROPERTY( Required )
        DataPath _shaderForwardLit; ///< 포워드 조명
        PROPERTY( Required )
        DataPath _shaderGBuffer; ///< 디퍼드 G 버퍼 채우기
        PROPERTY( Required )
        DataPath _shaderDeferredLighting; ///< 디퍼드 조명
        PROPERTY( Required )
        DataPath _shaderPostBloom; ///< 후처리 블룸
        PROPERTY( Required )
        DataPath _shaderPostOutline; ///< 후처리 외곽선
        PROPERTY( Required )
        DataPath _shaderFullscreenBlit; ///< 전체 화면 복사
        PROPERTY( Required )
        DataPath _shaderGPUCull; ///< GPU 컬링 · 드로우 커맨드 생성(컴퓨트)
        PROPERTY( Required )
        DataPath _shaderInstanceAnimation; ///< 인스턴스 애니메이션(컴퓨트)
        PROPERTY( Required )
        DataPath _shaderMeshMorph; ///< 모프 타깃(컴퓨트)
        PROPERTY( Required )
        DataPath _shaderMeshSkin; ///< 스키닝(컴퓨트)
        PROPERTY( Required )
        DataPath _shaderInstanceSort; ///< 투명 인스턴스 바이토닉 정렬(컴퓨트)
        PROPERTY( Required )
        DataPath _shaderLightGrid; ///< 라이트 그리드 생산(컴퓨트) — 클러스터드 라이팅의 빛 목록
        PROPERTY( Required )
        DataPath _shaderVfxSimulation; ///< GPU 방출기의 입자 시뮬레이션(컴퓨트)
        PROPERTY( Required )
        DataPath _shaderVfxMeshInstance; ///< GPU 메시 입자의 인스턴스 쓰기(컴퓨트)
        PROPERTY( Required )
        DataPath _shaderDepthHistory; ///< 뷰의 깊이 히스토리 복사(풀스크린) — GPU 입자의 깊이 충돌이 읽는다
        PROPERTY( Required )
        DataPath _shaderFullscreenTriangle; ///< 전체 화면 삼각형 정점 셰이더
        PROPERTY( Required )
        DataPath _shaderSsao; ///< SSAO
        PROPERTY( Required )
        DataPath _shaderTaa; ///< TAA
        PROPERTY( Required )
        DataPath _shaderTonemap; ///< 톤 매핑
        PROPERTY( Required )
        DataPath _shaderSky; ///< 하늘 그러데이션(스카이 패스) — 깊이 1 의 풀스크린 삼각형
        PROPERTY( Required )
        DataPath _shaderToon; ///< 셀 셰이딩 머티리얼 셰이더 — 메시 외곽선 패스의 기본 셰이더이기도 하다
        PROPERTY( Required )
        DataPath _shaderCanvas; ///< 화면 2D(UI · 월드 글자) 사각형 — 사각형 하나 = 인스턴스 하나, SDF 모양 · 글리프
        PROPERTY( Required )
        DataPath _shaderSprite2D; ///< 스프라이트(2D) 머티리얼 셰이더 — 머티리얼 없이 쿠킹 목록에도 든다
        PROPERTY( Required )
        DataPath _shaderSprite2DLit; ///< 빛 받는 스프라이트(2D 빛 · 그림자) 머티리얼 셰이더 — 머티리얼 없이 쿠킹 목록에도 든다
        PROPERTY( Required )
        DataPath _shaderParticle; ///< 파티클 빌보드(Vfx) — 방출기 하나 = 그리기 하나, 입자 하나 = 인스턴스 하나. 투명 패스 PSO 를 물려받는다

        // RHI · 렌더 테스트의 프로브 셰이더 — 런타임은 쓰지 않지만 Shipping 테스트가 쿠킹된 바이너리를 읽으므로 쿠킹 목록에 든다.
        PROPERTY( Required )
        DataPath _shaderProbeProvokingVertex; ///< 프로브: 플랫 보간이 첫 정점 값을 쓰는가(RHIDeviceTest)
        PROPERTY( Required )
        DataPath _shaderProbeInstanceSlot; ///< 프로브: 인스턴스 슬롯 스트림이 네 API 에서 같은가(RHIDeviceTest)
        PROPERTY( Required )
        DataPath _shaderProbeSampleCompute; ///< 프로브: 컴퓨트 쿠킹 · 리플렉션(ShaderTest)
        PROPERTY( Required )
        DataPath _shaderProbeComputeTextureWrite; ///< 프로브: 컴퓨트 RW 텍스처 쓰기 · 기본이 아닌 진입점(RHIDeviceTest)
        PROPERTY( Required )
        DataPath _shaderProbeWaterWave; ///< 프로브: 거스트너 파도 셰이더 식이 CPU 와 같은가(RenderPassGPUTest)

        /** @brief 만드는 열쇠입니다 — 읽기(`load`)와 소유 서브시스템만 만든다. 그래서 `EngineDefaultAssets{}` 는 컴파일되지 않는다. */
        class ConstructKey
        {
            friend struct EngineDefaultAssets;
            friend class EngineDefaultAssetsSubsystem;
            ConstructKey() {} // NOLINT(modernize-use-equals-default) — `= default` 면 집합체라 `{}` 가 private 를 비켜 간다
        };

        /** @brief 읽지 않은 표입니다. 값은 `loadFromResource` 만 넣는다. */
        explicit EngineDefaultAssets( ConstructKey ) {}

        /**
         * @brief 리소스(XML)에서 읽은 표를 만듭니다. 빈 경로면 path::kEngineDefaultAssets 를 씁니다.
         * @return 읽지 못했으면 nullptr 입니다(이유는 오류 로그) — 대신 쓸 내장 값은 없다. 테스트 · 쿠커가 데이터에서 읽는 창구다.
         */
        [[nodiscard]] static unique_ptr<EngineDefaultAssets> load( string_view assetRelativePath = {} );

        /**
         * @brief 리소스 경로(XML)에서 이 표를 채웁니다. 빈 경로면 path::kEngineDefaultAssets 를 씁니다.
         * @return 파일을 못 읽었거나 `Required` 칸이 빠졌거나 (개발 빌드에서) 그 칸이 가리키는 파일이 없으면 false 입니다. 대신 쓸 내장 값은 없습니다.
         */
        [[nodiscard]] bool loadFromResource( string_view assetRelativePath = {} );
    };
} // namespace sw
