/**
 * @file UserDataPath.h
 * @brief 사용자 데이터 · 설정 폴더 — 사용자 설정 · 로컬 저장(세이브 · 프로필) · 장치 키가 같은 한 곳에서 경로를 얻습니다.
 * @details - Windows: 데이터 · 설정 모두 `%LOCALAPPDATA%/SWEngine/<game>/`.
 *          - 리눅스: 데이터는 `$XDG_DATA_HOME`(없으면 `~/.local/share`)`/swengine/<game>/`, 설정은 `$XDG_CONFIG_HOME`(없으면 `~/.config`)`/swengine/<game>/`.
 *          - 사용자 폴더를 모르면(환경 변수 없음) 멈춘다(`SW_USER_DATA_ROOT` 로 정한다 - 작업 폴더로 대신하지 않는다). `<game>` 은 소문자, 비면 `default`.
 *          - 환경 변수 `SW_USER_DATA_ROOT`(`kUserDataRootVariable`)가 있으면 데이터 · 설정 모두 그 아래 `<game>/` 이다. 엔진이 읽는 위치만 가리키므로(`LOCALAPPDATA` 를 옮기면
 *            드라이버 셰이더 캐시 같은 남의 폴더도 따라간다) 자동화가 `-gv_userSettingsFile` 을 모르는 배포본의 사용자 설정 · 세이브를 실행별 폴더로 가를 때 쓴다.
 *          언리얼 `FPlatformProcess::UserSettingsDir` · 유니티 `Application.persistentDataPath` 에 해당한다. 모바일 샌드박스는 지원 플랫폼이 되면 분기 하나로 추가한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

namespace sw
{
    /** @brief 사용자 데이터 · 설정 폴더의 뿌리를 덮어쓰는 환경 변수 이름입니다(자동화용 — 정본은 이 줄이고 파이썬 러너가 이 헤더에서 읽습니다). */
    inline constexpr const utf8* kUserDataRootVariable = "SW_USER_DATA_ROOT";

    /**
     * @struct UserDataPath
     * @brief 사용자 폴더 경로 계산입니다(폴더를 만들지는 않는다).
     */
    struct SW_API UserDataPath
    {
        /** @brief 데이터 폴더(세이브 · 로컬 저장 · 장치 키)입니다. 끝에 구분자가 없습니다. */
        static string getDataDirectory( string_view gameName );
        /** @brief 설정 폴더(`usersettings.json`)입니다. 끝에 구분자가 없습니다. */
        static string getConfigDirectory( string_view gameName );
    };
} // namespace sw

namespace sw
{
    /**
     * @class LocalStoreLocation
     * @brief 로컬 저장(세이브 · 슬롯 저장소)을 두는 자리입니다. 종류(메모리 · 사용자 데이터 폴더 아래 · 절대 경로)는 만드는 쪽이 고른다.
     * @details 예전 `UserDataPath::resolve` 는 글을 보고 추측했다 - `:memory:` 면 그대로, 느슨한 절대(`C:foo` 도)면 그대로, 아니면 데이터 폴더 아래.
     *          그래서 데이터에 적은 드라이브 상대 경로가 "절대"로 통했고 SQLite 의 `:memory:` 약속이 파일 저장소에도 통했다. 언리얼 `ISaveGameSystem` 은 플랫폼이 자리를 정하고
     *          게임은 슬롯 이름만 준다 - 여기도 데이터는 사용자 데이터 폴더 아래 상대 경로만 적고(`tryUnderUserData`), 절대 경로 · 메모리는 코드(테스트 · 도구)만 고른다.
     */
    class SW_API LocalStoreLocation
    {
    public:
        enum class Kind : uint8
        {
            Memory,        ///< 디스크 없음(테스트 - 바닥이 메모리 · SQLite `:memory:` 를 연다)
            UnderUserData, ///< 사용자 데이터 폴더(`UserDataPath::getDataDirectory`) 아래 상대 경로
            Absolute,      ///< 엄격한 절대 경로
        };

        [[nodiscard]] static LocalStoreLocation memory();
        /** @brief 사용자 데이터 폴더 아래 상대 경로 @p relativePath 입니다. 빈 글 · 절대 꼴(드라이브 상대 `C:foo` · 드라이브 없는 루트 포함) · `..` 마디는 빈 값입니다(데이터 오류 - 부르는 쪽이 알린다). */
        [[nodiscard]] static std::optional<LocalStoreLocation> tryUnderUserData( string_view relativePath );
        [[nodiscard]] static LocalStoreLocation                absolute( const AbsolutePath& path );

        [[nodiscard]] Kind getKind() const { return _kind; }
        /** @brief 디스크 자리입니다 - 사용자 데이터 폴더 아래면 `<데이터 폴더>/<상대>`, 절대면 그대로. 메모리 자리는 디스크가 없어 멈춥니다(부르는 쪽이 종류를 먼저 본다). */
        [[nodiscard]] string resolveDiskPath( string_view gameName ) const;

    private:
        LocalStoreLocation( Kind kind, string path );

        string _path; ///< 사용자 데이터 폴더 아래면 상대, 절대면 절대 경로 글. 메모리면 빈 글
        Kind   _kind;
    };
} // namespace sw
