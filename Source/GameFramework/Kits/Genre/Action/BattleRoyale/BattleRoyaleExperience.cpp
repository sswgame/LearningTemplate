#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceKitRegistry.h"

namespace sw
{
    // Experience 의 kits="BattleRoyale" — 이 키트는 범위에 걸 서비스 · 카탈로그가 없다(시연 컴포넌트가 규칙 객체를 직접 쥐고 데이터를 경로 칸으로 읽는다).
    SW_EXPERIENCE_KIT( BattleRoyale, "BattleRoyale", &activateKitWithoutServices );
} // namespace sw
