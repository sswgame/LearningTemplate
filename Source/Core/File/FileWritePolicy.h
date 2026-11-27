/**
 * @file FileWritePolicy.h
 * @brief 파일을 바꾸는 입구가 모두 지나는 쓰기 정책(쓰기 게이트 · 새 이름 규칙 · 체크아웃 규칙)입니다. 세 규칙 모두 구독 토큰으로 겁니다.
 * @details 규칙마다 `ObserverSlot` 칸 하나라 둘째 등록은 단언이고, 토큰이 사라질 때 칸이 아직 자기 값일 때만 걷는다. 앞 주인이 뒤 주인의 규칙을 지울 수 없다.
 *          언리얼 `IPlatformFile` 래퍼 층과 같은 자리다(래퍼를 풀어도 남의 래퍼를 지우지 못한다). 디스크 I/O 는 `FileUtil` 이 하고, 그 입구가 이 정책을 묻는다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/ObserverSlot.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Path/AbsolutePath.h"

namespace sw
{
    /** @brief 파일을 바꾸는 입구의 쓰기 정책입니다. 규칙은 여러 스레드에서 불립니다. */
    struct SW_API FileWritePolicy
    {
        /** @brief 경로를 받아 허락하면 값, 아니면 이유를 돌려주는 규칙입니다(쓰기 게이트 · 새 이름 규칙). */
        using RuleSignature = expected<void>( string_view path );
        /** @brief 이 자리의 읽기 전용 파일이 "체크아웃하지 않음"(버전 관리 잠금, git LFS lockable 은 잠그기 전까지 읽기 전용)이면 true 인 규칙입니다. */
        using CheckoutRuleSignature = bool( string_view path );
        /** @brief 쓰기 게이트 · 새 이름 규칙의 등록 토큰입니다. 토큰이 사라지면 규칙을 걷습니다. */
        using RuleSubscription = ObserverSlot<RuleSignature>::Subscription;
        /** @brief 체크아웃 규칙의 등록 토큰입니다. */
        using CheckoutRuleSubscription = ObserverSlot<CheckoutRuleSignature>::Subscription;
        /** @brief 걸린 체크아웃 규칙을 잠깐 떼어 두는 범위입니다(테스트가 호스트 규칙을 비우고 자기 규칙을 걸 때). */
        using CheckoutRuleSuspension = ObserverSlot<CheckoutRuleSignature>::Suspension;

        /**
         * @brief 쓰기 게이트를 겁니다. 시나리오가 `scratch/` 밖의 추적 파일을 바꾸는 것을 막는 데 쓴다. 거절된 쓰기는 이유를 오류로 남기고 false 입니다(원본은 그대로).
         * @details `FileUtil` 의 바꾸는 입구(`writeFile` · `writeTextFile` · `copyFile` 의 대상 · `removeFile` · `tryRemoveFile` · `removeDirectory` · `renamePath` ·
         *          `appendTextFile` · `moveToRecycleBin`)가 모두 `isWriteAllowed` 하나를 지난다. 폴더를 만드는 `ensureDirectoryExists` 는 이미 있는 폴더에도 흔히 부르므로
         *          새 이름 규칙만 보고, 속성 · 시각만 바꾸는 `setWritable` · `setFileWriteTime` 은 내용이 그대로라 보지 않는다.
         */
        [[nodiscard]] static RuleSubscription bindWriteFilter( RuleSignature* filter );
        /**
         * @brief 새로 만드는 자리의 이름 규칙을 겁니다(있는 파일을 덮어쓰거나 지우는 것은 보지 않는다). 쓰기 게이트와 따로 선다.
         * @details 콘텐츠 트리(`Content/`)의 새 파일은 소문자다. 엔진의 `AssetPaths` 가 건다(Core 는 콘텐츠 루트를 모른다). 쓰기 경로 하나가 막으므로 이름을 접지 않고 이은
         *          새 입구도 대문자 파일을 만들지 못한다.
         */
        [[nodiscard]] static RuleSubscription bindCreateRule( RuleSignature* rule );
        /**
         * @brief 체크아웃 규칙을 겁니다. 버전 관리 트리를 아는 엔진의 `AssetPaths` 가 건다.
         * @details 규칙 밖의 읽기 전용(복사로 속성이 따라온 사본 · git 개체 · 작업 폴더)은 정리가 지워야 하는 파일이라, 지우기를 모든 읽기 전용에서 막지 않고 규칙 안에서만 막는다.
         */
        [[nodiscard]] static CheckoutRuleSubscription bindCheckoutRule( CheckoutRuleSignature* rule );
        /** @brief 걸린 체크아웃 규칙을 범위 동안 떼어 둡니다. 범위가 끝날 때 칸이 비어 있어야 한다(그사이 건 토큰을 먼저 푼다). */
        [[nodiscard]] static CheckoutRuleSuspension suspendCheckoutRule();

        /**
         * @brief 게이트가 @p path 를 바꾸는 것을 허락하는지, 체크아웃하지 않은 파일이 아닌지(폴더면 그 아래), 새 자리면 이름 규칙에 맞는지 봅니다.
         * @details 거절하면 경로와 이유를 오류로 남기고 false 입니다. 규칙이 없으면 true. 경로를 스스로 여는 서드파티 저장(DDS 저장 · SQLite)은 쓰기 전에 이것을 직접 부른다.
         *          그런 호출과 OS · C 런타임의 직접 쓰기는 `FileUtil` 쓰기 경로를 우회한 것이다.
         */
        [[nodiscard]] static bool isWriteAllowed( const AbsolutePath& path );
        /** @brief @p path 가 아직 없으면 이름 규칙에 맞는지 봅니다(있으면 true). 거절하면 경로와 이유를 오류로 남기고 false 입니다. 폴더를 만드는 입구도 부른다. */
        [[nodiscard]] static bool isCreateAllowed( const AbsolutePath& path );
        /**
         * @brief @p path(파일, 폴더면 그 아래 모든 파일)에서 체크아웃 규칙 안의 읽기 전용 파일을 처음 만난 경로입니다. 규칙이 없거나 그런 파일이 없으면 빈 값입니다.
         * @details POSIX 는 폴더 권한만으로 읽기 전용 파일을 지우고 옮기며, Windows 지우기는 정리를 위해 속성을 풀고 지워 잠금이 뚫린다. 그래서 `isWriteAllowed` 가 이 답으로 거절한다.
         *          유니티 `AssetDatabase.IsOpenForEdit` 의 반대 질의이고, 언리얼 `IFileManager::Delete` 가 `EvenReadOnly` 없이 읽기 전용을 지우지 않는 자리다.
         */
        [[nodiscard]] static optional<AbsolutePath> findNotOpenForEdit( const AbsolutePath& path );
    };
} // namespace sw
