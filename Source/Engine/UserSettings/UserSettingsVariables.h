/**
 * @file UserSettingsVariables.h
 * @brief 사용자 설정이 값을 넣는 전역 변수(`target="gv:…"`)입니다. 소비자(렌더러, 카메라, UI, 자막)는 이 변수를 읽습니다.
 * @details 값은 `UserSettingsManager` 가 넣는다. 에디터의 전역 변수 패널이나 `-gv_*` 로 바꿔 볼 수도 있지만 그 값은 사용자 파일에 남지 않는다.
 *          "읽는 곳" 이 아직 없는 변수는 각 줄의 설명에 적었다. 그 기능을 만드는 쪽이 이 변수를 읽으면 메뉴가 바로 반영된다.
 * @note 이 `extern` 은 Engine.dll 안의 코드만 링크됩니다. 게임 · 키트 모듈은 `game::getSubsystem<GlobalVariableManager>().findVariable( "gv_…" )` 로 읽습니다.
 */
#pragma once
#include "Core/GlobalVariable/GlobalVariableManager.h"

// 그래픽: 품질 번들(`graphics.quality`)이 함께 바꾸는 값
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_renderScale );       ///< 3D 렌더 해상도 배율(0.5~1). 화면 출력 뷰의 내부 해상도에 곱하고 Present 가 선형 업샘플한다(`RenderViewCollector`).
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_upscaler );            ///< 업스케일러(0 끔). 켜지면 `gv_renderScale` 은 업스케일러가 정한다.
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_shadowMapResolution ); ///< 그림자 맵 한 변의 텍셀 수입니다. 품질 단계마다의 값은 설정 스키마 `graphics.shadowQuality` 선택지의 `targetValue` 입니다.
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_foliageDensity );    ///< 식생 밀도 배율. 식생 배치가 읽는다(env-world).
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_postQuality );         ///< 후처리 품질 0~3. 파이프라인 패스의 `_minPostQuality` 보다 낮으면 그 패스를 건너뛴다(SSAO · 블룸, `FrameRenderer::executePass`).
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_motionBlur );           ///< 모션 블러. 3D 파이프라인(디퍼드 · 포워드 넷)의 MotionBlur 패스가 읽는다(끄면 원본을 넘긴다).
// 아래 층이 읽는 설정 변수는 읽는 쪽에 있다(그 층은 이 헤더를 include 하지 못한다): 시야 거리 `gv_viewDistanceScale`(CameraComponent.cpp),
// 텍스처 품질 `gv_textureMipDrop`(Texture2D.cpp), 이펙트 품질 `gv_effectsSpawnScale`(VfxComponent.cpp). 단계마다의 값은 스키마 선택지의 `targetValue` 다.

// 게임플레이와 카메라
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_cameraFieldOfView ); ///< 1 인칭과 3 인칭 카메라 시야각(도)입니다. 카메라가 읽습니다.
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_cameraShakeScale );  ///< 카메라 흔들림 배율(0 이면 끔). 카메라 흔들림이 곱한다.
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_cameraHeadBob );        ///< 걷기 머리 흔들림입니다.

// 접근성, UI, 자막
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_colorVisionMode );             ///< 색각 보정입니다(0 끔, 1 적색약, 2 녹색약, 3 청색약). 캔버스(UI)가 겁니다. 렌더러는 화면 출력 뷰의 `g_CameraFrame.z` 에 방식을 싣고, 톤맵 쪽 셰이더(`postgrade.hlsli`)는 쿠킹과 함께 들어온다(UserSettingsConsumers U3).
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_uiScale );                   ///< 게임 UI 배율.
SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_uiTheme );                ///< 게임 UI 테마 이름입니다(UISystem 이 테마 목록에서 고르며, 목록에 없으면 목록의 기본 테마를 씁니다).
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_uiTextScale );               ///< 게임 UI 글자 크기 배율입니다(줄여도 글은 12 UI 단위 밑으로 가지 않습니다. UIScaleUtil).
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_reduceFlashing );               ///< 번쩍임 줄이기입니다(섬광과 화면 깜빡임 효과를 약하게 합니다).
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_uiReduceMotion );               ///< UI 움직임 줄이기입니다. UI 애니메이션, 트윈, 스타일 전환의 길이를 0 으로 만듭니다.
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_subtitles );                    ///< 자막 표시입니다(UISubtitleService 가 읽으며, 끄면 줄은 받되 화면을 닫습니다).
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_subtitleSize );                ///< 자막 크기입니다(0 작게, 1 보통, 2 크게).
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_subtitleBackgroundOpacity ); ///< 자막 배경 불투명도 0~1.

// 개발 도구: Dev 만 스키마(engine/settings/debughud.settings.xml)를 읽는다. Shipping 에는 변수만 있고 읽는 곳이 없다.
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_debugHUD );               ///< 디버그 HUD 표시(DebugHUD).
SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_debugHUDSections ); ///< 섹션 켬과 끔 텍스트입니다(DebugHUDSectionState).
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_debugHUDCorner );        ///< HUD 모서리입니다(0 왼쪽 위, 1 오른쪽 위, 2 왼쪽 아래, 3 오른쪽 아래).
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_debugHUDOpacity );     ///< HUD 불투명도 0.2~1.

// 기동
SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_userSettingsFile ); ///< 사용자 설정 파일 경로 덮어쓰기입니다(자동화용이며 사용자 폴더를 건드리지 않습니다).
