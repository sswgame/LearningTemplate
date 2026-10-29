#include "pch.h"

#include "DevTools/Console/DevConsoleHost.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Log/Sink/ConsoleLogOutput.h"
#include "Core/String/string_splitter.h"

#include "DevTools/Console/DevConsoleController.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/UISystem.h"

namespace sw
{
    SW_LOG_CALLER( "DevConsole" );

    /** @brief `-gv_devConsoleExec="timescale 0.5;gv_viewMode Wireframe"`: 시작 씬이 열린 뒤 개발 콘솔에서 실행할 명령입니다(`;` 로 구분). 자동화와 재현에 씁니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_devConsoleExec, "", "시작 씬이 열린 뒤 개발 콘솔로 돌릴 명령 (; 로 나눔)" );
    /** @brief `-gv_devConsoleOpen=1`: 에디터 없이 실행할 때 게임 창의 개발 콘솔을 연 채로 시작합니다(화면 확인용). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_devConsoleOpen, 0, "게임 창 개발 콘솔을 연 채로 시작 (1=열기)" );
    /**
     * @brief `-gv_standardInputCommands=1`: 표준 입력의 각 줄을 명령으로 받습니다. `quit` 과 `stop` 은 정상 종료이고, 나머지는 개발 콘솔 명령입니다.
     * @details 부모 프로세스(에디터의 멀티 프로세스 재생)가 클라이언트를 띄울 때 켠다. 켜면 로그를 줄마다 내보내므로 부모가 줄 단위로 바로 모을 수 있다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_standardInputCommands, 0, "표준 입력의 줄을 명령으로 받는다 (quit=종료, 1=켜기)" );

    DevConsoleHost::DevConsoleHost()
        : _devConsoleController{ nullptr }
        , _standardInput{}
        , _bEnableEditor{ SW_FALSE }
        , _bExecPending{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    DevConsoleHost::~DevConsoleHost() = default;

    void DevConsoleHost::initialize( bool bEnableEditor )
    {
        _bEnableEditor        = bEnableEditor ? SW_TRUE : SW_FALSE;
        _devConsoleController = make_unique<DevConsoleController>();
        // 에디터가 있으면 입력을 받지 않는다(Output Log 입력 줄이 같은 콘솔이다). 시작 명령만 실행한다.
        if ( bEnableEditor == false && engine::areEngineServicesBound() )
            (void)_devConsoleController->initialize( &engine::getSubsystem<InputManager>(), engine::findSubsystem<UISystem>() ); // UI 가 없어도 입력과 시작 명령은 돈다
        _bExecPending = gv_devConsoleExec.empty() ? SW_FALSE : SW_TRUE;
        if ( gv_devConsoleOpen != 0 && bEnableEditor == false )
            _devConsoleController->setOpen( true );
        if ( gv_standardInputCommands != 0 )
        {
            ConsoleLogOutput::setFlushEveryLine( true ); // 부모가 줄 단위로 바로 모은다(접속과 종료 줄을 기다리는 쪽).
            _standardInput.start();
        }
    }

    void DevConsoleHost::shutdown()
    {
        _devConsoleController.reset();
        _standardInput.stop(); // 읽기 스레드를 멈추고 기다린다. 줄 버퍼도 엔진 종료의 누수 보고 전에 해제한다.
    }

    void DevConsoleHost::runPendingExec()
    {
        // 시작 씬이 다 열린 뒤에 실행한다. 씬을 보는 명령(teleport, select, debugdraw.demo)이 빈 씬에 닿지 않게 하기 위해서다.
        const bool bConsoleNotReadyToExecute = _bExecPending == SW_FALSE || _devConsoleController == nullptr || engine::areEngineServicesBound() == false;
        if ( bConsoleNotReadyToExecute )
            return;
        const SceneManager& sceneManager = engine::getSubsystem<SceneManager>();
        if ( sceneManager.isTransitioning() || sceneManager.getActiveScene() == nullptr )
            return;
        _bExecPending = SW_FALSE;
        const string_splitter commands( string_view{ gv_devConsoleExec.c_str(), gv_devConsoleExec.size() }, { ";" } );
        for ( const string_view command : commands.getSplitList() )
        {
            (void)_devConsoleController->getConsole().submit( command ); // 응답과 실패는 로그에 남는다.
        }
    }

    void DevConsoleHost::update( InputMap* pShellMap )
    {
        const bool bUpdateHeadlessConsole = _bEnableEditor == SW_FALSE && _devConsoleController != nullptr && pShellMap != nullptr;
        if ( bUpdateHeadlessConsole )
            _devConsoleController->update( *pShellMap );
    }

    bool DevConsoleHost::pollStandardInputCommands()
    {
        return _standardInput.poll( SW_DELEGATE_METHOD( StandardInputCommandDelegate, &DevConsoleHost::submitStandardInputCommand, this ) );
    }

    void DevConsoleHost::submitStandardInputCommand( string_view command )
    {
        if ( _devConsoleController != nullptr )
            (void)_devConsoleController->getConsole().submit( command ); // 응답과 실패는 로그에 남는다.
    }
} // namespace sw
