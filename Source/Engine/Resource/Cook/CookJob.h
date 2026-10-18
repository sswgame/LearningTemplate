/**
 * @file CookJob.h
 * @brief 쿠킹 작업 하나(씬 하나, 프리팹 하나, 팩 하나)의 범위입니다. 범위 안의 모든 리소스 읽기가 그 작업의 입력으로 기록됩니다.
 * @details 쿠커는 의존 목록을 따로 적지 않는다 — 씬이 프리팹을 읽고 프리팹이 머티리얼을 읽으면 그 읽기가 모두 씬 작업의 입력이 된다(`ResourceIO` 가 기록).
 *          빠뜨릴 수 있는 손 목록이 없으므로 "의존을 잊은 낡은 결과" 가 형식으로 생기지 않는다(유니티 Asset Database v2 의 동적 의존과 같은 방식).
 *          - 범위는 스레드 지역 값이다. 병렬 쿠킹에서도 읽기가 다른 작업에 섞이지 않는다. 안쪽 범위의 입력은 바깥 범위에도 기록된다.
 *          - 쿠킹 세션(`CookSession`)이 열려 있는 동안 범위 밖에서 일어난 읽기는 오류다(읽기가 실패한다). 전역 캐시가 미리 읽어 둔 것을 쓰면 기록이 빠지기 때문이다.
 *          - 작업 키 = 해시(쿠커 이름, 쿠커 버전, 대상, 설정 해시, 정렬한 입력 목록)이다(`computeKey`). 쿠커 버전은 쿠커 소스 옆의 사람이 올리는 상수다.
 *          - 입력은 둘이다. 파일(읽은 내용의 해시 — 없는 파일을 찾은 것도 해시 0 으로 남는다)과 목록(폴더 아래 확장자가 같은 파일 이름들의 해시 — `collectFiles`).
 *            폴더를 훑어 정한 출력(레지스트리)은 파일이 하나 더 생겨도 읽은 파일이 바뀌지 않으므로 목록이 입력이어야 한다.
 *          - 리소스 id 는 `ResourcePaths::toResourceID` 로 맞춘다(리소스 루트 밖의 파일은 받은 경로 그대로). 같은 파일을 절대 경로와 id 로 읽어도 입력 하나다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 입력의 종류입니다. */
    enum class CookInputKind : uint8
    {
        File,    ///< 파일 하나의 내용. 없으면 해시 0
        Listing, ///< 폴더(`_resourceID`) 아래(하위 폴더 포함) 확장자(`_extension`)가 같은 파일의 상대 경로 목록
    };

    /** @struct CookInput @brief 작업이 읽은 입력 하나(리소스 id 와 그때의 내용 해시)입니다. */
    struct CookInput
    {
        CookInputKind _kind{ CookInputKind::File };
        string        _resourceID;
        string        _extension; ///< 목록일 때만 쓴다(예: ".meta")
        uint64        _contentHash{ 0 };

        bool operator==( const CookInput& other ) const
        {
            return _kind == other._kind && _contentHash == other._contentHash && _resourceID == other._resourceID && _extension == other._extension;
        }
        /** @brief 같은 입력(종류 · id · 확장자)이면 true 입니다(해시는 보지 않는다). */
        bool isSameSource( const CookInput& other ) const { return _kind == other._kind && _resourceID == other._resourceID && _extension == other._extension; }
        /** @brief 이 입력의 지금 해시입니다. 파일은 내용 해시(없으면 0), 목록은 지금 목록의 해시입니다. 기록이 새것인지 이 값으로 본다. */
        SW_API uint64 computeCurrentHash() const;
    };
} // namespace sw

namespace sw
{
    /** @struct CookJobDesc @brief 작업 키에 들어가는 쿠커 쪽 값입니다. 모두 필수입니다. */
    struct CookJobDesc
    {
        string _cookerName;         ///< 쿠커 이름(예: "Scene")
        uint32 _cookerVersion{ 0 }; ///< 쿠커 형식 버전 — 출력 형식을 바꾸면 올린다. 그 쿠커의 결과가 모두 다시 구워진다
        string _target;             ///< 대상 플랫폼 · 백엔드(예: "Windows")
        uint64 _settingsHash{ 0 };  ///< 쿠킹 설정의 해시(설정이 없으면 0 을 명시한다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class CookSession
     * @brief 쿠킹 세션입니다. 살아 있는 동안 작업 범위(`CookJob`) 밖의 리소스 읽기가 오류가 됩니다. 쿠킹 진입점(`--cook-scenes` 등)이 하나 둡니다.
     */
    class SW_API CookSession
    {
    public:
        CookSession();
        ~CookSession();
        CookSession( const CookSession& )            = delete;
        CookSession& operator=( const CookSession& ) = delete;

