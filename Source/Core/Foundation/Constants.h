/**
 * @file Constants.h
 * @brief Core 의 컴파일 상수 한 곳입니다. 모든 상수는 `sw::constant::<분류>` 아래 둔다(버퍼 · 파일 · 시간 · 메모리 · 로그 · 네트워크 · 경로 · 무효 인덱스).
 * @details 여러 파일이 같은 뜻으로 쓰는 값만 여기 둔다. 한 형식 · 한 시스템만 쓰는 값(파일 형식 버전 `kStateVersion`, 한 알고리즘의 허용 오차)은 그 자리에 둔다 —
 *          모으면 따로 바뀌어야 할 값이 한 값에 묶인다(형식 A 의 버전을 올리면 형식 B 도 올라간다).
 *          위층의 분류는 그 층의 한 파일에 같은 `sw::constant::<분류>` 로 둔다:
 *          - 엔진 기본값(`window` · `physics` · `locale`)과 팩 · 폴더 이름(`path`): `Engine/Foundation/EngineDefines.h`
 *          - 렌더 백엔드 계약(`render` — RHI 형식 값이 필요하다): `Engine/Graphics/RHI/RHIDescriptors.h`
 *          - 리플렉션 이름 조각(`reflection`): `Engine/Reflection/ReflectionConstants.h`
 *          언리얼의 `MAX_*` · `FPlatformTime` 단위, Godot `core/math/math_defs.h` · `core/typedefs.h` 의 상수와 같은 자리다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::constant::buffer
{
    /** @brief 초소형 식별자/숫자용 스택 버퍼입니다. */
    inline constexpr uint32 kMax16 = 16;
    /** @brief 소형 식별자/숫자용 스택 버퍼입니다. */
    inline constexpr uint32 kMax32 = 32;
    /** @brief 단문자/속성 이름용 스택 버퍼입니다. */
    inline constexpr uint32 kMax64 = 64;
    /** @brief 짧은 포맷/이름용 스택 버퍼입니다. */
    inline constexpr uint32 kMax128 = 128;
    /** @brief 표준 이름/경로용 스택 버퍼입니다. */
    inline constexpr uint32 kMax256 = 256;
    /** @brief 중간 메시지/라인용 스택 버퍼입니다. */
    inline constexpr uint32 kMax512 = 512;
    /** @brief 중간 메시지용 스택 버퍼입니다. */
    inline constexpr uint32 kMax1024 = 1024;
    /** @brief 긴 경로·로그 줄용 스택 버퍼입니다. */
    inline constexpr uint32 kMax2048 = 2048;
    /** @brief 큰 텍스트 블록용 스택 버퍼입니다. */
    inline constexpr uint32 kMax4096 = 4096;
    /** @brief 로그/어서션 포맷용 최대 스택 버퍼입니다. */
    inline constexpr uint32 kMax8192 = 8192;
} // namespace sw::constant::buffer

namespace sw::constant::file
{
    /** @brief 파일 시스템 최대 경로 길이 (Windows MAX_PATH 호환) */
    inline constexpr uint32 kMaxPathSize = 260;
    /** @brief 파일 읽기 호출 하나가 읽는 최대 바이트(1 GiB)입니다. 길이 칸이 32 비트인 OS 호출(ReadFile · io_uring SQE)에 맞춥니다. 큰 구간은 이어서 읽습니다. */
    inline constexpr uint64 kMaxFileReadChunkBytes = uint64{ 1 } << 30;
} // namespace sw::constant::file

namespace sw::constant::time
{
    /** @brief 1초당 나노초 수입니다. */
    inline constexpr int64 kNanosecondsPerSecond = 1'000'000'000LL;
    /** @brief 1초당 밀리초 수입니다. */
    inline constexpr int64 kMillisecondsPerSecond = 1'000LL;
    /** @brief 1초당 마이크로초 수입니다. */
    inline constexpr int64 kMicrosecondsPerSecond = 1'000'000LL;
    /** @brief 1마이크로초당 나노초 수입니다. */
    inline constexpr int64 kNanosecondsPerMicrosecond = 1'000LL;
    /** @brief 1밀리초당 마이크로초 수입니다. */
    inline constexpr int64 kMicrosecondsPerMillisecond = 1'000LL;
    /** @brief 1밀리초당 나노초 수입니다. */
    inline constexpr int64 kNanosecondsPerMillisecond = 1'000'000LL;
    /** @brief 1분당 초 수입니다. */
    inline constexpr int64 kSecondsPerMinute = 60LL;
    /** @brief 1시간당 초 수입니다. */
    inline constexpr int64 kSecondsPerHour = 3600LL;
    /** @brief 1일당 초 수입니다. */
    inline constexpr int64 kSecondsPerDay = 86400LL;
    /**
     * @brief 고정 스텝 시뮬레이션의 기본 한 걸음(60 Hz)입니다. 고정 스텝 타이머 · 시나리오 · VFX 시계 · 키트의 결정적 세계가 따로 정하지 않을 때 쓴다.
     * @details 예전에는 같은 `1.0f / 60.0f` 가 스무 곳에 따로 적혀 있었다. 기본을 바꾸면 결정적 재생 · 골든이 함께 바뀌므로 한 값으로 둔다.
     */
    inline constexpr float32 kDefaultFixedStepSeconds = 1.0f / 60.0f;
    /**
     * @brief 고정 스텝 시뮬레이션이 한 프레임에 따라잡는 시간의 기본 상한(s)입니다(`FixedStepTimer::_maxFrameTime`).
     * @details 디버거 정지 · 로딩 같은 큰 프레임이 수천 번의 걸음이 되지 않게 자른다(유니티 `Time.maximumDeltaTime` 자리). 키트 열 곳이
     *          같은 `0.25f` 를 따로 적고 있었다 — 빨리 감기처럼 더 받아야 하는 세계만 자기 값을 쓴다.
     */
    inline constexpr float32 kDefaultMaxFrameSeconds = 0.25f;
} // namespace sw::constant::time

