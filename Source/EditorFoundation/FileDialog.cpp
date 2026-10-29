#include "pch.h"

#include "EditorFoundation/FileDialog.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/ThreadCrashStack.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "EditorFoundation/Windows/WindowsFileDialog.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "EditorFoundation/Linux/LinuxFileDialog.h"
#endif

namespace sw
{
    namespace
    {
        /** @brief 다이얼로그 스레드가 담고 메인 스레드가 꺼내는 결과 하나입니다. */
        struct FileDialogResult
        {
            FileDialogDelegate _delegate{};
            vector<string>     _listPath{};
        };

        /** @brief 파일 다이얼로그 결과 큐입니다. 담는 쪽은 분리된 스레드, 꺼내는 쪽은 메인 스레드입니다. */
        mutex                    s_fileDialogMutex{};
        vector<FileDialogResult> s_listFileDialogResult{};
        /// @brief cancelFileDialogResults 가 올립니다. 이미 열려 있는 다이얼로그의 결과를 버리는 표시입니다.
        uint32 s_fileDialogGeneration{ 0 };
    } // namespace

    void FileDialog::open( const FileDialogParams& params, FileDialogDelegate onSuccess )
    {
        if ( onSuccess.isBound() == false )
            return;

        uint32 openGeneration{ 0 };
        {
            ScopedLock<mutex> lock{ s_fileDialogMutex };
            openGeneration = s_fileDialogGeneration;
        }

        sw::thread(
            [delegateCallback = std::move( onSuccess ), params, openGeneration, memoryTag = Memory::getCurrentMemoryTag()]
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
            SW_LOG_WARNING( "openFileDialog is not supported on this platform." );
#endif

            if ( bSuccess == false || listResult.empty() )
                return;

            // **여기서 델리게이트를 부르지 않는다.** 이 스레드는 메인 스레드와 아무런 동기화 약속이 없다.
            ScopedLock<mutex> lock{ s_fileDialogMutex };
            if ( s_fileDialogGeneration != openGeneration )
                return; // 여는 사이에 취소됐다. 델리게이트가 가리키던 모듈이 이미 없을 수 있다.
            FileDialogResult result;
            result._delegate = delegateCallback;
            result._listPath = std::move( listResult );
            s_listFileDialogResult.push_back( std::move( result ) );
        } )
            .detach();
    }

    void FileDialog::pumpResults()
    {
        vector<FileDialogResult> listReady;
        {
            ScopedLock<mutex> lock{ s_fileDialogMutex };
            if ( s_listFileDialogResult.empty() )
                return;
            listReady.swap( s_listFileDialogResult );
        }

        // 델리게이트는 **락 밖에서** 부른다. 콜백이 다시 다이얼로그를 열면 같은 뮤텍스에 재진입하기 때문이다.
        for ( FileDialogResult& result : listReady )
        {
            if ( result._delegate.isBound() )
                result._delegate( result._listPath );
        }
    }

    void FileDialog::cancelResults()
    {
        vector<FileDialogResult> listDropped;
        {
            ScopedLock<mutex> lock{ s_fileDialogMutex };
            ++s_fileDialogGeneration;
            listDropped.swap( s_listFileDialogResult );
        }
        // 델리게이트 파괴도 락 밖에서 한다.
        listDropped.clear();
    }
} // namespace sw
