/**
 * @file FileLogOutput.h
 * @brief 파일 로그 장치 — `Saved/Logs` 폴더, 시간별 + 세션별 롤오버
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Log/ILogOutput.h"

namespace sw
{
    /**
     * @class FileLogOutput
     * @brief 로그 한 줄을 `Saved/Logs/LOG_<날짜>_<세션>.txt` 에 씁니다.
     * @details 파일은 **시간이 바뀔 때** 갈아 끼웁니다. 이름에 세션 ID 가 들어가므로 같은 시간에 여러
     *          번 실행해도 섞이지 않습니다 — 배포본은 크래시 뒤 바로 재실행하는 일이 잦아 그게 기본
     *          상황입니다. 크래시 덤프도 같은 세션 ID 를 쓰므로 둘을 짝지을 수 있습니다.
     */
    class SW_API FileLogOutput final : public ILogOutput
    {
    public:
        FileLogOutput();
        virtual ~FileLogOutput() override;

        FileLogOutput( const FileLogOutput& )            = delete;
        FileLogOutput& operator=( const FileLogOutput& ) = delete;

        /**
         * @brief 로그 폴더를 만들고 크래시 리포트 폴더도 같은 곳으로 맞춥니다.
         * @details 파일 자체는 첫 `write` 때 엽니다 — 한 줄도 남기지 않고 끝나는 실행에
         *          빈 파일을 만들지 않기 위해서입니다.
         */
        bool open() override;
        /** @brief 열려 있는 파일을 비우고 닫습니다. */
        void close() override;
        /** @brief 시각이 바뀌었으면 파일을 갈아 끼운 뒤 한 줄을 씁니다. */
        void write( const LogRecord& record ) override;

        /** @brief 로그 파일이 있는 폴더 경로입니다. `open` 전에는 비어 있습니다. */
        const string& getLogFolderPath() const { return _logFolderPath; }

    private:
        mutex      _mutex; ///< 이 장치 전용 — 콘솔 출력과 더 이상 락을 공유하지 않는다
        string     _logFolderPath;
        string     _currentLogFileName;
        std::FILE* _pFile;
        int32      _lastLogHour; ///< 시간별 로그 파일 롤오버 감지용
        bool       _bOpened;
    };
} // namespace sw
