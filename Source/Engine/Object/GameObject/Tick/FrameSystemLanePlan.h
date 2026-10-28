/**
 * @file FrameSystemLanePlan.h
 * @brief 프레임 시스템(`ISceneFrameSystem`)의 접근 선언(`SceneFrameAccess`)으로 "어느 것끼리 같이 돌 수 있나" 를 정하는 순수 계산입니다.
 * @details 붙인 순서가 의미이므로 레인(작업 묶음)은 **순서를 보존하는 파도(wave)** 로 만든다. 시스템 j 는 앞에 붙은 시스템 i 와 접근이 겹치면 i 의 파도 **다음** 파도에 들고(선언된 선행),
 *          겹치는 앞 시스템이 없으면 첫 파도에 든다. 같은 파도의 시스템은 서로 겹치는 접근이 없어 한꺼번에 돌아도 직렬 실행과 같은 결과다. 파도는 차례로 돈다.
 *          언리얼 Task Graph 의 선행(prerequisite)이 있는 태스크 묶음, 유니티 Job System 의 `JobHandle` 의존과 같은 자리이고, 엔진 밖(데이터 접근 선언)을 모르는 순수 함수라 단위로 시험한다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/Tick/SceneFrameAccess.h"

namespace sw
{
    /**
     * @brief 앞 시스템(@p earlier, 붙은 순서가 앞)과 뒤 시스템(@p later)이 같이 돌 수 없는지 돌려줍니다.
     * @details 한쪽이 쓰는 종류를 다른 쪽이 읽거나(`_reads` · `_readsPrevious`) 쓰면 겹칩니다. 둘 다 읽기만 하는 종류는 겹치지 않습니다.
     */
    SW_API bool hasFrameAccessConflict( const SceneFrameAccess& earlier, const SceneFrameAccess& later );

    /** @brief 계획 한 칸 — 파도 번호와 그 파도에 든 시스템 번호들(입력 순서대로)입니다. */
    struct FrameSystemWave
    {
        vector<uint32> _listSystem;
    };

    /** @brief 파도의 순서 목록입니다. 호출처가 재사용해도 됩니다(`build` 가 비우고 채운다). */
    struct FrameSystemLanePlan
    {
        vector<FrameSystemWave> _listWave;
        /** @brief 만드는 중 쓰는 칸 — 시스템마다 정해진 파도 번호입니다(프레임 사이에 용량을 둔다). */
        vector<uint32> _listWaveOf;
    };

    /**
     * @brief 접근 선언의 목록(붙인 순서)으로 파도 계획을 만듭니다. `outPlan` 의 시스템 번호는 @p listAccess 의 번호입니다.
     * @details 시스템 j 의 파도는 "앞 시스템 중 j 와 겹치는 것의 가장 늦은 파도 + 1" 이다. 겹침이 없으면 0.
     */
    SW_API void buildFrameSystemLanePlan( span<const SceneFrameAccess> listAccess, FrameSystemLanePlan& outPlan );
} // namespace sw
