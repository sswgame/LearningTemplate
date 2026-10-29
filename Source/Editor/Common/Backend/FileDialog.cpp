#include "pch.h"

#include "Editor/Common/Backend/FileDialog.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/ThreadCrashStack.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Editor/Common/Backend/Platform/WindowsFileDialog.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Editor/Common/Backend/Platform/LinuxFileDialog.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "FileDialog" );

    namespace
    {
        /** @brief 열린 대화상자 하나입니다. 스레드는 결과를 담고 `_bDone` 을 올릴 뿐이고, 합류 · 전달은 메인 스레드(`pumpResults`)가 한다. */
        struct OpenFileDialog
        {
            thread             _thread{};
            atomic<bool>       _bDone{ false };
            FileDialogDelegate _delegate{};
            vector<string>     _listPath{};
        };

        /** @brief 열린 대화상자입니다. 메인 스레드만 만들고 없앤다. */
        unique_ptr<OpenFileDialog> s_pOpenDialog{};
    } // namespace

    void FileDialog::open( const FileDialogParams& params, FileDialogDelegate onSuccess )
    {
        if ( onSuccess.isBound() == false )
            return;
        if ( s_pOpenDialog != nullptr )
        {
            SW_LOG_WARNING( "A file dialog is already open - close it first" );
            return;
        }
        s_pOpenDialog                 = make_unique<OpenFileDialog>();
        s_pOpenDialog->_delegate      = std::move( onSuccess );
        OpenFileDialog* const pDialog = s_pOpenDialog.get();
        pDialog->_thread              = thread( [pDialog, params, memoryTag = Memory::getCurrentMemoryTag()]()
        {
            // 이 스레드의 할당은 대화상자를 연 쪽의 용도로 센다.
            const ScopedMemoryTag threadMemoryTag{ memoryTag };
            ThreadCrashStack::initializeCurrentThread();
            vector<string> listResult;
            bool           bSuccess{ false };
#if defined( SW_PLATFORM_WINDOWS )
            bSuccess = WindowsFileDialog::open( params, listResult );
#elif defined( SW_PLATFORM_LINUX )
            bSuccess = LinuxFileDialog::open( params, listResult );
#else
            (void)params;
            SW_LOG_WARNING( "The file dialog is not supported on this platform." );
#endif
            // **여기서 델리게이트를 부르지 않는다** — 콜백은 메인 스레드가 매 프레임 만지는 상태(씬 · 컴포넌트)를 고친다.
            if ( bSuccess )
                pDialog->_listPath = std::move( listResult );
            pDialog->_bDone.store( true, std::memory_order_release );
        } );
    }

    void FileDialog::pumpResults()
    {
        if ( s_pOpenDialog == nullptr || s_pOpenDialog->_bDone.load( std::memory_order_acquire ) == false )
            return;
        // 합류가 끝나야 그 스레드가 이 DLL 의 코드를 다 떠난 것이다 — 그 뒤에야 리로드 잡음이 풀린다(`isOpen`).
        if ( s_pOpenDialog->_thread.joinable() )
            s_pOpenDialog->_thread.join();
        const unique_ptr<OpenFileDialog> pDialog = std::move( s_pOpenDialog );
        // 델리게이트는 대화상자를 비운 뒤에 부른다 — 콜백이 다시 대화상자를 열 수 있다.
        if ( pDialog->_delegate.isBound() && pDialog->_listPath.empty() == false )
            pDialog->_delegate( pDialog->_listPath );
    }

    void FileDialog::cancelResults()
    {
        if ( s_pOpenDialog == nullptr )
            return;
        s_pOpenDialog->_delegate = {};
        // 편집기를 내리는 길(종료 · 끄기)이면 사용자가 닫을 때까지 기다릴 수 없다 — 떼어 둔다. 리로드는 `_bDone` 까지 계속 잡힌다.
        // shortcut: 뗀 스레드는 `_bDone` 을 올린 뒤 몇 명령을 이 DLL 에서 더 돈다. 리로드 준비(복사 · 적재)가 그보다 훨씬 길어 겹치지 않는다 — 겹치면 FreeLibraryAndExitThread 로 바꾼다.
        if ( s_pOpenDialog->_bDone.load( std::memory_order_acquire ) == false && s_pOpenDialog->_thread.joinable() )
            s_pOpenDialog->_thread.detach();
    }

    bool FileDialog::isOpen()
    {
        return s_pOpenDialog != nullptr;
    }
} // namespace sw
