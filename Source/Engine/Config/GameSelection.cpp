#include "pch.h"

#include "Engine/Config/GameSelection.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        struct GameSelectionInternal
        {
            /**
             * @brief 고른 게임 이름 — 고정 배열이라 상수 초기화되고 정적 소멸 · 할당자 순서와 엮이지 않는다(`ModuleLibraryUtil` 의 옮긴 모듈과 같다).
             * @details 코어 범위의 `CrashHandlerSubsystem` 이 기동 스레드에서 한 번 적고 그 뒤에 읽는다. 읽는 쪽(모듈 · 설정 경로를 찾는 정적 함수)이 서비스 범위가 서기 전에도 묻고
             *          테스트도 범위 없이 묻는다(고르기 전이면 기본 게임) — 그래서 범위에 맡기지 않고 이 변수 하나가 소유한다. 지연 생성하는 접근자를 두지 않는다.
             */
            static inline utf8 s_arrName[GameSelection::kNameCapacity]{};
        };
    } // namespace

    void GameSelection::storeName( string_view name )
    {
        StringUtil::strncpy( GameSelectionInternal::s_arrName, string{ name }.c_str(), kNameCapacity );
    }

    string GameSelection::getName()
    {
        const utf8* pName = GameSelectionInternal::s_arrName;
        if ( pName[0] != '\0' )
            return string{ pName };
        return readDefaultName();
    }

    string GameSelection::getConfigPath()
    {
        return FileUtil::joinPath( config::kDirRuntimeGamePreset, getName() + ".json" );
    }

    string GameSelection::getServerConfigPath()
    {
        return FileUtil::joinPath( config::kDirRuntimeServerPreset, getName() + ".json" );
    }
} // namespace sw
