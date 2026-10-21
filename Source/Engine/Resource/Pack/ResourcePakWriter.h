/**
 * @file ResourcePakWriter.h
 * @brief .pack(SWPK) 아카이브를 쓰는 곳 하나입니다. 쿠커(`PackCooker`)와 테스트(`ResourcePakTestUtil`)가 같이 씁니다.
 * @details 레이아웃은 `ResourcePakTypes.h` 의 구조체이고, 리더(`ResourcePakReader`)가 같은 구조체를 읽습니다. 쓰는 곳이 하나라 "쓰는 쪽과 읽는 쪽의
 *          오프셋이 어긋난 팩" 이 형식으로 생기지 않습니다(예전에는 파이썬 쿠커 · 테스트 도우미 · 리더가 레이아웃을 따로 들었다).
 *          - 배치: 헤더 · 섹터 정렬 FAT · 섹터 정렬 페이로드 · 섹터 정렬 스트링 풀(`ResourcePakTypes.h` 머리말). FAT 은 경로 해시 오름차순이다.
 *          - 압축은 팩 단위다. 모든 항목을 같은 코덱으로 넣는다(리더가 헤더의 코덱 하나로 푼다). 빈 항목은 코덱을 거치지 않고 0 바이트로 둔다 —
 *            리더는 원본 크기 0 인 항목에 압축 바이트가 있으면 거절한다.
 *          - 같은 경로(대소문자 무시 해시가 같은 경로)를 두 번 담으면 쓰기가 실패한다. 리더는 해시 하나에 항목 하나만 들므로 어느 쪽이 이길지 조용히
 *            정하지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Resource/Pack/ResourcePakTypes.h"

namespace sw
{
    /** @struct ResourcePakWriterDesc @brief 팩 하나의 쓰기 설정입니다. 모두 필수입니다. */
    struct ResourcePakWriterDesc
    {
        PackCompressionType _compression{ PackCompressionType::None };
        int32               _compressionLevel{ 0 }; ///< 코덱의 레벨(0 = 코덱 기본값)
        uint32              _dlcAppID{ 0 };         ///< 0: 본편 · 공용, >0: DLC AppID
    };
} // namespace sw

namespace sw
{
    /**
     * @class ResourcePakWriter
     * @brief 항목을 담고(`addBytes`) 팩 바이트를 만듭니다(`makePackBytes` · `writePackFile`). 파일 머리말 참고.
     */
    class SW_API ResourcePakWriter
    {
    public:
        explicit ResourcePakWriter( const ResourcePakWriterDesc& desc );

        /** @brief 팩 안 경로 @p relativePath(구분자는 '/' 로 맞춘다)에 @p bytes 를 담습니다. */
        void addBytes( string_view relativePath, vector<uint8> bytes );

        /** @brief 담은 항목 수입니다. */
        uint32 getEntryCount() const { return static_cast<uint32>( _listEntry.size() ); }

        /**
         * @brief 담은 항목으로 팩 바이트를 만듭니다.
         * @return 같은 경로가 두 번 담겼거나, 코덱이 없거나, 압축 · 크기 한계에서 실패하면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool makePackBytes( vector<uint8>& outBytes ) const;

        /** @brief `makePackBytes` 의 결과를 @p packPath 에 원자적으로 씁니다(부모 폴더를 만든다). */
        [[nodiscard]] bool writePackFile( string_view packPath ) const;

    private:
        /** @brief 담은 항목 하나입니다. */
        struct Entry
        {
            uint64        _pathHash{ 0 };
            string        _relativePath;
            vector<uint8> _bytes;
        };

        /** @brief 항목 하나를 팩 코덱으로 압축합니다. 빈 항목과 `None` 은 그대로 둡니다. */
        [[nodiscard]] bool compressEntry( const Entry& entry, vector<uint8>& outPayloadBytes ) const;

        vector<Entry>         _listEntry;
        ResourcePakWriterDesc _desc;
    };
} // namespace sw
