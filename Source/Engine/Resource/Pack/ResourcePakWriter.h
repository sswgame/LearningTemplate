/**
 * @file ResourcePakWriter.h
 * @brief .pack(SWPK) 아카이브를 쓰는 곳 하나입니다. 쿠커(`PackCooker`)와 테스트(`ResourcePakTestUtil`)가 같이 씁니다.
 * @details 레이아웃은 `ResourcePakTypes.h` 의 구조체이고, 리더(`ResourcePakReader`)가 같은 구조체를 읽습니다. 쓰는 곳이 하나라 "쓰는 쪽과 읽는 쪽의
 *          오프셋이 어긋난 팩" 이 형식으로 생기지 않습니다(예전에는 파이썬 쿠커 · 테스트 도우미 · 리더가 레이아웃을 따로 들었다).
 *          - 배치: 헤더 · 섹터 정렬 FAT · 섹터 정렬 페이로드 · 섹터 정렬 스트링 풀(`ResourcePakTypes.h` 머리말). FAT 은 경로 해시 오름차순이다.
 *          - 압축은 팩 단위다. 모든 항목을 같은 코덱으로 넣는다(리더가 헤더의 코덱 하나로 푼다). 빈 항목은 코덱을 거치지 않고 0 바이트로 둔다 —
 *            리더는 원본 크기 0 인 항목에 압축 바이트가 있으면 거절한다.
 *          - 바이트 안정성(스팀 청크 차이 받기용, 테스트 `PackLayoutTest.*`): 항목은 경로 해시 순으로 놓이고 항목별로 압축하므로 같은 입력이면 담는 순서와
 *            상관없이 같은 바이트다. 항목 하나가 바뀌어도 그 항목의 페이로드 · FAT 칸 밖은 그대로다. 단 항목을 더하거나 지우거나 섹터를 넘게 키우면 해시 순으로
 *            뒤에 오는 페이로드가 섹터 단위로 밀린다(내용은 그대로) — 고정 1MB 청크 비교에는 불리하고 내용 기준 청크에는 거의 영향이 없다. 밀림을 없애려면
 *            항목마다 빈 자리를 두는 형식 변경이 필요해 하지 않는다.
 *          - 항목별 압축 · CRC 는 병렬이고(`engine::runParallel`, 항목마다 자기 결과 칸에만 쓴다) 오프셋 배치 · 바이트 조립은 경로 해시 순으로 직렬이다. 압축 크기를
 *            안 뒤에야 오프셋이 정해지므로 배치는 직렬 루프 그대로이고, 항목 정렬 키가 경로 해시라 병렬이어도 팩 바이트는 직렬로 돌 때와 비트 단위로 같다
 *            (테스트 `PackLayoutTest.ParallelCompressionGivesTheSerialBytes`). 엔진 서비스가 없거나 워커가 없으면 호출 스레드가 차례로 돈다.
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
     * @struct ResourcePakWriteStats
     * @brief 팩 하나를 만들고 쓰는 데 든 단계별 시간(ns)입니다. 쿠킹 요약 로그가 모아 병렬 효과를 보입니다. 단계는 겹치지 않으므로 합이 곧 쓰기에 든 시간입니다.
     */
    struct ResourcePakWriteStats
    {
        int64  _compressWallNanos{ 0 }; ///< 압축 · CRC 구간의 벽시계 시간(병렬이면 가장 느린 스레드가 정한다)
        int64  _compressCPUNanos{ 0 };  ///< 항목마다 잰 압축 · CRC 시간의 합(직렬로 돌았다면 든 시간). 벽시계와의 비가 병렬 효과다
        int64  _layoutNanos{ 0 };       ///< 정렬 · 중복 검사 · 오프셋 배치 · 바이트 조립
        int64  _writeNanos{ 0 };        ///< 부모 폴더를 만들고 파일을 쓴 시간(`writePackFile` 만)
        uint64 _inputBytes{ 0 };        ///< 담은 원본 바이트의 합
        uint32 _entryCount{ 0 };        ///< 항목 수

        /** @brief @p other 의 시간 · 크기를 더합니다(팩 여럿을 한 줄로 요약할 때). */
        void add( const ResourcePakWriteStats& other )
        {
            _compressWallNanos += other._compressWallNanos;
            _compressCPUNanos += other._compressCPUNanos;
            _layoutNanos += other._layoutNanos;
            _writeNanos += other._writeNanos;
            _inputBytes += other._inputBytes;
            _entryCount += other._entryCount;
        }
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
        [[nodiscard]] bool makePackBytes( vector<uint8>& outBytes, ResourcePakWriteStats* pOutStats = nullptr ) const;

        /** @brief `makePackBytes` 의 결과를 @p packPath 에 원자적으로 씁니다(부모 폴더를 만든다). @p pOutStats 가 있으면 단계별 시간을 담습니다. */
        [[nodiscard]] bool writePackFile( string_view packPath, ResourcePakWriteStats* pOutStats = nullptr ) const;

    private:
        /** @brief 담은 항목 하나입니다. */
        struct Entry
        {
            uint64        _pathHash{ 0 };
            string        _relativePath;
            vector<uint8> _bytes;
        };

        /** @brief 항목 하나의 압축 결과 칸입니다(정의는 .cpp). 병렬 구간에서 항목마다 자기 칸에만 씁니다. */
        struct CompressSlot;
        /** @brief 항목들을 압축하는 병렬 본문과 그 입력입니다(정의는 .cpp). */
        struct CompressJob;

        vector<Entry>         _listEntry;
        ResourcePakWriterDesc _desc;
    };
} // namespace sw
