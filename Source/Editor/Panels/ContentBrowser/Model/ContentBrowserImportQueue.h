/**
 * @file ContentBrowserImportQueue.h
 * @brief 콘텐츠 브라우저의 임포트 대기열입니다. 파일 대화상자가 고른 경로를 백그라운드 스레드에서 받아 두었다가 메인 스레드에 넘깁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw::editor
{
    /**
     * @class ContentBrowserImportQueue
     * @brief 임포트할 원본 경로를 스레드 사이로 넘기는 대기열입니다.
     * @details 파일 대화상자는 백그라운드에서 열리고 결과도 그 스레드에서 온다(`onDialogResult`). 임포트는 에셋 파일을 쓰고 목록을 다시 읽게 하므로
     *          메인 스레드에서 `takePending` 으로 꺼내 실행한다. 받는 쪽과 꺼내는 쪽이 다른 스레드라 뮤텍스 하나로 지킨다.
     */
    class ContentBrowserImportQueue
    {
    public:
        /** @brief 빈 대기열을 만듭니다. */
        ContentBrowserImportQueue();

        /**
         * @brief 임포트할 파일을 고르는 대화상자를 엽니다. 고른 경로는 `onDialogResult` 로 들어옵니다.
         * @param initialFolderAbs 대화상자가 처음 보일 폴더입니다. 임포트 대상 폴더를 넘깁니다.
         * @warning 대화상자가 닫힐 때 이 객체의 주소로 결과를 넘깁니다. 대화상자가 열려 있는 동안 객체를 옮기거나 지우지 않습니다.
         */
        void openDialog( string_view initialFolderAbs );
        /** @brief 파일 대화상자에서 고른 경로가 들어왔습니다. 어느 스레드에서 불러도 됩니다. */
        void onDialogResult( const vector<string>& listPath );
        /** @brief 쌓인 경로를 모두 꺼내 @p outListPath 에 둡니다. 꺼낼 것이 없으면 false 이고 @p outListPath 는 비웁니다. */
        bool takePending( vector<string>& outListPath );

    private:
        mutex          _mutex;
        vector<string> _listPendingPath;
    };
} // namespace sw::editor
