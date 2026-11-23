#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogValidation.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 검증이 도는 동안 **이 호출의** 로더가 남기는 경고 · 오류 줄을 모읍니다. 다른 스레드의 줄이 섞이면 멀쩡한 파일이 실패로 보이므로 추적 id 로 가른다. */
        class CatalogWarningCollector
        {
        public:
            CatalogWarningCollector()
                : _traceID{ LogTraceID::makeRandom() }
                , _mutex{}
                , _listWarning{}
                , _subscription{}
            {
                _subscription = Logger::registerGlobalListener( SW_DELEGATE_METHOD( LogWrittenDelegate, &CatalogWarningCollector::onLog, this ) );
            }

            CatalogWarningCollector( const CatalogWarningCollector& )            = delete;
            CatalogWarningCollector& operator=( const CatalogWarningCollector& ) = delete;

            /** @brief 지금 스레드의 로그 문맥에 이 추적 id 를 얹은 값입니다. `ScopedLogContext` 에 넘깁니다. */
            [[nodiscard]] LogContext makeContext() const
            {
                LogContext context = LogContext::getCurrent();
                context._traceID   = _traceID;
                return context;
            }

            vector<string> take()
            {
                ScopedLock<mutex> lock{ _mutex };
                return std::move( _listWarning );
            }

        private:
            void onLog( const LogEntry& entry )
            {
                if ( entry._level != LogLevel::Warning && entry._level != LogLevel::Error )
                    return;
                if ( entry._context._traceID != _traceID )
                    return;
                ScopedLock<mutex> lock{ _mutex };
                _listWarning.push_back( entry._message );
            }

        private:
            LogTraceID     _traceID;
            mutex          _mutex;
            vector<string> _listWarning;
            Subscription   _subscription; ///< 마지막 멤버라 먼저 풀린다 — 콜백이 만지는 `_mutex` · `_listWarning` 보다 먼저
        };
    } // namespace

    bool CatalogValidation::validate( const AssetTypeDef& kind, string_view assetID, vector<string>& outListWarning )
    {
        CatalogWarningCollector collector;
        const ScopedLogContext  context{ collector.makeContext() };
        const bool              bLoaded = AssetTypeRegistry::validateLoad( kind, AssetID{ assetID } );
        outListWarning                  = collector.take();
        return bLoaded;
    }
} // namespace sw::editor
