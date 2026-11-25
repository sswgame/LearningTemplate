/**
 * @file AutomationScratchWorkspace.h
 * @brief 시나리오 사본 폴더(`<Scratch>`)를 실제로 만들고, 실행 동안 원본 쓰기를 막는 실행기 쪽 일입니다.
 * @details id 규칙(선언 검사 · 글 안의 id 바꾸기)은 시나리오 형식과 함께 엔진의 `AutomationScratch` 에 있다. 디스크를 지우고 복사하고 쓰기 문을 세우는 일은
 *          실행기만 하므로 개발 도구에 둔다 — 배포 구성에는 없다(언리얼 Automation 의 테스트 픽스처 준비도 Developer 모듈 쪽이다).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/Subsystem.h"

#include "DevTools/DevToolsAPI.h"

#include "Engine/Content/AssetGUID.h"

namespace sw
{
    // 개발 도구 클라이언트가 기동 범위에 세운다(`DevToolsClient::addStartupSubsystems`) — 엔진 서브시스템 칸(SW_SUBSYSTEM_SLOT)은 Engine 이미지의 정적 멤버라 DevTools 가 정의할 수 없다.
    /**
     * @struct AutomationScratchCounter
     * @brief 사본 폴더를 끝까지 만든 횟수입니다(`AutomationScratchWorkspace::getPrepareCount` — 세대 번호). 엔진 범위 서비스라 엔진 범위와 함께 생기고 사라진다.
     * @details 시나리오 실행기는 단계마다 새로 서므로 실행기 멤버로 두면 번호가 되돌아가 에디터 울타리가 새 사본을 놓친다 — 엔진 범위가 든다(예전에는 프로세스 원자 칸).
     */
    class AutomationScratchCounter final : public Subsystem
    {
    public:
        atomic<uint64> _prepareCount{ 0 };

        static constexpr const utf8* kSubsystemName = "AutomationScratchCounter";
        SubsystemInitResult          initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                         shutdownSubsystem() override {}
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
    };

    struct AutomationScratchFolder;

    /** @brief 시나리오 사본 폴더를 만들고 쓰기 문을 세웁니다. 상태는 엔진 범위 서비스(사본 횟수)뿐입니다. */
    struct SW_DEVTOOLS_API AutomationScratchWorkspace
    {
        /**
         * @brief 사본 폴더를 지우고 원본을 복사합니다. 글 파일의 원본 id 는 사본 id 로, 원본의 GUID(머리 · `.meta` · 참조 글)는 사본의 GUID 로 바꿉니다.
         * @details 사본은 새 에셋이다 — 원본의 GUID 를 그대로 들면 색인에서 겹치고, 사본끼리 GUID 로 가리킨 참조가 원본을 가리킨다. 사본의 GUID 는 원본 GUID 와 사본 폴더 id 에서 나온다
         *          (`AssetGUID::fromName` — 다시 만들어도 같다). 사본은 이 프로세스의 실행 뿌리 아래(`AssetPaths::getScratchFolderPath`)라 남의 실행 사본과 겹치지 않는다. 끝난 실행의 사본은 그 실행 뿌리째 `SavedCleanup` 이 거둔다.
         * @return 선언이 틀렸거나 원본이 없거나 지우기 · 복사 · 쓰기가 실패하면 오류입니다(시나리오는 시작하지 않는다 — 원본에 쓰는 길로 돌아가지 않는다).
         */
        [[nodiscard]] static expected<void> prepare( const AutomationScratchFolder& folder );
        /**
         * @brief 끝난 시나리오의 사본 폴더를 지웁니다(체인의 다음 단계 앞 — `AutomationSession::startStage`).
         * @details 이 프로세스의 사본은 색인 · Quick Open 에 보인다. 남기면 뒤 단계가 원본과 같은 파일 이름의
         *          사본을 집었다(editor.sequencerkeys 가 saveroundtripsequencer 의 사본을 열었다). 언리얼 자동화 테스트도 픽스처를 테스트 끝에 거둔다. 못 지우면 경고가 남고 false 다.
         */
        static bool discard( const AutomationScratchFolder& folder );
        /**
         * @brief 사본 폴더를 끝까지 만든 횟수입니다(`prepare` 성공마다 하나 오른다). 사본을 만든 뒤 OS 파일 감시가 그 쓰기를 알려야 한다 — 에디터가 이 번호가 바뀌면
         *        감시 울타리를 다시 세운다(`AssetHotReload` 의 시작 차단 조건).
         */
        [[nodiscard]] static uint64 getPrepareCount();
        /**
         * @brief 쓰기 입구 게이트를 세우고 그 토큰을 돌려줍니다 — 실행기가 시나리오를 시작할 때 받아 쥐고, 토큰이 사라지면 게이트가 걷힌다(`FileUtil::bindWriteFilter`).
         *        시나리오는 한 프로세스에서 차례로 돌므로 게이트는 하나다 — 쥔 채 또 부르면 이중 등록 단언이다.
         * @details 게이트가 서 있는 동안 리소스 루트(`Content/`) 아래로 가는 모든 파일 쓰기(저장 · Quick Open 으로 연 원본 포함)는 오류와 함께 거절된다.
         *          프로젝트 루트의 추적 폴더 `docs/` · `Config/` 도 같은 문이 막는다(`Config/Editor/` 만 허용 목록 — 에디터 설정 저장 길). 사본(작업 도메인)은 리소스 루트 밖이라 열려 있다.
         */
        [[nodiscard]] static FileUtil::WriteFilterRegistration acquireWriteFilter();
        /** @brief 글 @p text 안의 GUID 글 표기(앞뒤가 글자 · 숫자가 아닌 소문자 16진 32 자) 가운데 @p mapGUID 에 있는 것을 바꿉니다. */
        static string rebaseIdentities( string_view text, const unordered_map<AssetGUID, AssetGUID>& mapGUID );
    };
} // namespace sw
