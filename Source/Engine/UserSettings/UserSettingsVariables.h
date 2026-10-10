/**
 * @file UserSettingsVariables.h
 * @brief 사용자 설정이 값을 넣는 전역 변수(`target="gv:…"`)입니다. 소비자(렌더러 · 카메라 · UI · 자막)는 이 변수를 읽습니다.
 * @details 값은 `UserSettingsManager` 가 넣습니다. 에디터의 전역 변수 패널 · `-gv_*` 로 바꿔 볼 수도 있지만 그 값은 사용자 파일에 남지 않습니다.
 *          "읽는 곳" 이 아직 없는 것은 각 줄의 설명에 적었습니다 — 그 기능을 만드는 쪽이 이 변수를 읽으면 메뉴가 바로 닿습니다.
 * @note 이 `extern` 은 Engine.dll 안의 코드만 링크됩니다. 게임 · 키트 모듈은 `engine::getGlobalVariableManager().findVariable( "gv_…" )` 로 읽습니다.
 */
#pragma once
#include "Core/GlobalVariable/GlobalVariableManager.h"

// 그래픽 — 품질 묶음(`graphics.quality`)이 함께 바꾸는 값
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_renderScale );       ///< 3D 렌더 해상도 배율(0.5~1). 아직 렌더러가 읽지 않는다.
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_upscaler );            ///< 업스케일러(0 끔). 켜지면 `gv_renderScale` 은 업스케일러가 정한다.
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_shadowQuality );       ///< 그림자 품질 0~3. 그림자 맵 한 변 1024 · 1536 · 2048 · 4096(FrameRenderer::getShadowMapResolution).
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_viewDistanceScale ); ///< 시야 거리 배율. 아직 컬링이 읽지 않는다.
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_foliageDensity );    ///< 식생 밀도 배율. 식생 배치가 읽는다(env-world).
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_postQuality );         ///< 후처리 품질 0~3.
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_textureQuality );      ///< 텍스처 품질 0~3(밉 바이어스). 텍스처 스트리밍이 생기면 읽는다.
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_effectsQuality );      ///< 이펙트 품질 0~3. 파티클이 읽는다.
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_motionBlur );           ///< 모션 블러.

// 게임플레이 · 카메라
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_cameraFieldOfView ); ///< 1 인칭 · 3 인칭 카메라 시야각(도). 카메라가 읽는다.
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_cameraShakeScale );  ///< 카메라 흔들림 배율(0 이면 끔). 카메라 흔들림이 곱한다.
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_cameraHeadBob );        ///< 걷기 머리 흔들림.

// 접근성 · UI · 자막
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_colorVisionMode );             ///< 색각 보정 0 끔 · 1 적색약 · 2 녹색약 · 3 청색약. 캔버스(UI)가 읽는다 — 톤맵 쪽은 아직(함수는 colorvision.hlsli).
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_uiScale );                   ///< 게임 UI 배율.
SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_uiTheme );                ///< 게임 UI 테마 이름(UISystem 이 테마 목록에서 고른다 — 목록에 없으면 목록의 기본).
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_uiTextScale );               ///< 게임 UI 글자 크기 배율(줄여도 글은 12 UI 단위 밑으로 가지 않는다 — UIScaleUtil).
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_reduceFlashing );               ///< 번쩍임 줄이기(섬광 · 화면 깜빡임 효과를 약하게).
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_uiReduceMotion );               ///< UI 움직임 줄이기 — UI 애니메이션 · 트윈 · 스타일 전환의 길이를 0 으로.
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_subtitles );                    ///< 자막 표시(UISubtitleService — 끄면 줄은 받되 화면을 닫는다).
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_subtitleSize );                ///< 자막 크기 0 작게 · 1 보통 · 2 크게.
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_subtitleBackgroundOpacity ); ///< 자막 배경 불투명도 0~1.

// 개발 도구 — Dev 만 스키마(engine/settings/debughud.settings.xml)를 읽는다. Shipping 에는 변수만 있고 읽는 곳이 없다.
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_debugHUD );               ///< 디버그 HUD 표시(DebugHUD).
SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_debugHUDSections ); ///< 섹션 켬 · 끔 글(DebugHUDSectionState).
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_debugHUDCorner );        ///< HUD 모서리 0 왼위 · 1 오위 · 2 왼아래 · 3 오아래.
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_debugHUDOpacity );     ///< HUD 불투명도 0.2~1.

// 기동
SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_userSettingsFile ); ///< 사용자 설정 파일 경로 덮어쓰기(자동화 — 사용자 폴더를 건드리지 않는다)
