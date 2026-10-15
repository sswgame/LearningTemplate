#pragma once
/**
 * @file TestFramework.h
 * @brief 자동 등록 단위 테스트 프레임워크 — 테스트 파일이 include 하는 한 헤더
 * @details 책임별 헤더를 모은다. 케이스 등록과 스위트 선언은 `TestCatalog.h`, 단언은 `TestAssert.h`, 실행 · 선택 · 건너뜀은 `TestRegistry.h`,
 *          범위 도우미(실패 · 로그 · 엔진 단언 가로채기, 추적)는 `TestScopes.h`, 임시 경로는 `TestTempPath.h` 다.
 *          스위트가 같이 쓰는 하네스(엔진 기동 · 자식 프로세스 · 루프백 망 · 벤치)는 `TestFramework/Harness/` 에 있고 필요한 파일만 따로 include 한다.
 */
#include "TestFramework/TestAssert.h"
#include "TestFramework/TestCatalog.h"
#include "TestFramework/TestContext.h"
#include "TestFramework/TestEnvironment.h"
#include "TestFramework/TestFilter.h"
#include "TestFramework/TestRegistry.h"
#include "TestFramework/TestScopes.h"
#include "TestFramework/TestTempPath.h"
