/**
 * @file NetObjectID.h
 * @brief 서버와 클라이언트가 같은 오브젝트를 부르는 네트워크 오브젝트 번호 형식입니다(번호를 매기는 곳은 `NetObjectTable`).
 * @note 복제 키트(파괴 · 페어드 애니메이션)의 메시지 헤더가 번호표 전체(`NetObjectTable.h`) 없이 형식만 쓰도록 엔진 바닥에 둡니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @brief 네트워크 오브젝트 번호입니다. 0 은 번호 없음, 맨 아래 비트가 켜지면 생성 번호, 꺼지면 씬 번호입니다(`NetObjectTable`).
     * @details 64 비트다 — 씬 번호가 이름의 해시라 31 비트로는 이름이 다른 오브젝트 수천 개에서 겹칠 확률이 무시할 수 없었다(겹치면 둘 다 복제를 잃는다).
     *          와이어에는 가변 길이 정수(`BitWriter::writeVarUint`)로 싣는다 — 생성 번호는 작아 1 ~ 3 바이트, 씬 번호는 9 바이트다(언리얼 NetGUID 도 64 비트).
     */
    using NetObjectID = uint64;
} // namespace sw
