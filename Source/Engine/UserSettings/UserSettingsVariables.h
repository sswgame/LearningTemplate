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
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_shadowQuality );       ///< 그림자 품질 0~3. 아직 렌더러가 읽지 않는다.
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
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_colorVisionMode );             ///< 색각 보정 0 끔 · 1 적색약 · 2 녹색약 · 3 청색약. 톤맵 패스가 읽을 자리다(아직 셰이더 없음).
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_uiScale );                   ///< 게임 UI 배율.
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_uiTextScale );               ///< 게임 UI 글자 크기 배율.
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_reduceFlashing );               ///< 번쩍임 줄이기(섬광 · 화면 깜빡임 효과를 약하게).
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_subtitles );                    ///< 자막 표시.
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_subtitleSize );                ///< 자막 크기 0 작게 · 1 보통 · 2 크게.
SW_EXTERN_GLOBAL_VARIABLE( float32, gv_subtitleBackgroundOpacity ); ///< 자막 배경 불투명도 0~1.

// 기동
SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_userSettingsFile ); ///< 사용자 설정 파일 경로 덮어쓰기(자동화 — 사용자 폴더를 건드리지 않는다)
