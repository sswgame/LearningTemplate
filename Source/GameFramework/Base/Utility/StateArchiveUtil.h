/**
 * @file StateArchiveUtil.h
 * @brief 키트 시뮬레이션의 `writeState` · `readState`(바이너리 `Archive`)가 함께 쓰는 머리 · 이름 · 개수 읽기 도우미입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct FixedStepTimer;
    struct int2;

    class Archive;
    class GameRandom;

    /**
     * @struct StateArchiveUtil
     * @brief 시뮬레이션 상태 바이트의 공통 모양입니다 — 머리(형식 표 + 버전) 다음에 본문, 이름은 길이 붙은 글자, 개수는 남은 바이트로 상한을 둡니다.
     * @details 같은 상태를 핫 리로드(같은 실행)와 세이브(다른 실행)가 함께 씁니다. 그래서 포인터 대신 id · 이름을 쓰고, 읽을 때는 임시에 읽어
     *          끝까지 맞으면 그때 바꿉니다(반쯤 읽은 상태를 남기지 않는다).
     */
    struct SW_GF_API StateArchiveUtil
    {
        /** @brief 형식 표(4 글자 태그) · 버전을 씁니다. */
        static void writeHeader( Archive& outArchive, uint32 tag, uint32 version );
        /** @brief 머리를 읽고 @p tag · @p version 과 같으면 true 입니다. */
        [[nodiscard]] static bool readHeader( Archive& archive, uint32 tag, uint32 version );
        /**
         * @brief 구간 하나를 씁니다 — 표 · 판 · 본문 길이(uint32) · 본문. 읽는 쪽은 모르는 표를 길이로 건너뜁니다.
         * @details 상태 하나에 키트 · 기반 상태 여럿을 실을 때 씁니다(`GameStateComponent`, 키트 여럿을 든 디렉터). 한 구간의 형식이 바뀌어도 다른
         *          구간은 그대로 읽히고, 빠진 구간은 읽는 쪽이 새 판으로 둡니다. @p body 는 쓰기 모드로 다 쓴 아카이브입니다.
         */
        static void writeSection( Archive& outArchive, uint32 tag, uint32 version, const Archive& body );
        /**
         * @brief `writeSection` 이 쓴 구간 하나의 머리를 읽고 본문을 @p outBody 로 자릅니다(읽기 전용 보기 — @p archive 의 바이트를 가리킨다).
         * @return 머리가 잘렸거나 길이가 남은 바이트를 넘으면 false 이고 @p archive 에 오류를 남깁니다.
         */
        [[nodiscard]] static bool readSection( Archive& archive, uint32& outTag, uint32& outVersion, Archive& outBody );

        static void               writeName( Archive& outArchive, const hashed_string& name );
        [[nodiscard]] static bool readName( Archive& archive, hashed_string& outName );

        /**
         * @brief 원소 개수를 읽습니다. 원소마다 적어도 @p minBytesPerElement 바이트를 쓰므로 남은 바이트로 담을 수 없는 개수면 false 입니다.
         * @details 깨진 개수 하나가 `reserve` 로 수십 기가를 요구하지 않게 합니다.
         */
        [[nodiscard]] static bool readCount( Archive& archive, uint32 minBytesPerElement, uint32& outCount );

        static void writeInt2( Archive& outArchive, const int2& value );
        static void readInt2( Archive& archive, int2& outValue );

        static void               writeRandom( Archive& outArchive, const GameRandom& random );
        [[nodiscard]] static bool readRandom( Archive& archive, GameRandom& outRandom );

        /** @brief 고정 걸음 타이머의 남은 시간만 씁니다(걸음 · 상한은 설정이 정한다). */
        static void               writeStepTimer( Archive& outArchive, const FixedStepTimer& timer );
        [[nodiscard]] static bool readStepTimer( Archive& archive, FixedStepTimer& inoutTimer );
    };
} // namespace sw
