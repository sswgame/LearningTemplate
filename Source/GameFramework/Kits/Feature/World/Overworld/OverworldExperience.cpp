#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceKitRegistry.h"

namespace sw
{
    // Experience 의 kits="Overworld" — 이 키트는 범위에 걸 서비스 · 카탈로그가 없다(맵의 OverworldGameModeComponent 가 씬의 타일 레이어 · 대화 러너로 판을 돌린다).
    SW_EXPERIENCE_KIT( Overworld, "Overworld", &activateKitWithoutServices );
} // namespace sw
