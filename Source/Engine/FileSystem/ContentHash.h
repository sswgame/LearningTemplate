/**
 * @file ContentHash.h
 * @brief 쿠킹 판정이 쓰는 파일 내용 해시(CR 을 뺀 바이트의 FNV-1a 64)와 그 캐시입니다. 셰이더 쿠킹 스탬프와 쿠킹 기록(`CookRecord`)이 같은 값을 씁니다.
 * @details 판정은 파일 시각이 아니라 내용이다 — 이 저장소는 `git pull` 이 소스와 산출물의 시각을 임의의 순서로 바꾼다.
 *          캐시는 파일 크기와 쓰기 시각이 그대로일 때만 다시 읽지 않는다(셰이더 요청 하나가 같은 소스를 여러 번 본다 — 한 번 여는 데 약 200µs).
 *          셰이더 스탬프를 쓰는 쪽과 판정하는 쪽(`ShaderCookStamp.cpp`)이 같은 함수를 쓴다.
 *          **소유.** 캐시는 서비스 범위의 서브시스템 `ContentHashCache` 가 갖고(`EngineSubsystemSet`), 창구(`ContentHash::computeFile`)는 서비스 표의 칸으로 찾는다.
 *          예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다. 언리얼 DDC 의 파일 해시 캐시(`FFileHashCache`)가 엔진 쪽 객체에 있는 자리.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /**
     * @class ContentHash
     * @brief 파일 · 바이트의 내용 해시입니다. 0 은 "읽지 못함" 입니다(내용 해시가 0 인 파일은 사실상 없다).
     */
    class SW_API ContentHash
    {
    public:
        /** @brief 바이트의 내용 해시입니다(CR 을 빼고 FNV-1a 64). 사본을 만들지 않고 한 번에 훑는다. */
        static uint64 computeBytes( const uint8* pData, size_t size );
        /** @brief 바이트의 내용 해시입니다. */
        static uint64 computeBytes( const vector<uint8>& bytes ) { return computeBytes( bytes.data(), bytes.size() ); }
        /** @brief 파일의 내용 해시입니다. 크기와 쓰기 시각이 지난번과 같으면 파일을 열지 않습니다(`ContentHashCache`). 읽지 못하면 0 입니다. 엔진 서비스가 없으면 멈춘다. */
        static uint64 computeFile( string_view absPath );
        /** @brief 캐시를 비웁니다(테스트가 같은 경로의 파일을 같은 시각에 다시 쓸 때, 수동 리로드). */
        static void clearCache();
    };
} // namespace sw

namespace sw
{
    /**
     * @class ContentHashCache
     * @brief 경로 → (파일 크기 · 쓰기 시각, 내용 해시) 캐시입니다(서비스 범위의 서브시스템). `ContentHash` 창구만 부릅니다.
     * @details 셰이더 컴파일 · 쿠킹이 워커에서도 부르므로 안에서 잠근다. 항목은 기동 뒤 첫 셰이더 반사 · 쿠킹이 채우고 서비스 범위가 내려갈 때 돌아간다.
     */
    class SW_API ContentHashCache final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "ContentHashCache";

        // ISubsystem — 세울 것이 없다(처음 묻는 파일부터 채운다).
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief `ContentHash::computeFile` 의 본문입니다. */
        uint64 computeFile( string_view absPath ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief `ContentHash::clearCache` 의 본문입니다. */
        void clear() SW_EXCLUDES_LOCK( _mutex );

    private:
        /** @brief 항목 하나입니다. 그 해시를 구했을 때의 파일 크기와 쓰기 시각을 함께 둡니다. */
        struct Entry
        {
            FileStamp _stamp{};
            uint64    _hash{ 0 };
        };

        mutex                                  _mutex;
        unordered_map<string, Entry> _mapEntry SW_GUARDED_BY( _mutex );
    };
} // namespace sw
