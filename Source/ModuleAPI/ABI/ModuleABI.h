/**
 * @file ModuleABI.h
 * @brief 모듈 C-ABI 표(`GameAPI` · `EditorAPI`)를 X 매크로 목록 하나로 선언하는 장치입니다 — 목록에서 구조체 멤버와 함수 포인터 형이 함께 나온다.
 *
 * @details 표는 **함수 포인터를 순서대로 늘어놓은 구조체**라 호스트와 모듈이 다른 헤더로 빌드되면 호스트가 엉뚱한 함수를 부른다(가운데에 하나 끼우면 뒤가 한 칸씩 밀린다).
 *          그 대조는 표마다 매기지 않고 모듈을 올리는 모든 길이 하나로 한다 — 빌드가 모듈마다 박는 엔진 ABI 스탬프(Core · Engine · ModuleAPI 헤더 내용 지문,
 *          `engine::isModuleBuiltWithThisEngine`)가 이 헤더들을 덮는다. 예전에는 표 글 해시를 따로 내보내 대조했는데, 같은 헤더를 덮는 두 번째 스탬프였다(언리얼 `BuildId` 하나 ·
 *          Godot GDExtension 의 호환 대조 한 곳과 같은 자리).
 */
#pragma once
#include "Core/Foundation/Types.h"

/** @brief 표 항목 하나를 구조체 멤버(빈 함수 포인터)로 펼칩니다. */
#define SW_MODULE_ABI_ENTRY_MEMBER( ReturnType, Name, Params ) ReturnType( *Name ) Params{ nullptr };
/** @brief 표 항목 하나를 함수 포인터 형(`PFN_<이름>`)으로 펼칩니다. 이름으로 찾는 진입점(헤드리스 에디터 진입점)이 씁니다. */
#define SW_MODULE_ABI_ENTRY_PFN( ReturnType, Name, Params ) using PFN_##Name = ReturnType( * ) Params;
