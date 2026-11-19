/**
 * @file AndroidInputConnectionState.h
 * @brief Android `InputConnection` 호출(조합 글 설정 · 조합 끝 · 확정 · 주변 글 지우기)을 엔진의 조합 · 확정 글과 지우기 키로 옮기는 규칙입니다(설계: DesignBacklog "Android IME 조합").
 * @details 플랫폼 헤더가 없는 순수 코드라 데스크톱 단위 테스트가 Android 없이 본다. Java 뷰(`SWInputView`)의 호출을 JNI 가 이 객체에 넘기고, 나온 동작을 창이
 *          `CallbackInputWindow::queueTextInput` · 키 입력으로 낸다. 편집기 글은 엔진(UI 글 칸)이 갖고 Java 편집기는 비워 둔다 — 언리얼 GameTextInput 이 확정마다 상태를 비우는 것과 같다.
 *          `deleteSurroundingText` 는 명세대로 조합 구간을 빼고 커서 앞뒤를 지운다 — 조합은 그대로 두고 지우기 키만 낸다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 옮긴 동작 하나의 종류입니다. */
    enum class AndroidTextActionKind : uint8
    {
        Composition,  ///< 조합 중 글 전체(빈 글이면 조합 끝)
        Commit,       ///< 확정 글
        BackspaceKey, ///< 커서 앞 한 글자 지우기(`AKEYCODE_DEL`)
        DeleteKey,    ///< 커서 뒤 한 글자 지우기(`AKEYCODE_FORWARD_DEL`)
    };

    /** @brief 옮긴 동작 하나입니다. 글은 조합 · 확정에만 있다. */
    struct AndroidTextAction
    {
        AndroidTextActionKind _kind{ AndroidTextActionKind::Commit };
        string                _text;
    };

    /**
     * @class AndroidInputConnectionState
     * @brief 조합 중 글 하나를 들고 `InputConnection` 호출을 동작 목록으로 옮깁니다. 한 스레드(Java UI 스레드의 JNI 호출)에서만 부른다.
     */
    class SW_API AndroidInputConnectionState
    {
    public:
        /** @brief Android `AKEYCODE_DEL`(커서 앞 지우기)입니다. */
        static constexpr int32 kBackspaceKeyCode = 67;
        /** @brief Android `AKEYCODE_FORWARD_DEL`(커서 뒤 지우기)입니다. */
        static constexpr int32 kForwardDeleteKeyCode = 112;
        /** @brief 지우기 하나가 낼 수 있는 키 수의 상한입니다 — 입력기 값을 믿지 않는다(커서 앞 글 전체를 지우라는 큰 수가 온다). */
        static constexpr uint32 kMaxDeleteKeys = 64;

        /** @brief 조합 글을 @p text 로 바꿉니다(`setComposingText`). 빈 글이면 조합 끝이고, 조합 중이 아니었으면 아무것도 내지 않는다. */
        void setComposingText( string_view text, vector<AndroidTextAction>& outListAction );
        /** @brief 조합을 그대로 확정합니다(`finishComposingText`). 조합 중이면 확정(그 글) + 조합(빈 글)을 낸다. */
        void finishComposingText( vector<AndroidTextAction>& outListAction );
        /** @brief @p text 를 확정합니다(`commitText` — 조합 중이었으면 조합을 갈음한다). 조합(빈 글, 조합 중이었으면) + 확정을 낸다. */
        void commitText( string_view text, vector<AndroidTextAction>& outListAction );
        /** @brief 커서 앞 @p beforeLength · 뒤 @p afterLength 글자를 지웁니다(`deleteSurroundingText`). 조합은 그대로이고 지우기 키를 낸다(각 `kMaxDeleteKeys` 까지). */
        void deleteSurroundingText( uint32 beforeLength, uint32 afterLength, vector<AndroidTextAction>& outListAction );
        /** @brief 포커스를 잃었거나 입력기가 닫혔습니다 — 조합 중이었으면 조합(빈 글)을 내고 비운다(확정하지 않는다 — 데스크톱 입력기의 취소와 같다). */
        void cancelComposition( vector<AndroidTextAction>& outListAction );

        const string& getComposingText() const { return _composingText; }

    private:
        string _composingText;
    };
} // namespace sw
