/**
 * @file CookRecord.h
 * @brief 쿠킹 작업 하나의 기록(`<출력>.cookrec`)과 파생 데이터 캐시(DDC)입니다. 다음 쿠킹은 기록의 입력만 다시 해시해 같으면 DDC 의 출력을 그대로 씁니다.
 * @details 기록은 키, 출력 해시, 입력(리소스 id 와 내용 해시) 목록을 글로 적는다. 판정은 파일 시각이 아니라 내용 해시다(`ContentHash`).
 *          기록이 없거나 형식이 다르거나(머리 줄) 깨졌으면 다시 베이크한다 — 옛 형식을 읽지 않는다.
 *          DDC 는 키의 내용 주소 폴더(`<루트>/<키 앞 두 글자>/<키>`)다. 로컬 루트는 `<프로젝트>/Saved/DDC` 이고, 공유 층은 아직 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/FileSystem/CookJob.h"

namespace sw
{
    /**
     * @struct CookRecord
     * @brief 작업 하나의 기록입니다.
     */
    struct SW_API CookRecord
    {
        /** @brief 기록 파일 머리 줄입니다. 형식을 바꾸면 버전을 올린다 — 옛 기록은 다시 베이크하는 이유가 된다. */
        static constexpr string_view kHeader{ "SWCOOKREC 3" }; // 3: 입력 종류 셋(resource · disk · listing) - 옛 기록은 거절되어 한 번 다시 굽는다

        uint64            _key{ 0 };
        uint64            _outputHash{ 0 };
        vector<CookInput> _listInput;

        /** @brief 끝난 작업 @p job 과 출력 바이트로 기록을 만듭니다. */
        static CookRecord make( const CookJob& job, const vector<uint8>& outputBytes );
        /** @brief 출력 해시를 이미 구했을 때의 기록 만들기입니다(`CookStore` 가 시간을 재며 한 번만 해시한다 — 바이트를 다시 훑지 않는다). */
        static CookRecord make( const CookJob& job, uint64 outputHash );

        /** @brief 기록을 글로 씁니다. */
        [[nodiscard]] bool write( string_view absPath ) const;
        /**
         * @brief 기록을 읽습니다. 파일이 없으면 false(조용히 — 처음 베이크하는 작업), 머리 줄이 다르거나 줄이 깨졌으면 이유를 경고하고 false 입니다.
         */
        [[nodiscard]] static bool read( string_view absPath, CookRecord& outRecord );

        /**
         * @brief 이 기록의 출력을 다시 써도 되는지 판정합니다. 입력마다 지금 내용 해시가 기록과 같고, @p desc 로 다시 구한 키가 기록의 키와 같아야 합니다.
         * @details 키 비교가 쿠커 버전 · 대상 · 설정이 바뀐 것을 잡는다. 입력 경로는 리소스 id 라 워크트리가 달라도 같은 기록이 맞는다.
         */
        bool isFresh( const CookJobDesc& desc ) const;
        /** @brief 지금 해시가 기록과 다른 첫 입력입니다. 모두 같으면 nullptr 입니다(다시 베이크하는 이유를 알릴 때). */
        const CookInput* findChangedInput() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class DerivedDataCache
     * @brief 키로 쿠킹 출력을 저장하고 찾는 폴더입니다.
     */
    class SW_API DerivedDataCache
    {
    public:
        explicit DerivedDataCache( string rootPath );

        /** @brief 로컬 DDC 루트(`<프로젝트>/Saved/DDC`)입니다. */
        static string makeLocalRootPath();

        /** @brief 키의 출력을 읽습니다. 없으면 false 입니다. */
        [[nodiscard]] bool find( uint64 key, vector<uint8>& outBytes ) const;
        /** @brief 키의 출력을 씁니다. 같은 키가 이미 있으면 덮어씁니다(같은 입력은 같은 바이트여야 한다 — 쿠킹은 결정적이다). */
        [[nodiscard]] bool store( uint64 key, const vector<uint8>& bytes ) const;
        /** @brief 키의 파일 경로입니다. */
        string makePath( uint64 key ) const;

    private:
        string _rootPath;
    };
} // namespace sw
