#include "pch.h"

#include "Engine/UserSettings/UserSettingsVariables.h"

// 기본값은 스키마(`engine/settings/engine.settings.xml`)의 기본값과 같게 둔다 — 사용자 설정이 기동 때 다시 넣으므로 실행 중 값은 스키마가 정하지만,
// 설정 서비스 없이 도는 시험 · 도구는 이 값을 본다.
SW_GLOBAL_VARIABLE( float32, gv_renderScale, 1.0f, "3D 렌더 해상도 배율 0.5~1 (사용자 설정 graphics.renderScale, 렌더러 미구현)" );
SW_GLOBAL_VARIABLE( int32, gv_upscaler, 0, "업스케일러 (0 끔, 사용자 설정 graphics.upscaler)" );
SW_GLOBAL_VARIABLE( int32, gv_shadowQuality, 2, "그림자 품질 0~3 (사용자 설정 graphics.shadowQuality)" );
SW_GLOBAL_VARIABLE( float32, gv_viewDistanceScale, 1.0f, "시야 거리 배율 (사용자 설정 graphics.viewDistance)" );
SW_GLOBAL_VARIABLE( float32, gv_foliageDensity, 1.0f, "식생 밀도 배율 (사용자 설정 graphics.foliageDensity)" );
SW_GLOBAL_VARIABLE( int32, gv_postQuality, 2, "후처리 품질 0~3 (사용자 설정 graphics.postQuality)" );
SW_GLOBAL_VARIABLE( int32, gv_textureQuality, 2, "텍스처 품질 0~3 (사용자 설정 graphics.textureQuality)" );
SW_GLOBAL_VARIABLE( int32, gv_effectsQuality, 2, "이펙트 품질 0~3 (사용자 설정 graphics.effectsQuality)" );
SW_GLOBAL_VARIABLE( bool, gv_motionBlur, true, "모션 블러 (사용자 설정 graphics.motionBlur)" );

SW_GLOBAL_VARIABLE( float32, gv_cameraFieldOfView, 70.0f, "카메라 시야각(도) (사용자 설정 gameplay.fieldOfView)" );
SW_GLOBAL_VARIABLE( float32, gv_cameraShakeScale, 1.0f, "카메라 흔들림 배율 0~1 (사용자 설정 gameplay.cameraShake)" );
SW_GLOBAL_VARIABLE( bool, gv_cameraHeadBob, true, "걷기 머리 흔들림 (사용자 설정 gameplay.headBob)" );

SW_GLOBAL_VARIABLE( int32, gv_colorVisionMode, 0, "색각 보정 0 끔 1 적색약 2 녹색약 3 청색약 (사용자 설정 accessibility.colorVision, UI 캔버스만 — 톤맵 미구현)" );
SW_GLOBAL_VARIABLE( float32, gv_uiScale, 1.0f, "게임 UI 배율 (사용자 설정 accessibility.uiScale)" );
SW_GLOBAL_VARIABLE( sw::string, gv_uiTheme, "default", "게임 UI 테마 이름 default · highcontrast (사용자 설정 accessibility.uiTheme)" );
SW_GLOBAL_VARIABLE( float32, gv_uiTextScale, 1.0f, "게임 UI 글자 크기 배율 (사용자 설정 accessibility.textSize)" );
SW_GLOBAL_VARIABLE( bool, gv_reduceFlashing, false, "번쩍임 줄이기 (사용자 설정 accessibility.reduceFlashing)" );
SW_GLOBAL_VARIABLE( bool, gv_uiReduceMotion, false, "UI 움직임 줄이기 — UI 애니메이션 · 트윈 · 스타일 전환이 바로 끝 값으로 (사용자 설정 accessibility.reduceMotion)" );
SW_GLOBAL_VARIABLE( bool, gv_subtitles, true, "자막 표시 (사용자 설정 accessibility.subtitles)" );
SW_GLOBAL_VARIABLE( int32, gv_subtitleSize, 1, "자막 크기 0 작게 1 보통 2 크게 (사용자 설정 accessibility.subtitleSize)" );
SW_GLOBAL_VARIABLE( float32, gv_subtitleBackgroundOpacity, 0.5f, "자막 배경 불투명도 0~1 (사용자 설정 accessibility.subtitleBackground)" );

// 개발 도구 — 스키마는 `engine/settings/debughud.settings.xml` 이고 Dev 만 읽는다.
SW_GLOBAL_VARIABLE( bool, gv_debugHUD, false, "디버그 HUD 표시 — Dev 전용, 단축키 Ctrl+F3 · 명령 hud (사용자 설정 debug.hud)" );
SW_GLOBAL_VARIABLE( sw::string, gv_debugHUDSections, "", "디버그 HUD 섹션 켬 · 끔 — 공백으로 나눈 '이름'(켬) · '-이름'(끔), 적지 않은 섹션은 기본 (사용자 설정 debug.hudSections)" );
SW_GLOBAL_VARIABLE( int32, gv_debugHUDCorner, 0, "디버그 HUD 모서리 0 왼위 1 오위 2 왼아래 3 오아래 (사용자 설정 debug.hudCorner)" );
SW_GLOBAL_VARIABLE( float32, gv_debugHUDOpacity, 0.85f, "디버그 HUD 불투명도 0.2~1 (사용자 설정 debug.hudOpacity)" );

SW_TEST_GLOBAL_VARIABLE_SHIPPED( sw::string, gv_userSettingsFile, "", "사용자 설정 파일 경로 (비면 사용자 폴더의 usersettings.json)" );
