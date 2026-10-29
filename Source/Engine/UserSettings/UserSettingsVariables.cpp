#include "pch.h"

#include "Engine/UserSettings/UserSettingsVariables.h"

// 기본값은 스키마(`engine/settings/engine.settings.xml`)의 기본값과 같게 둔다. 사용자 설정이 기동 때 다시 넣으므로 실행 중 값은 스키마가 정하지만,
// 설정 서비스 없이 도는 테스트와 도구는 이 값을 본다.
SW_GLOBAL_VARIABLE( float32, gv_renderScale, 1.0f, "3D 렌더 해상도 배율 0.5~1 (사용자 설정 graphics.renderScale, 화면 출력 뷰)" );
SW_GLOBAL_VARIABLE( int32, gv_upscaler, 0, "업스케일 필터 0 선형 1 FSR 1 — 렌더 배율 뷰를 Present 가 키울 때 (사용자 설정 graphics.upscaler)" );
SW_GLOBAL_VARIABLE( int32, gv_shadowMapResolution, 2048, "그림자 맵 한 변 텍셀 수 (사용자 설정 graphics.shadowQuality 의 targetValue)" );
SW_GLOBAL_VARIABLE( int32, gv_postQuality, 2, "후처리 품질 0~3 — 패스의 _minPostQuality 보다 낮으면 건너뛴다 (사용자 설정 graphics.postQuality)" );
SW_GLOBAL_VARIABLE( bool, gv_motionBlur, true, "모션 블러 (사용자 설정 graphics.motionBlur, MotionBlur 패스가 읽는다)" );
// 아래 층(Object · Graphics · VFX)이 읽는 설정 변수는 읽는 쪽에 둔다 — 그 층은 이 층을 include 하지 못한다:
// gv_viewDistanceScale(CameraComponent.cpp) · gv_textureMipDrop(Texture2D.cpp) · gv_effectsSpawnScale(VFXComponent.cpp).

SW_GLOBAL_VARIABLE( float32, gv_cameraFieldOfView, 70.0f, "카메라 시야각(도) (사용자 설정 gameplay.fieldOfView)" );
SW_GLOBAL_VARIABLE( float32, gv_cameraShakeScale, 1.0f, "카메라 흔들림 배율 0~1 (사용자 설정 gameplay.cameraShake)" );
SW_GLOBAL_VARIABLE( bool, gv_cameraHeadBob, true, "걷기 머리 흔들림 (사용자 설정 gameplay.headBob)" );

SW_GLOBAL_VARIABLE( int32, gv_colorVisionMode, 0, "색각 보정 0 끔 1 적색약 2 녹색약 3 청색약 (사용자 설정 accessibility.colorVision, UI 캔버스와 화면 출력 뷰의 톤맵 — postgrade.hlsli)" );
SW_GLOBAL_VARIABLE( float32, gv_uiScale, 1.0f, "게임 UI 배율 (사용자 설정 accessibility.uiScale)" );
SW_GLOBAL_VARIABLE( sw::string, gv_uiTheme, "default", "게임 UI 테마 이름 default · highcontrast (사용자 설정 accessibility.uiTheme)" );
SW_GLOBAL_VARIABLE( float32, gv_uiTextScale, 1.0f, "게임 UI 글자 크기 배율 (사용자 설정 accessibility.textSize)" );
SW_GLOBAL_VARIABLE( bool, gv_uiReduceMotion, false, "UI 움직임 줄이기 — UI 애니메이션 · 트윈 · 스타일 전환이 바로 끝 값으로 (사용자 설정 accessibility.reduceMotion)" );
SW_GLOBAL_VARIABLE( bool, gv_subtitles, true, "자막 표시 (사용자 설정 accessibility.subtitles)" );
SW_GLOBAL_VARIABLE( int32, gv_subtitleSize, 1, "자막 크기 0 작게 1 보통 2 크게 (사용자 설정 accessibility.subtitleSize)" );
SW_GLOBAL_VARIABLE( float32, gv_subtitleBackgroundOpacity, 0.5f, "자막 배경 불투명도 0~1 (사용자 설정 accessibility.subtitleBackground)" );

// 개발 도구: 스키마는 `engine/settings/debughud.settings.xml` 이며 Dev 만 읽는다.

SW_TEST_GLOBAL_VARIABLE( sw::string, gv_userSettingsFile, "", "사용자 설정 파일 경로 (비면 사용자 폴더의 usersettings.json)" );
