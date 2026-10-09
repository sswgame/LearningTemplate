/**
 * @file JrpgEncounter.h
 * @brief 걸음 수 인카운터 — 걸음마다 지역 확률로 조우를 굴리고, 조우 뒤 유예 걸음 동안은 굴리지 않으며, 무리는 가중치로 고릅니다(드래곤 퀘스트).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Rpg/ClassicJrpg/JrpgCatalog.h"

namespace sw
{
    class Archive;

    /**
     * @class JrpgEncounterWalker
     * @brief 걸음을 세어 조우를 정합니다. 씨앗이 같고 걸음이 같으면 같은 걸음에서 같은 무리를 만납니다.
     */
    class SW_GF_API JrpgEncounterWalker
    {
    public:
        static constexpr uint32 kStateTag       = FourCcUtil::make( "JENC" );
        static constexpr uint32 kStateVersion   = 1;
        static constexpr int32  kNoEncounterYet = 0x3fffffff;

        JrpgEncounterWalker();

        void initialize( const JrpgCatalog* pCatalog, uint32 seed );
        /** @brief @p areaId 에서 한 걸음 걷습니다. 조우하면 그 무리, 아니면 nullptr 입니다(모르는 지역 · 무리 없는 지역도 nullptr). */
        const JrpgEncounterGroup* step( const hashed_string& areaId );
        /** @brief 유예를 처음부터 다시 셉니다(마을에서 나왔을 때 · 성수). */
        void restartGrace() { _stepsSinceEncounter = 0; }
        /** @brief 난수 · 조우 뒤 걸음 · 총 걸음을 씁니다. 카탈로그는 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        int32 getStepsSinceEncounter() const { return _stepsSinceEncounter; }
        int32 getTotalSteps() const { return _totalSteps; }

    private:
        GameRandom         _random;
        const JrpgCatalog* _pCatalog;
        int32              _stepsSinceEncounter;
        int32              _totalSteps;
    };
} // namespace sw
