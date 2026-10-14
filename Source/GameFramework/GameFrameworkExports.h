/**
 * @file GameFrameworkExports.h
 * @brief GameFramework SHARED DLL 용 SW_GF_API 입니다.
 */
#pragma once
#include "Core/Common/Macros.h"

#include "RuntimeAPI/Export/GameFrameworkAPI.h"

// SW_GF_API 는 RuntimeAPI/Export/GameFrameworkAPI.h 한 곳에서 정한다(RuntimeAPI 의 게임 서비스 연결 선언도 같은 매크로를 쓴다).
// `registerGameFrameworkTypes()` 는 두지 않는다. GameFramework 의 리플렉션 타입은
// `Engine` 이 서비스를 묶은 직후 `engine::registerModuleTypes( "GameFramework" )` 로
// 직접 등록한다. 그것이 기준이고 실제로 도는 유일한 경로다. 같은 일을 하는 함수를 여기 하나 더
// export 하지 말 것 — 등록 자리가 둘이면 어느 쪽이 도는지 알 수 없다.
