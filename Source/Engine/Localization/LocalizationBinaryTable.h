/**
 * @file LocalizationBinaryTable.h
 * @brief 쿠킹된 이진 번역 표(`.locres`)를 정의합니다. 문화권 하나의 "키 해시 → 화면에 나오는 글" 표를 한 덩어리 바이트로 굳힌 것입니다(언리얼 `.locres`).
 * @details JSON 저작 파일(`*.strings.json`, `<문화권>.translation.json`)은 읽을 때마다 파싱하고 낡은 번역을 가려내야 하지만, 이 표는 **이미 가려낸 결과**라서
 *          그대로 실행 테이블(`StringTable`)에 넣으면 된다. 키 문자열은 담지 않는다(조회는 해시로만 한다) — 원문 수집 · 번역 가져오기에는 JSON 이 계속 정본이다.
 *          형식(리틀 엔디언):
 * @code
 *   u32 magic("SWLR")  u32 version  u32 cultureLength  u8 culture[]  u32 entryCount  u32 blobBytes
 *   entryCount x { u64 keyHash  u32 offset  u32 length }      ← 키 해시 오름차순
 *   u8 blob[blobBytes]                                         ← UTF-8 글, 끝 표시 없음
 *   u64 checksum                                               ← 앞의 모든 바이트의 해시
 * @endcode
 *          읽기는 길이 · 범위 · 검사합을 모두 확인하고, 하나라도 틀리면 거절한다(깨진 파일이 일부만 적용되지 않는다).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @class LocalizationBinaryTable
     * @brief 이진 번역 표 하나(문화권 하나)입니다. 만들고 → `toBytes`, 또는 `parse` → 읽습니다.
     */
    class SW_API LocalizationBinaryTable
    {
    public:
        static constexpr uint32      kMagic     = FourCcUtil::make( "SWLR" );
        static constexpr uint32      kVersion   = 1;
        static constexpr const utf8* kExtension = ".locres";

        /** @brief 키(`Menu.Start`)의 글을 넣습니다. 같은 키(해시)는 바꿉니다. */
        void setText( string_view key, string_view text );
        /** @brief 키 해시의 글을 넣습니다. */
        void setTextByHash( uint64 keyHash, string_view text );

        /** @brief 바이트로 굳힙니다(해시 오름차순이라 같은 내용이면 같은 바이트입니다). */
        void toBytes( vector<uint8>& outBytes ) const;
        /** @brief 바이트를 읽습니다. 머리 · 범위 · 검사합이 틀리면 @p pOutError 에 이유를 적고 false 이며, 이 객체는 비워집니다. */
        [[nodiscard]] bool parse( const uint8* pData, size_t size, string* pOutError = nullptr );

        const string&              getCulture() const { return _culture; }
        void                       setCulture( string_view culture );
        const map<uint64, string>& getTexts() const { return _mapText; }

    private:
        string              _culture;
        map<uint64, string> _mapText;
    };
} // namespace sw
