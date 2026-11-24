#include "pch.h"

#include "Engine/Content/ContentFolderWatch.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    // 구현(개발 구성의 `ContentFolderWatchService`)은 호스트 범위에 건다(`HostDevTools`). 게임 모듈(외형 데이터) · 에디터 확장(공원 배치 미리보기)이 읽는다. 배포본에는 없어 칸이 빈다.
    SW_ENGINE_SERVICE( IContentFolderWatch, GameVisible );
} // namespace sw
