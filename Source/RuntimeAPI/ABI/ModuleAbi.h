/**
 * @file ModuleAbi.h
 * @brief 모듈 C-ABI 버전 + 스탬프입니다(App 과 EditorModule/SWGame 이 일치해야 합니다).
 *
 * @details `GameAPI` · `EditorAPI` 는 **함수 포인터를 순서대로 늘어놓은 구조체**입니다. 모듈이
 *          자기가 아는 자리에 채우고 호스트가 자기가 아는 자리에서 읽으므로, 둘이 서로 다른
 *          헤더로 빌드되면 **호스트가 엉뚱한 함수를 부릅니다.** 끝에 항목을 덧붙인 경우는 호스트
 *          쪽이 `nullptr` 로 남아 그나마 티가 나지만, **가운데에 하나 끼우면** 그 뒤가 모두 한
 *          칸씩 밀려 `update` 자리에서 `shutdown` 이 불립니다.
 *
 *          예전에는 그것을 막는 것이 로더의 `create != nullptr && destroy != nullptr` 검사뿐이었습니다.
 *          그 둘은 **맨 앞** 이라 가운데 삽입에서도 멀쩡히 채워집니다. 즉 가장 위험한 어긋남을
 *          정확히 통과시켰습니다. 핫 리로드는 모듈 DLL 만 다시 굽는 기능이므로 이 어긋남이 생기는
 *          바로 그 상황입니다.
 *
 *          RHI 경계는 이미 같은 이유로 `RHIModuleAbi.h` 에 버전+스탬프를 두고 로드할 때
 *          대조합니다(CLAUDE.md 의 "RHI ABI stamps"). 이 파일은 그 장치를 모듈 경계에 그대로 옮긴 것입니다.
 *
 * @note **표에 항목을 더하거나 순서를 바꾸면 `kModuleAbiVersion` 을 올리고 스탬프 문자열을 고치십시오.**
 *       그러면 옛 DLL 은 로드 시점에 이유가 적힌 에러와 함께 거부됩니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief `GameAPI`/`EditorAPI` 표의 모양이 바뀔 때마다 올립니다. */
    inline constexpr uint32 kModuleAbiVersion = 1;

    /**
     * @brief 표의 지문입니다. 버전과 함께 바꿉니다.
     *        v1: 모듈 경계에 버전 · 스탬프를 처음 붙임 (GameAPI 9항목 · EditorAPI 19항목).
     */
    inline constexpr auto kModuleAbiStamp = "module-abi-v1-2026-09";

    using PFN_GetModuleAbiVersion = uint32 ( * )();
    using PFN_GetModuleAbiStamp   = const utf8* (*)();
} // namespace sw
