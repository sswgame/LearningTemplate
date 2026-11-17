/**
 * @file SiblingOrderKey.h
 * @brief 형제 순서의 분수 색인 키(fractional indexing)입니다. 씬 · ImGui 를 모르는 순수 함수라 EditorTest 가 본다.
 * @details 한 줄 옮기기 = 옮긴 오브젝트 하나의 키 하나다(씬 파일 속성 한 줄 · 되돌리기 데이터 단계 한 쌍). 협업 도구(Figma · Linear)의 분수 색인과 같고,
 *          유니티 `SceneRoots` 목록 · Godot 파일 순서와 달리 프리팹 변형의 칸 단위 차이로 적히고 다른 오브젝트를 옮긴 두 사람의 병합이 충돌하지 않는다(설계 E-6).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /**
     * @struct SiblingOrderKey
     * @brief 앞 · 뒤 키 사이의 키를 고르고, 틈이 다 찬 형제를 고르게 다시 폅니다. 같은 키는 오브젝트 id 순이다(`EditorSiblingOrder::isBefore`).
     */
    struct SiblingOrderKey
    {
        /** @brief 끝에 더할 때의 간격입니다. 형제 100 만까지 다시 펴지 않는다. */
        static constexpr uint32 kAppendStep = 1u << 12;

        /**
         * @brief 앞 키 @p previous 와 뒤 키 @p next 사이(둘 다 제외)의 키입니다. 끝이면 그쪽을 비운다(둘 다 비면 `kAppendStep`).
         * @return 틈이 없으면(뒤 키가 앞 키보다 크지 않거나 사이에 정수가 없거나 `uint32` 끝에 닿았으면) 없음 — 부르는 쪽이 그 부모의 형제만 `spread` 한다.
         */
        [[nodiscard]] static optional<uint32> between( optional<uint32> previous, optional<uint32> next );
        /** @brief @p count 개를 고르게 편 키(오름차순, 첫 키는 0 이 아니다)를 @p outListKey 에 채웁니다(먼저 비운다). 틈이 다 찼을 때만 쓴다. */
        static void spread( uint32 count, vector<uint32>& outListKey );
    };
} // namespace sw::editor