namespace sw::constant::memory
{
    /** @brief 1 KiB 의 바이트 수입니다. */
    inline constexpr size_t kBytesPerKilobyte = size_t{ 1024 };
    /** @brief 1 MiB 의 바이트 수입니다. */
    inline constexpr size_t kBytesPerMegabyte = size_t{ 1024 } * 1024;
    /** @brief 1 GiB 의 바이트 수입니다. */
    inline constexpr size_t kBytesPerGigabyte = kBytesPerMegabyte * 1024;
    /** @brief 선형 할당기 기본 초기 용량 (64KB)입니다. */
    inline constexpr size_t kDefaultLinearCapacity = size_t{ 64 } * 1024;
    /** @brief 작업 훔치기(work stealing) 덱의 기본 용량(1024)입니다. */
    inline constexpr size_t kDefaultDequeCapacity = 1024;
} // namespace sw::constant::memory

namespace sw::constant::log
{
    /** @brief 기본 로그 태그입니다. */
    inline constexpr const utf8* kDefaultLogTag = "Engine";
    /** @brief 기본 로그 파일 식별자입니다. */
    inline constexpr const utf8* kDefaultLogFile = "unknown";
} // namespace sw::constant::log

namespace sw::constant::network
{
    /** @brief 포트 번호의 상한(16 비트)입니다. 0 은 "아무 포트" 라 쓰는 쪽이 따로 거른다. */
    inline constexpr int32 kMaxPort = 65535;
} // namespace sw::constant::network

namespace sw::constant::path
{
    /** @brief 기본 저장 폴더 이름입니다. */
    inline constexpr const utf8* kSavedFolder = "Saved";
    /** @brief 기본 로그 폴더 이름입니다. */
    inline constexpr const utf8* kLogsFolder = "Logs";
    /** @brief 리소스 루트 폴더 이름입니다(프로젝트 루트 아래). `ProcessRoots` 가 프로젝트 루트를 이 폴더로 알아본다. */
    inline constexpr const utf8* kContentFolder = "Content";
    /** @brief 구운 팩 폴더 이름입니다(바이너리 폴더 아래 - `ProcessRoots::_packs`). 패키지에는 `Content` 없이 이것만 있다. */
    inline constexpr const utf8* kPacksFolder = "Packs";
} // namespace sw::constant::path

namespace sw::constant
{
    /**
     * @brief 무효 인덱스 표지의 형식입니다. 정수 `T` 로 암시 변환되어 비교 · 대입 · 산술은 리터럴처럼 쓰지만, `optional` · `expected` 의
     *        값으로는 들어가지 못한다(`Expected.h` 가 그 생성 · 대입 · 비교를 지운다) — "없음" 을 값 -1 로 싣던 결함이 컴파일 오류가 된다.
     * @note 따로 된 형식이라 `auto` 로 받거나 템플릿 인자로 추론하면 이 형식이 된다. 정수가 필요하면 형식을 적는다(`const int32 x = kInt32;`).
     */
    template <typename T>
    struct InvalidIndex
    {
        static constexpr T kValue = static_cast<T>( -1 );

        constexpr operator T() const noexcept { return kValue; }
    };
} // namespace sw::constant

namespace sw::constant::invalid_index
{
    // 무효 인덱스 — 부호 없는 형은 모든 비트가 1, 부호 있는 형은 -1
    /** @brief uint8 무효값 (0xFF) 입니다. */
    inline constexpr InvalidIndex<uint8> kUint8{};
    /** @brief uint16 무효값 (0xFFFF) 입니다. */
    inline constexpr InvalidIndex<uint16> kUint16{};
    /** @brief uint32 무효값 (0xFFFFFFFF) 입니다. */
    inline constexpr InvalidIndex<uint32> kUint32{};
    /** @brief uint64 무효값(모든 비트가 1)입니다. */
    inline constexpr InvalidIndex<uint64> kUint64{};

    /** @brief int8 무효값 (-1) 입니다. */
    inline constexpr InvalidIndex<int8> kInt8{};
    /** @brief int16 무효값 (-1) 입니다. */
    inline constexpr InvalidIndex<int16> kInt16{};
    /** @brief int32 무효값 (-1) 입니다. */
    inline constexpr InvalidIndex<int32> kInt32{};
    /** @brief int64 무효값 (-1) 입니다. */
    inline constexpr InvalidIndex<int64> kInt64{};
} // namespace sw::constant::invalid_index
