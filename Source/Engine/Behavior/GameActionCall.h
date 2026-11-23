/**
 * @file GameActionCall.h
 * @brief 읽을 때 칸까지 해석해 들고 있는 동작 호출(`GameActionCall`)과 그 문맥 · 결과입니다. 동작의 등록은 `ActionRegistry.h` 다.
 * @details 행동 트리 잎, 대화 Action 노드, 스케줄 칸의 시작 · 끝, 퀘스트 보상이 같은 이름을 부른다. 실행기를 합치지 않고 어휘만 나눈다(언리얼이 BT · StateTree · GAS 를 나누고 Gameplay Tag 를 나누는 것과 같다).
 *          칸은 읽을 때 한 번 해석하므로 실행에 글이 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Behavior/GameplayContext.h"
#include "Engine/Behavior/RegisteredParams.h"
#include "Engine/Reflection/InstancedStruct.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct ActionEntry;

    /** @brief 동작의 결과입니다. 즉시 끝나는 동작은 `Succeeded` · `Failed` 를 돌려줍니다. `Running` 은 다시 불러 달라는 뜻이고 기다릴 수 없는 실행기(대화 · 스케줄 · 보상)는 끝난 것으로 봅니다. */
    ENUM()
    enum class ActionResult : uint8
    {
        Succeeded = 0,
        Failed,
        Running
    };

    /** @brief 동작 함수가 받는 문맥입니다. 세계는 칸(`_world`)이고, 나머지는 시간을 쓰는 실행기(행동 트리)가 채웁니다. */
    struct ActionContext
    {
        GameplayContext _world{};
        void*           _pOwner{ nullptr };       ///< 실행기에 넘긴 행위자
        float32         _deltaTime{ 0.0f };       ///< 틱이 아닌 실행기는 0 이다
        uint8           _bJustStarted{ SW_TRUE }; ///< 이번에 시작했다. 처음 한 번 할 일에 쓴다
        uint8           _bAborted{ SW_FALSE };    ///< 중단되어 불렸다. 돌려준 값은 쓰지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class GameActionCall
     * @brief 동작 하나의 호출입니다. 등록 줄과 해석한 칸을 들고 있어 부를 때 글을 보지 않습니다. 복사해도 칸은 나눠 듭니다.
     * @details 글 형식은 `flag.set(hasKey, 1)` 또는 `flag.set(name=hasKey, value=1)` 이고 인자가 없으면 괄호를 생략합니다. XML 형식은 속성이 곧 칸입니다(`<Action task="shop.open" shop="…"/>`).
     */
    class SW_API GameActionCall
    {
    public:
        GameActionCall();

        /** @brief 글을 읽습니다. 모르는 이름 · 틀린 칸이면 오류 로그(`GameAction.Invalid`)를 남기고 false 이며 호출은 비어 있습니다. */
        [[nodiscard]] static bool compileText( string_view text, string_view sourceName, GameActionCall& outCall );
        /** @brief 로그를 남기지 않는 `compileText` 입니다. 틀리면 그 까닭입니다. */
        [[nodiscard]] static expected<void> tryCompileText( string_view text, GameActionCall& outCall );
        /** @brief 이미 읽은 칸으로 호출을 만듭니다. 칸을 XML 속성으로 읽는 행동 트리가 씁니다. @p params 는 @p entry 의 칸 구조체여야 합니다. */
        [[nodiscard]] static GameActionCall make( const ActionEntry& entry, const InstancedStruct& params );

        /** @brief 글 @p text 의 이름 부분입니다(`flag.set(a)` → `flag.set`). */
        [[nodiscard]] static string_view getCallName( string_view text );
        /** @brief 글을 칸 값으로 풉니다. 편집기가 칸을 보이려고 부릅니다. 틀리면 그 까닭이고 @p outParams 는 비어 있습니다. */
        [[nodiscard]] static expected<void> tryReadProperties( string_view text, const ActionEntry*& outEntry, InstancedStruct& outParams );
        /** @brief 칸 값을 글로 되돌립니다(`tryReadProperties` 의 짝). 기본값과 같은 칸은 쓰지 않습니다. */
        [[nodiscard]] static string formatText( const ActionEntry& entry, const InstancedStruct& params );

        /** @brief 부릅니다. 빈 호출은 `Failed` 입니다. 읽은 뒤 등록 줄이 빠졌으면(모듈을 내렸고 다시 컴파일하지 않았다) 부르지 않고 오류(`GameAction.Stale`)와 `Failed` 입니다. */
        ActionResult invoke( ActionContext& context ) const;

        bool               isValid() const { return _pEntry != nullptr; }
        const ActionEntry* getEntry() const { return _pEntry; }
        /** @brief 해석한 칸 구조체입니다(등록 줄의 칸 형식 — `getEntry()->_pParamsOps`). 읽은 뒤 참조를 검사하는 쪽(지역 프로필)이 쓴다. 빈 호출이면 nullptr 입니다. */
        const void* getParams() const { return _params.get(); }
        const utf8* getName() const;

    private:
        const ActionEntry* _pEntry;
        RegisteredParams   _params; ///< 칸 값과 읽을 때의 등록표 세대 — 줄이 빠졌으면 부르지도 놓지도 않는다(감사 S-14)
    };
} // namespace sw
