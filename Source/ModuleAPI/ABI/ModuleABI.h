/**
 * @file ModuleABI.h
 * @brief 모듈 C-ABI 표의 스탬프를 표 선언 글에서 계산하는 장치입니다(App 과 EditorModule/SWGame 이 일치해야 합니다).
 *
 * @details `GameAPI` · `EditorAPI` 는 함수 포인터를 순서대로 늘어놓은 구조체입니다. 모듈이
 *          자기가 아는 자리에 채우고 호스트가 자기가 아는 자리에서 읽으므로, 둘이 서로 다른
 *          헤더로 빌드되면 호스트가 엉뚱한 함수를 부릅니다. 끝에 항목을 덧붙인 경우는 호스트
 *          쪽이 `nullptr` 로 남아 그나마 티가 나지만, 가운데에 하나 끼우면 그 뒤가 모두 한
 *          칸씩 밀려 `update` 자리에서 `shutdown` 이 불립니다.
 *
 *          로더의 `create != nullptr && destroy != nullptr` 검사로는 막지 못합니다. 그 둘은 맨 앞 이라
 *          가운데 삽입에서도 멀쩡히 채워집니다. 핫 리로드는 모듈 DLL 만 다시 빌드하는 기능이므로 이 어긋남이
 *          생기는 바로 그 상황입니다.
 *
 *          예전에는 손 판 번호(v1 ~ v12)와 스탬프 문자열을 사람이 올렸다 — 잊으면 옛 DLL 이 통과했다. 이제 표는 X 매크로 목록 하나
 *          (`SW_GAME_API_ENTRIES` · `SW_EDITOR_API_ENTRIES`)로 선언하고, 구조체 멤버와 스탬프가 둘 다 그 목록에서 나온다. 스탬프는 목록 글
 *          (반환형 · 이름 · 매개변수, 순서대로)의 FNV-1a 64 비트 해시라 항목을 더하거나 빼거나 순서 · 시그니처를 바꾸면 저절로 바뀐다
 *          (언리얼 모듈의 빌드가 만드는 `BuildId`, Godot GDExtension 의 생성된 인터페이스 해시와 같은 자리).
 *          표 밖에서 경계를 넘는 구조체(`StateBufferSink` · `ViewportTarget` …)의 배치는 이 스탬프에 들지 않는다 — 그것은 핫 리로드가
 *          올리기 전에 대조하는 엔진 ABI 스탬프(헤더 내용 지문, `Engine/Module/EngineABIStamp.h`)가 덮는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

/** @brief 표 항목 하나를 구조체 멤버(빈 함수 포인터)로 펼칩니다. */
#define SW_MODULE_ABI_ENTRY_MEMBER( ReturnType, Name, Params ) ReturnType( *Name ) Params{ nullptr };
/** @brief 표 항목 하나를 스탬프 글(`반환형 이름(매개변수);`)로 펼칩니다. 목록 전체가 이어 붙은 문자열 리터럴 하나가 됩니다. */
#define SW_MODULE_ABI_ENTRY_TEXT( ReturnType, Name, Params ) #ReturnType " " #Name #Params ";"
/** @brief 표 항목 하나를 함수 포인터 형(`PFN_<이름>`)으로 펼칩니다. 이름으로 찾는 진입점(헤드리스 에디터 진입점)이 씁니다. */
#define SW_MODULE_ABI_ENTRY_PFN( ReturnType, Name, Params ) using PFN_##Name = ReturnType( * ) Params;

namespace sw
{
    /** @struct ModuleABIUtil @brief 표 선언 글에서 스탬프를 계산합니다. */
    struct ModuleABIUtil
    {
        /** @brief 표 선언 글 @p text 의 스탬프입니다(대소문자를 구분하는 FNV-1a 64 비트). 리터럴 끝의 NUL 은 넣지 않습니다. */
        template <size_t N>
        [[nodiscard]] static constexpr uint64 computeStamp( const utf8 ( &text )[N] ) noexcept
        {
            return HashUtil::computeStringHash64( text, N - 1, false );
        }
    };

    /** @brief 모듈이 빌드된 표 스탬프를 돌려주는 진입점의 형입니다(`getGameModuleABIStamp` · `getEditorModuleABIStamp`). */
    using PFN_GetModuleABIStamp = uint64 ( * )();
} // namespace sw
