/**
 * @file SerializePurpose.h
 * @brief 직렬화가 무엇을 위해 쓰고 읽는지(저작 · 스냅샷 · 세이브)를 나타내는 열거형입니다. 직렬화 문맥(`SerializeContext`)과 오브젝트 상태 옵션의 필수 값입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @enum SerializePurpose
     * @brief 직렬화가 무엇을 위해 쓰고 읽는지입니다. 필드 플래그와 함께 어떤 필드를 쓰는지 정합니다.
     * @details 판정 표는 `SerializerUtil::shouldSerializeProperty` 한 곳이다.
     *          | 필드 | Authoring | Snapshot | SaveGame |
     *          | 없음 | 씀 | 씀 | 타입에 `SaveGame` 필드가 없으면 씀 |
     *          | `Transient` | 안 씀 | 안 씀 | 안 씀 |
     *          | `RuntimeState` | 안 씀(파일에 있으면 로드 오류) | 씀 | 타입에 `SaveGame` 필드가 없으면 씀 |
     *          | `SaveGame` | 씀(`RuntimeState` 와 함께면 안 씀) | 씀 | 씀 |
     */
    enum class SerializePurpose : uint8
    {
        Authoring, ///< 저작 파일입니다(씬 · 프리팹 · 데이터 파일, 쿠킹, 에디터의 복사 · 복제 · 되돌리기)
        Snapshot,  ///< 같은 실행 안에서 상태를 옮깁니다(플레이 세션 복원, 핫 리로드, 씬 넘김, 게임 상태 봉투)
        SaveGame,  ///< 세이브 파일입니다. `SaveGame` 필드가 하나라도 있는 타입(옵트인)은 그것만 쓰고 읽습니다
    };
} // namespace sw
