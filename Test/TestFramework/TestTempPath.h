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

    /** @brief 프로세스 임시 폴더를 붙이는 리소스 도메인 이름입니다(`makeTempResourceID`). */
    inline constexpr const utf8* kTempResourceDomain = "testtemp";

    /**
     * @brief 프로세스 임시 폴더 아래 디스크 경로(`makeTempPath` · `makeTempDirectory` 가 준 것)를 리소스 id(`testtemp/<...>`)로 바꿉니다.
     * @details 리소스 입구(`ResourceIO::read*Resource`, 그 위의 로더 · 캐시)는 OS 경로를 받지 않는다(결정 기록 5-4 안 B). 임시 파일을
     *          리소스로 읽는 케이스는 쓰기에는 디스크 경로를, 읽기에는 이 id 를 쓴다. 처음 부르면 프로세스 임시 폴더를 도메인 `testtemp` 로 붙인다
     *          (`ResourcePaths::mountDiskDomain` — 낱개 파일을 막는 배포 구성에서도 디스크에서 읽힌다).
     * @return 프로세스 임시 폴더 밖 경로이거나 도메인을 붙이지 못하면 케이스를 실패로 기록하고 빈 문자열입니다.
     */
    sw::string makeTempResourceID( sw::string_view tempDiskPath );
    /** @brief 이 케이스의 임시 폴더 — `<임시 폴더>/sw_<pid>/<스위트_케이스>`(케이스 이름이 비면 프로세스 폴더). */
    sw::string getCaseTempDirectory( const sw::string& testName );
} // namespace test

namespace test
{
    /**
     * @brief 이 케이스만 쓰는 리소스 루트입니다. 케이스 임시 폴더에 `Resource` 를 만들어 `ResourcePaths` 의 루트를 그리로 바꾸고, 범위가 끝나면 되돌립니다.
     * @details 소스 트리의 `Resource/` 는 `ctest -j` 로 나란히 도는 모든 테스트 프로세스가 같이 읽는다. 한 케이스가 거기에 파일을 넣었다 빼면
     *          그 사이에 그 폴더를 훑는 다른 프로세스(셰이더 쿠킹 스탬프, 검색 우선순위)가 진다. 도메인 · 검색 우선순위 · 셰이더 디스크 캐시처럼
     *          "루트 아래 폴더" 로만 풀리는 것을 테스트할 때 이 루트를 쓴다.
     *
     *          파일은 `writeTextFile` 로 넣고 경로는 `makePath` 로 얻는다. 둘 다 **루트 기준 상대 경로**만 받고, 절대 경로나 `..` 는 그 자리에서
     *          케이스를 떨어뜨린다 — 소스 트리 경로를 넘길 길이 없다. 폴더는 케이스 임시 폴더라 케이스가 끝나면 프레임워크가 지운다.
     * @note 루트 교체는 프로세스 전역이다. 다른 스레드가 같은 때 경로를 풀면 임시 루트를 본다(`ResourcePaths::exchangeRootFolderPath`).
     */
    class ScopedTempResourceRoot
    {
    public:
        ScopedTempResourceRoot();
        ~ScopedTempResourceRoot();

        ScopedTempResourceRoot( const ScopedTempResourceRoot& )            = delete;
        ScopedTempResourceRoot& operator=( const ScopedTempResourceRoot& ) = delete;

        /** @brief 임시 루트의 절대 경로(정규화)입니다. */
        const sw::string& getRootFolderPath() const { return _rootFolderPath; }

        /**
         * @brief 루트 기준 상대 경로를 임시 루트 아래 절대 경로로 바꿉니다.
         * @return 절대 경로나 `..` 가 든 경로면 케이스를 실패로 기록하고 빈 문자열을 돌려준다.
         */
        sw::string makePath( sw::string_view relativePath ) const;

        /** @brief 임시 루트 아래 @p relativePath 에 글을 씁니다(부모 폴더를 만든다). 경로가 루트 밖이면 쓰지 않고 실패합니다. */
        [[nodiscard]] bool writeTextFile( sw::string_view relativePath, sw::string_view text ) const;

    private:
        sw::string _rootFolderPath;
        sw::string _sourceRootFolderPath;
    };
} // namespace test
