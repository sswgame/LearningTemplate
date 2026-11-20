#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSequence/Editor/Sample/SequenceNetSimGame.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 이 확장 모듈이 로드된 동안 본보기 게임(`SequenceNetSimGame`)을 한 프로세스 재생의 게임 목록에 올립니다(`NetSimSampleRegistrar` 와 같은 꼴 — 등록 · 해제를 한 객체에). */
        struct SequenceNetSimRegistrar
        {
            NetSimGameEntry _entry{ SequenceNetSimGame::kName, &SequenceNetSimGame::create };
            SequenceNetSimRegistrar() { (void)NetSimGameRegistry::registerGame( _entry ); } // 실패(이름 없음 · 중복)는 등록이 오류로 남긴다
            ~SequenceNetSimRegistrar() { (void)NetSimGameRegistry::unregisterGame( _entry ); }
        };

        const SequenceNetSimRegistrar s_sequenceNetSimRegistrar;
    } // namespace
} // namespace sw::editor
