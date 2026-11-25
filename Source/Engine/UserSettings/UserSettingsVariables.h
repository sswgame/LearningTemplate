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
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_renderScale ); ///< 3D 렌더 해상도 배율(0.5~1). 화면 출력 뷰의 내부 해상도에 곱하고(`RenderViewCollector`) Present 가 출력 크기로 키운다 — 업스케일러(`gv_upscaler`)가 그 필터를 고른다.
// 렌더 스레드가 쓰는 그래픽 설정(업스케일 필터 · 그림자 맵 크기 · 그림자 필터 · 후처리 품질 · 모션 블러)은 여기 없다 — `RenderFrameSettings.cpp` 의 익명 이름공간에 두고
// 게임 스레드가 프레임마다 뜬 사본(`RenderFrameSettings`)으로만 렌더러에 간다(렌더 쪽이 변수를 직접 읽을 수 없게).
// 아래 층이 읽는 설정 변수는 읽는 쪽에 있다(그 층은 이 헤더를 include 하지 못한다): 시야 거리 `gv_viewDistanceScale`(CameraComponent.cpp),
// 텍스처 품질 `gv_textureMipDrop`(TextureCache.cpp — 사본 `TextureCache::getQualityMipDrop`), 이펙트 품질 `gv_effectsSpawnScale`(VFXComponent.cpp). 단계마다의 값은 스키마 선택지의 `targetValue` 다.

// 게임플레이와 카메라
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_cameraFieldOfView );     ///< 1 인칭과 3 인칭 카메라 시야각(도)입니다. 카메라가 읽습니다.
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_cameraShakeScale );      ///< 카메라 흔들림 배율(0 이면 끔). 카메라 흔들림이 곱한다.
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_cameraHeadBob );            ///< 걷기 머리 흔들림입니다.
SW_EXTERN_GLOBAL_VARIABLE( sw::GlobalString, gv_cameraPreset ); ///< 플레이어가 순환 키로 고른 카메라 프리셋 id 입니다(순환 액션이 있는 카메라 디렉터가 시작할 때 읽고 순환할 때 쓴다). 비면 장면의 시작 프리셋입니다.

// 접근성, UI, 자막
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_colorVisionMode );             ///< 색각 보정입니다(0 끔, 1 적색약, 2 녹색약, 3 청색약). 캔버스(UI)가 겁니다. 렌더러는 화면 출력 뷰의 `g_CameraFrame.z` 에 방식을 싣고, 톤맵 쪽 셰이더(`post_grade.hlsli`)는 쿠킹과 함께 들어온다(UserSettingsConsumers U3).
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_uiScale );                   ///< 게임 UI 배율.
SW_EXTERN_GLOBAL_VARIABLE( sw::GlobalString, gv_uiTheme );          ///< 게임 UI 테마 이름입니다(UISystem 이 테마 목록에서 고르며, 목록에 없으면 목록의 기본 테마를 씁니다).
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_uiTextScale );               ///< 게임 UI 글자 크기 배율입니다(줄여도 글은 12 UI 단위 밑으로 가지 않습니다. UIScaleSettings).
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_uiReduceMotion );               ///< UI 움직임 줄이기입니다. UI 애니메이션, 트윈, 스타일 전환의 길이를 0 으로 만듭니다.
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_subtitles );                    ///< 자막 표시입니다(UISubtitleSubsystem 가 읽으며, 끄면 줄은 받되 화면을 닫습니다).
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_subtitleSize );                ///< 자막 크기입니다(0 작게, 1 보통, 2 크게).
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_subtitleBackgroundOpacity ); ///< 자막 배경 불투명도 0~1.
