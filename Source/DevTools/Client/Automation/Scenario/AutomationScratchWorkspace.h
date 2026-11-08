/**
 * @file AutomationScratchWorkspace.h
 * @brief 시나리오 사본 폴더(`<Scratch>`)를 실제로 만들고, 실행 동안 원본 쓰기를 막는 실행기 쪽 일입니다.
 * @details id 규칙(선언 검사 · 프로세스 꼬리 · 글 안의 id 바꾸기)은 시나리오 형식과 함께 엔진의 `AutomationScratch` 에 있다. 디스크를 지우고 복사하고 쓰기 문을 세우는 일은
 *          실행기만 하므로 개발 도구에 둔다 — 배포 구성에는 없다(언리얼 Automation 의 테스트 픽스처 준비도 Developer 모듈 쪽이다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    struct AutomationScratchFolder;

    /** @brief 시나리오 사본 폴더를 만들고 쓰기 문을 세웁니다. 상태는 엔진 범위 서비스(사본 횟수)뿐입니다. */
    struct SW_DEVTOOLS_API AutomationScratchWorkspace
    {
        /**
         * @brief 프로세스 꼬리를 가진 폴더(`…/<이름>_p<pid>` — `AutomationScratch::makeInstanceID`)를 비우고, 같은 이름의 다른 폴더 가운데 끝난 프로세스의 것을 지웁니다.
         * @details 사본 말고도 프로세스마다 따로 두는 폴더(시나리오 실행의 에디터 상태)가 같은 거두기를 쓴다. 못 지운 것은 `FileUtil` 이 경고를 남긴다.
         */
        static void resetInstanceFolder( const string& instanceDir );
        /**
         * @brief 사본 폴더를 지우고 원본을 복사합니다. 글 파일의 원본 id 는 사본 id 로 바꿉니다.
         * @details 사본 id 가 프로세스 꼬리(`AutomationScratch::makeInstanceID`)를 가졌으면, 같은 이름의 다른 사본 가운데 끝난 프로세스의 것을 먼저 지운다(죽은 실행의 사본이 쌓이지 않게).
         * @return 선언이 틀렸거나 원본이 없거나 지우기 · 복사 · 쓰기가 실패하면 오류입니다(시나리오는 시작하지 않는다 — 원본에 쓰는 길로 돌아가지 않는다).
         */
        [[nodiscard]] static expected<void> prepare( const AutomationScratchFolder& folder );
        /**
         * @brief 사본 폴더를 끝까지 만든 횟수입니다(`prepare` 성공마다 하나 오른다). 사본을 만든 뒤 OS 파일 감시가 그 쓰기를 알려야 한다 — 에디터가 이 번호가 바뀌면
         *        감시 울타리를 다시 세운다(`AssetHotReload` 의 시작 차단 조건).
         */
        [[nodiscard]] static uint64 getPrepareCount();
        /**
         * @brief 쓰기 입구 게이트를 세웁니다 — 실행기가 시나리오를 시작할 때 한 번 부르고 끝날 때 `releaseWriteGate` 로 짝을 맞춥니다(`FileUtil::setWriteGate`). 시나리오는 한 프로세스에서 차례로 돌므로 셈을 두지 않는다.
         * @details 게이트가 서 있는 동안 리소스 루트 아래에서 `scratch/` 밖으로 가는 모든 파일 쓰기(저장 · Quick Open 으로 연 원본 포함)는 오류와 함께 거절된다.
         *          프로젝트 루트의 추적 폴더 `docs/` · `Config/` 도 같은 문이 막는다(`Config/Editor/` 만 허용 목록 — 에디터 설정 저장 길).
         */
        static void acquireWriteGate();
        /** @brief `acquireWriteGate` 의 짝입니다. 게이트를 걷습니다. */
        static void releaseWriteGate();
    };
} // namespace sw