        /** @brief 세션이 열려 있으면 true 입니다(프로세스 전체). */
        static bool isActive();
    };
} // namespace sw

namespace sw
{
    /**
     * @class CookJob
     * @brief 파일 머리말 참고. 스택에 만들고 범위가 끝나면 사라집니다.
     */
    class SW_API CookJob
    {
    public:
        explicit CookJob( const CookJobDesc& desc );
        ~CookJob();
        CookJob( const CookJob& )            = delete;
        CookJob& operator=( const CookJob& ) = delete;

        /** @brief 이 스레드의 가장 안쪽 작업입니다. 없으면 nullptr 입니다. */
        static CookJob* findCurrent();

        /**
         * @brief 리소스 읽기 하나를 받아들입니다(`ResourceIO` 가 읽은 바이트와 함께 부른다).
         * @return 이 스레드에 작업이 있으면 입력으로 기록하고 true, 작업이 없고 세션도 없으면 true, 세션 안인데 작업 밖이면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool admitRead( string_view resourceID, const uint8* pData, size_t size );
        /**
         * @brief 이 스레드에 작업이 있으면 비동기 읽기를 거절합니다(완료가 다른 스레드에서 와서 기록이 빠진다). 쿠커는 동기로 읽는다.
         * @return 거절했으면 false 입니다(오류를 남긴다).
         */
        [[nodiscard]] static bool admitAsyncRead( string_view resourceID );
        /**
         * @brief 찾지 못한 리소스 읽기를 받아들입니다. 이 스레드에 작업이 있으면 해시 0 으로 기록합니다.
         * @details 없는 파일을 찾은 결과로 출력이 정해질 수 있다 — 그 파일이 생기면 다시 구워야 한다.
         */
        static void admitMissingRead( string_view resourceID );

        /**
         * @brief @p folderPath 아래(하위 폴더 포함) @p extension 으로 끝나는 파일을 경로 순서로 모읍니다. 이 스레드에 작업이 있으면 그 목록을 입력으로 기록합니다.
         * @details 폴더를 훑어 출력을 정하는 쿠커는 `FileUtil::collectFiles` 대신 이것을 쓴다 — 파일이 생기거나 사라지면 다시 굽는다.
         */
        static void collectFiles( string_view folderPath, string_view extension, vector<string>& outListFile );
        /** @brief 목록 입력의 해시입니다. 폴더 기준 상대 경로를 정렬해 해시합니다. 폴더가 없으면 0 입니다. */
        static uint64 computeListingHash( string_view folderPath, string_view extension );

        /** @brief 파일 입력 하나를 기록합니다. 같은 리소스를 다시 읽으면 처음 것만 둡니다. 바깥 작업에도 기록합니다. */
        void recordInput( string_view resourceID, uint64 contentHash );
        /** @brief 입력 하나를 기록합니다. 같은 입력이 있으면 처음 것만 둡니다. 바깥 작업에도 기록합니다. */
        void recordInput( const CookInput& input );

        const CookJobDesc&       getDesc() const { return _desc; }
        const vector<CookInput>& getInputs() const { return _listInput; }
        /** @brief 지금까지 기록한 입력으로 작업 키를 구합니다. */
        uint64 computeKey() const { return computeKey( _desc, _listInput ); }

        /** @brief 작업 키입니다. 입력은 리소스 id 순서로 정렬해 넣으므로 읽은 순서와 관계없습니다. */
        static uint64 computeKey( const CookJobDesc& desc, const vector<CookInput>& listInput );

    private:
        CookJobDesc       _desc;
        vector<CookInput> _listInput;
        CookJob*          _pOuter;
    };
} // namespace sw
