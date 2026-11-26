/**
 * @file DevTools/Host/HostClientDevTasks.h
 * @brief 개발 계층(`HostDevLayer`) 가운데 렌더러 · 에디터가 있어야 하는 일(초상화 굽기, 에디터 모듈 관리자)입니다.
 * @details 정의는 구성마다 하나다. 렌더러가 있는 구성은 `Client/HostClientDevTasks.cpp`, 전용 서버는 `Server/HostServerDevTasks.cpp` 이고, 고르는 규칙은
 *          DevTools CMakeLists 한 곳에 있다. 그래서 공통 파일에 구성 분기를 흩지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "ModuleManager/Host/HostDevLayer.h"

namespace sw
{
    class CommandLineManager;
    class Engine;
    class GameModuleManager;

    /** @brief 클라이언트에만 있는 개발 계층 일입니다. 전용 서버 정의는 청하면 실패로 답한다. */
    struct HostClientDevTasks
    {
        /** @brief 초상화 굽기(`--render-portraits=a.prefab.xml,b.prefab.xml`)입니다. 인자가 없으면 NotRequested 입니다. */
        [[nodiscard]] static HostTaskResult runPortraitBake( Engine& engine, const CommandLineManager& commandLine );
        /** @brief 에디터 인스턴스를 드는 모듈 관리자(`-EnableEditor`)를 만듭니다. 전용 서버는 에디터가 없어 nullptr 입니다. */
        [[nodiscard]] static unique_ptr<GameModuleManager> makeEditorModuleManager( Engine& engine );
    };
} // namespace sw
