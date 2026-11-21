/**
 * @file WorkerReadFence.h
 * @brief "지금 이 주인의 데이터를 읽는 워커가 없다" 는 증표입니다(`WorkerReadFence<TOwner>`) — 워커 읽기와 겹쳐 고치는 길을 형식으로 막는 한 벌입니다.
 * @details 워커가 읽는 데이터(내비메시 타일 · 격자 · 물리 씬)를 바꾸는 함수가 이 증표를 인자로 받으면, 워커를 기다리지(수확 · 읽기 구간 닫기) 않고 바꾸는 코드는
 *          컴파일되지 않는다. 증표는 주인 형식(`TOwner`)만 만든다 — 주인마다 다른 형식이라 격자 줄의 증표로 내비메시를 바꾸지 못한다.
 *          경로 줄(`PathRequestQueue::Fence` — `complete` · `clear`)과 물리 읽기 구간(`ScenePhysics::closeReadScope`)이 같은 형식을 쓴다.
 *          유니티 잡 시스템의 안전 핸들(`AtomicSafetyHandle`)이 읽기 · 쓰기 겹침을 한 장치로 잡는 자리를 컴파일 시간 형식으로 둔다.
 */
#pragma once

namespace sw
{
    /** @class WorkerReadFence @brief 파일 머리말 참고. 복사는 자유롭다 — 증표가 있다는 사실만 뜻이 있고 값은 없다. */
    template <typename TOwner>
    class WorkerReadFence
    {
        friend TOwner;

    private:
        WorkerReadFence() {} // NOLINT(modernize-use-equals-default): 사용자 제공 생성자라야 C++17 집합체가 아니다 — 집합체면 아무나 `{}` 로 만든다
    };
} // namespace sw
