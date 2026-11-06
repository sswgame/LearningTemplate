/**
 * @file ActionRegistry.h
 * @brief 동작 어휘입니다. 등록 줄 `SW_GAME_ACTION` 과, 읽을 때 칸까지 해석해 들고 있는 호출 `GameActionCall` 을 정의합니다.
 * @details 행동 트리 잎, 대화 Action 노드, 스케줄 칸의 시작 · 끝, 퀘스트 보상이 같은 이름을 부른다. 실행기를 합치지 않고 어휘만 나눈다(언리얼이 BT · StateTree · GAS 를 나누고 Gameplay Tag 를 나누는 것과 같다).
 *          칸은 리플렉션 구조체라 고정 칸이 없고, 편집기는 등록을 읽어 목록과 칸을 저절로 보인다. 칸은 읽을 때 한 번 해석하므로 실행에 글이 없다.
 *          어빌리티 이펙트(복제 · 예측), 기믹 회로 노드(스텝 상태), `EditorCommandRegistry` · `DevCommandRegistry`(게임플레이가 아니다)는 여기에 합치지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Vocabulary/GameplayContext.h"
#include "Engine/Vocabulary/InstancedStruct.h"

namespace sw
{
    /** @brief 동작의 결과입니다. 즉시 끝나는 동작은 `Succeeded` · `Failed` 를 돌려줍니다. `Running` 은 다시 불러 달라는 뜻이고 기다릴 수 없는 실행기(대화 · 스케줄 · 보상)는 끝난 것으로 봅니다. */
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

    /** @brief 형식을 지운 동작 함수입니다. */
    using ActionFunction = ActionResult ( * )( ActionContext& context, const void* pParams );

    /** @brief 동작 이름 하나의 등록 줄입니다. 글자는 등록한 이미지의 정적 저장소에 있습니다. */
    struct ActionEntry
    {
        const utf8*               _pName;
        const InstancedStructOps* _pParamsOps;
        ActionFunction            _function;
    };

    /** @brief `ActionResult( ActionContext&, const TParams& )` 함수를 형식을 지운 함수로 옮깁니다. */
    template <typename TParams, auto pFunction>
    struct ActionThunk
    {
        static ActionResult call( ActionContext& context, const void* pParams )
        {
            return pFunction( context, *static_cast<const TParams*>( pParams ) );
        }
    };

    /**
     * @struct ActionRegistry
     * @brief 이름 → 동작 표입니다(대소문자 무시). Engine 이미지의 함수 정적입니다. 까닭은 `ConditionRegistry` 와 같습니다.
     */
    struct SW_API ActionRegistry
    {
        static void               registerAction( const ActionEntry* pEntry );
        static void               unregisterAction( const ActionEntry* pEntry );
        static const ActionEntry* find( string_view name );
        /** @brief 등록된 이름을 등록 순서대로 뒤에 붙입니다(편집기의 고르기 목록). */
        static void collectNames( vector<string>& outListName );
        /** @brief 등록된 모든 이름을 ", " 로 이은 글입니다(모르는 이름 오류에 보인다). */
        static string joinNames();
    };

    /** @brief `SW_GAME_ACTION` 이 두는 정적 등록자입니다. */
    class SW_API ActionRegistrar
    {
    public:
        explicit ActionRegistrar( const ActionEntry* pEntry );
        ~ActionRegistrar();
        ActionRegistrar( const ActionRegistrar& )            = delete;
        ActionRegistrar& operator=( const ActionRegistrar& ) = delete;

    private:
        const ActionEntry* _pEntry;
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
        /** @brief 로그를 남기지 않는 `compileText` 입니다. 틀리면 까닭을 @p outError 에 적습니다. */
        [[nodiscard]] static bool tryCompileText( string_view text, GameActionCall& outCall, string& outError );
        /** @brief 이미 읽은 칸으로 호출을 만듭니다. 칸을 XML 속성으로 읽는 행동 트리가 씁니다. @p fields 는 @p entry 의 칸 구조체여야 합니다. */
        [[nodiscard]] static GameActionCall make( const ActionEntry& entry, const InstancedStruct& fields );

        /** @brief 글 @p text 의 이름 부분입니다(`flag.set(a)` → `flag.set`). */
        [[nodiscard]] static string_view getCallName( string_view text );
        /** @brief 글을 칸 값으로 풉니다. 편집기가 칸을 보이려고 부릅니다. 틀리면 false 이고 @p outFields 는 비어 있습니다. */
        [[nodiscard]] static bool tryReadFields( string_view text, const ActionEntry*& outEntry, InstancedStruct& outFields, string& outError );
        /** @brief 칸 값을 글로 되돌립니다(`tryReadFields` 의 짝). 기본값과 같은 칸은 쓰지 않습니다. */
        [[nodiscard]] static string formatText( const ActionEntry& entry, const InstancedStruct& fields );

        /** @brief 부릅니다. 빈 호출은 `Failed` 입니다. */
        ActionResult invoke( ActionContext& context ) const;

        bool               isValid() const { return _pEntry != nullptr; }
        const ActionEntry* getEntry() const { return _pEntry; }
        const utf8*        getName() const { return _pEntry != nullptr ? _pEntry->_pName : ""; }

    private:
        const ActionEntry*     _pEntry;
        shared_ptr<const void> _pParams;
    };
} // namespace sw

/**
 * @brief 동작을 그 함수의 .cpp 에서 등록합니다(`namespace sw` 안, 파일 범위).
 * @param varName   파일 안에서 유일한 이름 조각(변수 이름용)
 * @param pName     데이터가 적는 이름(리터럴, `<영역>.<동작>`)
 * @param TParams   칸 구조체. 없으면 `VocabularyNoFields`
 * @param pFunction `ActionResult( ActionContext&, const TParams& )` 함수의 주소
 */
#define SW_GAME_ACTION( varName, pName, TParams, pFunction )                                                                                                     \
    static const ::sw::ActionEntry     sw_gameAction_##varName{ pName, &::sw::InstancedStructOps::of<TParams>(), &::sw::ActionThunk<TParams, pFunction>::call }; \
    static const ::sw::ActionRegistrar sw_gameActionRegistrar_##varName                                                                                          \
    {                                                                                                                                                            \
        &sw_gameAction_##varName                                                                                                                                 \
    }
