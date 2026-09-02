/**
 * @file Defines.h
 * @brief 버퍼·경로 길이·무효 인덱스 등 공통 상수입니다.
 * @note 렌더/리소스 폴더 상수는 Engine/Common/EngineDefines.h
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 경로·스택 버퍼 상한
    // ------------------------------------------------------------------------------
    namespace constant
    {
        /** @brief 파일 시스템 최대 경로 길이 (Windows MAX_PATH 호환) */
        inline constexpr uint32 kMaxPathSize = 260;

        /** @brief 초소형 식별자/숫자용 스택 버퍼입니다. */
        inline constexpr uint32 kMaxBuffer16 = 16;
        /** @brief 소형 식별자/숫자용 스택 버퍼입니다. */
        inline constexpr uint32 kMaxBuffer32 = 32;
        /** @brief 단문자/속성 이름용 스택 버퍼입니다. */
        inline constexpr uint32 kMaxBuffer64 = 64;
        /** @brief 짧은 포맷/이름용 스택 버퍼입니다. */
        inline constexpr uint32 kMaxBuffer128 = 128;
        /** @brief 표준 이름/경로용 스택 버퍼입니다. */
        inline constexpr uint32 kMaxBuffer256 = 256;
        /** @brief 중간 메시지/라인용 스택 버퍼입니다. */
        inline constexpr uint32 kMaxBuffer512 = 512;
        /** @brief 중간 메시지용 스택 버퍼입니다. */
        inline constexpr uint32 kMaxBuffer1024 = 1024;
        /** @brief 긴 경로·로그 줄용 스택 버퍼입니다. */
        inline constexpr uint32 kMaxBuffer2048 = 2048;
        /** @brief 큰 텍스트 블록용 스택 버퍼입니다. */
        inline constexpr uint32 kMaxBuffer4096 = 4096;
        /** @brief 로그/어서션 포맷용 최대 스택 버퍼입니다. */
        inline constexpr uint32 kMaxBuffer8192 = 8192;

        /** @brief 기본 로그 태그입니다. */
        inline constexpr const utf8* kDefaultLogTag = "Engine";
        /** @brief 기본 로그 파일 식별자입니다. */
        inline constexpr const utf8* kDefaultLogFile = "unknown";

        /** @brief 1초당 나노초 수입니다. */
        inline constexpr int64 kNanosecondsPerSecond = 1'000'000'000LL;
        /** @brief 1나노초당 초 환산 계수입니다. */
        inline constexpr float64 kSecondsPerNanosecond = 1e-9;

        /** @brief 선형 할당기 기본 초기 용량 (64KB)입니다. */
        inline constexpr size_t kDefaultLinearCapacity = 64 * 1024;
        /** @brief 프레임 아레나 기본 청크 크기 (1MB)입니다. */
        inline constexpr size_t kDefaultFrameArenaCapacity = 1024 * 1024;
        /** @brief 작업 훔치기 덱 기본 용량 (1024)입니다. */
        inline constexpr size_t kDefaultDequeCapacity = 1024;
    } // namespace constant

    // ------------------------------------------------------------------------------
    // 2) 폴더 및 공통 경로 상수
    // ------------------------------------------------------------------------------
    namespace path
    {
        /** @brief 기본 저장 폴더 이름입니다. */
        inline constexpr const utf8* kSavedFolder = "Saved";
        /** @brief 기본 로그 폴더 이름입니다. */
        inline constexpr const utf8* kLogsFolder = "Logs";
    } // namespace path

    // ------------------------------------------------------------------------------
    // 3) 무효 인덱스 — 부호 없는 형은 전비트 1, 부호 있는 형은 -1
    // ------------------------------------------------------------------------------
    namespace invalid_index
    {
        /** @brief uint8 무효값 (0xFF) 입니다. */
        inline constexpr uint8 kUint8 = static_cast<uint8>( -1 );
        /** @brief uint16 무효값 (0xFFFF) 입니다. */
        inline constexpr uint16 kUint16 = static_cast<uint16>( -1 );
        /** @brief uint32 무효값 (0xFFFFFFFF) 입니다. */
        inline constexpr uint32 kUint32 = static_cast<uint32>( -1 );
        /** @brief uint64 무효값 (전비트 1) 입니다. */
        inline constexpr uint64 kUint64 = static_cast<uint64>( -1 );

        /** @brief int8 무효값 (-1) 입니다. */
        inline constexpr int8 kInt8 = -1;
        /** @brief int16 무효값 (-1) 입니다. */
        inline constexpr int16 kInt16 = -1;
        /** @brief int32 무효값 (-1) 입니다. */
        inline constexpr int32 kInt32 = -1;
        /** @brief int64 무효값 (-1) 입니다. */
        inline constexpr int64 kInt64 = -1;
    } // namespace invalid_index

} // namespace sw
