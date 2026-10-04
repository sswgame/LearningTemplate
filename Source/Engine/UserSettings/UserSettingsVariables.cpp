#include "pch.h"

#include "Engine/UserSettings/UserSettingsVariables.h"

// 기본값은 스키마(`engine/settings/engine.settings.xml`)의 기본값과 같게 둔다 — 사용자 설정이 기동 때 다시 넣으므로 실행 중 값은 스키마가 정하지만,
// 설정 서비스 없이 도는 시험 · 도구는 이 값을 본다.
SW_GLOBAL_VARIABLE_FLOAT( gv_renderScale, 1.0f, "3D 렌더 해상도 배율 0.5~1 (사용자 설정 graphics.renderScale, 렌더러 미구현)" );
SW_GLOBAL_VARIABLE_INT( gv_upscaler, 0, "업스케일러 (0 끔, 사용자 설정 graphics.upscaler)" );
SW_GLOBAL_VARIABLE_INT( gv_shadowQuality, 2, "그림자 품질 0~3 (사용자 설정 graphics.shadowQuality)" );
SW_GLOBAL_VARIABLE_FLOAT( gv_viewDistanceScale, 1.0f, "시야 거리 배율 (사용자 설정 graphics.viewDistance)" );
SW_GLOBAL_VARIABLE_FLOAT( gv_foliageDensity, 1.0f, "식생 밀도 배율 (사용자 설정 graphics.foliageDensity)" );
SW_GLOBAL_VARIABLE_INT( gv_postQuality, 2, "후처리 품질 0~3 (사용자 설정 graphics.postQuality)" );
SW_GLOBAL_VARIABLE_INT( gv_textureQuality, 2, "텍스처 품질 0~3 (사용자 설정 graphics.textureQuality)" );
SW_GLOBAL_VARIABLE_INT( gv_effectsQuality, 2, "이펙트 품질 0~3 (사용자 설정 graphics.effectsQuality)" );
SW_GLOBAL_VARIABLE_BOOL( gv_motionBlur, true, "모션 블러 (사용자 설정 graphics.motionBlur)" );

SW_GLOBAL_VARIABLE_FLOAT( gv_cameraFieldOfView, 70.0f, "카메라 시야각(도) (사용자 설정 gameplay.fieldOfView)" );
SW_GLOBAL_VARIABLE_FLOAT( gv_cameraShakeScale, 1.0f, "카메라 흔들림 배율 0~1 (사용자 설정 gameplay.cameraShake)" );
SW_GLOBAL_VARIABLE_BOOL( gv_cameraHeadBob, true, "걷기 머리 흔들림 (사용자 설정 gameplay.headBob)" );

SW_GLOBAL_VARIABLE_INT( gv_colorVisionMode, 0, "색각 보정 0 끔 1 적색약 2 녹색약 3 청색약 (사용자 설정 accessibility.colorVision, 셰이더 미구현)" );
SW_GLOBAL_VARIABLE_FLOAT( gv_uiScale, 1.0f, "게임 UI 배율 (사용자 설정 accessibility.uiScale)" );
SW_GLOBAL_VARIABLE_FLOAT( gv_uiTextScale, 1.0f, "게임 UI 글자 크기 배율 (사용자 설정 accessibility.textSize)" );
SW_GLOBAL_VARIABLE_BOOL( gv_reduceFlashing, false, "번쩍임 줄이기 (사용자 설정 accessibility.reduceFlashing)" );
SW_GLOBAL_VARIABLE_BOOL( gv_subtitles, true, "자막 표시 (사용자 설정 accessibility.subtitles)" );
SW_GLOBAL_VARIABLE_INT( gv_subtitleSize, 1, "자막 크기 0 작게 1 보통 2 크게 (사용자 설정 accessibility.subtitleSize)" );
SW_GLOBAL_VARIABLE_FLOAT( gv_subtitleBackgroundOpacity, 0.5f, "자막 배경 불투명도 0~1 (사용자 설정 accessibility.subtitleBackground)" );

SW_TEST_GLOBAL_VARIABLE_STRING( gv_userSettingsFile, "", "사용자 설정 파일 경로 (비면 사용자 폴더의 usersettings.json)", SW_KEEP_IN_SHIPPING );
