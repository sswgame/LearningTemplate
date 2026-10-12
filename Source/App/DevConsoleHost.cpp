#include "pch.h"

#include "App/DevConsoleHost.h"

#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/LogSink/ConsoleLogOutput.h"
#include "Core/Process/StandardInputReader.h"
#include "Core/String/string_splitter.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Input/DevConsoleController.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    SW_LOG_CALLER( "App" );

#if !defined( SW_SHIPPING )
    /** @brief `-gv_devConsoleExec="timescale 0.5;gv_viewMode 2"`: 시작 씬이 열린 뒤 개발 콘솔에서 실행할 명령입니다(`;` 로 구분). 자동화와 재현에 씁니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_devConsoleExec, "", "시작 씬이 열린 뒤 개발 콘솔로 돌릴 명령 (; 로 나눔)" );
    /** @brief `-gv_devConsoleOpen=1`: 에디터 없이 실행할 때 게임 창의 개발 콘솔을 연 채로 시작합니다(화면 확인용). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_devConsoleOpen, 0, "게임 창 개발 콘솔을 연 채로 시작 (1=열기)" );
    /**
     * @brief `-gv_standardInputCommands=1`: 표준 입력의 각 줄을 명령으로 받습니다. `quit` 과 `stop` 은 정상 종료이고, 나머지는 개발 콘솔 명령입니다.
     * @details 부모 프로세스(에디터의 멀티 프로세스 재생)가 클라이언트를 띄울 때 켠다. 켜면 로그를 줄마다 내보내므로 부모가 줄 단위로 바로 모을 수 있다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_standardInputCommands, 0, "표준 입력의 줄을 명령으로 받는다 (quit=종료, 1=켜기)" );
#endif

    DevConsoleHost::DevConsoleHost()
#if !defined( SW_SHIPPING )
        : _devConsoleController{ nullptr }
        , _standardInput{ nullptr }
        , _listStandardInputCommand{}
        , _bEnableEditor{ SW_FALSE }
        , _bExecPending{ SW_FALSE }
        , _reserved{ 0 }
#endif
    {
    }

    DevConsoleHost::~DevConsoleHost() = default;

    void DevConsoleHost::initialize( [[maybe_unused]] IWindow* pWindow, [[maybe_unused]] bool bEnableEditor )
    {
#if !defined( SW_SHIPPING )
        _bEnableEditor        = bEnableEditor ? SW_TRUE : SW_FALSE;
        _devConsoleController = make_unique<DevConsoleController>();
        // 에디터가 있으면 입력을 받지 않는다(Output Log 입력 줄이 같은 콘솔이다). 시작 명령만 실행한다.
        if ( bEnableEditor == false && engine::areEngineServicesBound() )
            (void)_devConsoleController->initialize( &engine::getInputManager(), pWindow ); // 창을 만들지 못해도 입력과 시작 명령은 돈다(경고는 그쪽이 남긴다).
        _bExecPending = gv_devConsoleExec.empty() ? SW_FALSE : SW_TRUE;
        if ( gv_devConsoleOpen != 0 && bEnableEditor == false )
            _devConsoleController->setOpen( true );
        if ( gv_standardInputCommands != 0 )
        {
            ConsoleLogOutput::setFlushEveryLine( true ); // 부모가 줄 단위로 바로 모은다(접속과 종료 줄을 기다리는 쪽).
            _standardInput = make_unique<StandardInputReader>();
            _standardInput->start();
        }
#endif
    }

    void DevConsoleHost::shutdown()
    {
#if !defined( SW_SHIPPING )
        _devConsoleController.reset();
        _standardInput.reset();                             // 읽기 스레드를 멈추고 기다린다.
        vector<string>{}.swap( _listStandardInputCommand ); // 엔진 종료의 누수 보고 전에 해제한다(기동 뒤에 잡은 용량).
#endif
    }

    void DevConsoleHost::runPendingExec()
    {
#if !defined( SW_SHIPPING )
        // 시작 씬이 다 열린 뒤에 실행한다. 씬을 보는 명령(teleport, select, debugdraw.demo)이 빈 씬에 닿지 않게 하기 위해서다.
        if ( _bExecPending == SW_FALSE || _devConsoleController == nullptr || engine::areEngineServicesBound() == false )
            return;
        const SceneManager& sceneManager = engine::getSceneManager();
        if ( sceneManager.isTransitioning() || sceneManager.getActiveScene() == nullptr )
            return;
        _bExecPending = SW_FALSE;
        const string_splitter commands( string_view{ gv_devConsoleExec.c_str(), gv_devConsoleExec.size() }, { ";" } );
        for ( const string_view command : commands.getSplitList() )
        {
            (void)_devConsoleController->getConsole().submit( command ); // 응답과 실패는 로그에 남는다.
        }
#endif
    }

    void DevConsoleHost::update( [[maybe_unused]] InputMap* pShellMap )
    {
#if !defined( SW_SHIPPING )
        if ( _bEnableEditor == SW_FALSE && _devConsoleController != nullptr && pShellMap != nullptr )
            _devConsoleController->update( *pShellMap );
#endif
    }

    bool DevConsoleHost::pollStandardInputCommands()
    {
#if !defined( SW_SHIPPING )
        if ( _standardInput == nullptr )
            return false;
        _standardInput->drainLines( _listStandardInputCommand );
        bool bQuitRequested = false;
        for ( const string& line : _listStandardInputCommand )
        {
            const string_view command = StringUtil::trim( line );
            if ( command == "quit" || command == "stop" )
            {
                SW_LOG_INFO( "App quit requested on standard input" );
                bQuitRequested = true;
                break;
            }
            if ( command.empty() == false && _devConsoleController != nullptr )
                (void)_devConsoleController->getConsole().submit( command ); // 응답과 실패는 로그에 남는다.
        }
        _listStandardInputCommand.clear();
        return bQuitRequested;
#else
        return false;
#endif
    }
} // namespace sw
