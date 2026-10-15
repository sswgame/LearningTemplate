/**
 * @file Test/TestFramework/TestTempPath.h
 * @brief 케이스마다의 임시 경로 — 프로세스 · 케이스별 폴더를 주고, 케이스가 끝나면 프레임워크가 통째로 지운다
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace test
{
    /**
     * @brief 이 프로세스·이 케이스만 쓰는 임시 파일 경로를 만듭니다.
     * @param fileName 쓰려는 파일 이름. 확장자는 그대로 남는다(로더가 그것으로 형식을 고른다).
     * @return `<임시 폴더>/sw_<pid>/<스위트_케이스>/<fileName>` — 그 폴더는 만들어 둔다.
     * @details `%TEMP%/test_malformed.wav` 처럼 **고정된 이름**에 쓰면 안 된다. 같은 `EngineTest` 가 네 프리셋에서
     *          각각 돌고 CI 는 그것들을 나란히 돌리므로, 한쪽의 `removeFile` 이 다른 쪽이 방금 쓴 파일을 지워
     *          간헐 실패가 된다. 프로세스 id 를 섞으면 그 충돌이 사라지고, 케이스 이름까지 섞으면
     *          **한 프로세스 안에서 같은 이름을 쓰는 두 케이스**도 서로를 안 밟는다.
     *
     *          **케이스가 끝나면 프레임워크가 그 케이스 폴더를 통째로 지운다** — `SW_TEST_DEFER_CLEANUP` 이 돈 뒤에. 엔진이 옆에
     *          쿠킹해 둔 `.bin` · `.meta` 도 같은 폴더라 함께 지워진다. 케이스가 끝에서 `removeFile` 을 손으로 부르면 단언으로 일찍
     *          빠질 때 그 줄에 닿지 않아 남는다.
     *          못 지우면(열린 핸들) 그 케이스가 진다. 폴더를 케이스마다 두는 것은 정리가 **훑기 없이** 끝나게 하려는 것이다
     *          (임시 폴더 전체를 이름으로 훑으면 케이스마다 수십 ms 가 든다).
     * @note 케이스 밖에서 부르면 프로세스 폴더(`sw_<pid>`)에 생기고, 실행이 끝날 때 지워진다.
     */
    sw::string makeTempPath( sw::string_view fileName );

    /**
     * @brief `makeTempPath` 로 이름을 지은 **폴더**를 만들어 돌려줍니다. 케이스가 끝나면 통째로 지워진다.
     * @details 파일 이름 자체가 뜻을 갖는 경우(팩 이름이 우선순위를 정한다 — `engine_` · `game_` · `patch_`, 파서 산출물 이름이
     *          헤더 이름을 따른다) 그 이름을 지킨 채 이 폴더 안에 둔다. 같은 이름으로 다시 부르면 같은 폴더다.
     */
    sw::string makeTempDirectory( sw::string_view directoryName );

    /** @brief 이 프로세스의 임시 폴더 — `<임시 폴더>/sw_<pid>`. 케이스 폴더가 전부 그 아래에 생긴다(실행기가 끝에 거둔다). */
    sw::string getProcessTempDirectory();
    /** @brief 이 케이스의 임시 폴더 — `<임시 폴더>/sw_<pid>/<스위트_케이스>`(케이스 이름이 비면 프로세스 폴더). */
    sw::string getCaseTempDirectory( const sw::string& testName );
} // namespace test
