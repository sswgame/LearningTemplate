#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/PasswordHashQueue.h"

#include "Core/Delegate/Delegate.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/LoginTypes.h"

namespace sw
{
    namespace
    {
        struct PasswordHashQueueInternal
        {
            static constexpr const utf8* kStageName           = "PasswordHashQueue";
            static constexpr int64       kWaitStepNanoseconds = constant::time::kNanosecondsPerMillisecond; ///< `waitForRunning` 이 한 번 자는 폭
        };
    } // namespace
} // namespace sw

namespace sw
{
    PasswordHashTicket::PasswordHashTicket( PasswordHashTicket&& other ) noexcept
        : _pQueue{ other._pQueue }
    {
        other._pQueue = nullptr;
    }

    PasswordHashTicket& PasswordHashTicket::operator=( PasswordHashTicket&& other ) noexcept
    {
        if ( this == &other )
            return *this;
        if ( _pQueue != nullptr )
            _pQueue->returnSeat();
        _pQueue       = other._pQueue;
        other._pQueue = nullptr;
        return *this;
    }

    PasswordHashTicket::~PasswordHashTicket()
    {
        if ( _pQueue != nullptr )
            _pQueue->returnSeat(); // 쓰지 않은 자리는 돌려준다
    }

    void PasswordHashQueue::HashJob::run()
    {
        // 해시 구현은 스레드 안전하다(`IPasswordHasher` 계약). 비밀번호 상한을 넘는 입력은 빈 글로 해시한다 — 느린 해시에 거대한 입력을 넣지 않는다(시간은 같다).
        const bool        bWithinLimit = _request._password.size() <= static_cast<size_t>( LoginConstant::kMaxPasswordSize );
        const string_view password     = bWithinLimit ? string_view( _request._password ) : string_view{};
        bool              bComputed    = _request._specCount > 0;
        for ( int32 index = 0; index < _request._specCount && bComputed; ++index )
        {
            const PasswordHashSpec& spec = _request._arrSpec[index];
            bComputed                    = _pOwner->_pHasher->computePasswordHash( password, spec._arrSalt, LoginConstant::kSaltSize, spec._params, _completion._arrHash[index],
                                                                                   LoginConstant::kPasswordHashSize );
        }
        _completion._bComputed = bComputed ? SW_TRUE : SW_FALSE;
        StringUtil::wipeSecret( _request._password ); // 해시가 끝난 워커에서 비밀번호를 지운다
        _pOwner->markFinished( this );
    }

    PasswordHashQueue::PasswordHashQueue()
        : _listFinished{}
        , _listRunning{}
        , _listWaiting{}
        , _stage{}
        , _pTaskManager{ nullptr }
        , _pHasher{ nullptr }
        , _settings{}
        , _nextJobID{ 1 }
        , _reservedCount{ 0 }
        , _runningCount{ 0 }
    {
    }

    PasswordHashQueue::~PasswordHashQueue()
    {
        vector<PasswordHashCompletion> listDropped;
        shutdown( listDropped ); // 워커가 이 객체의 칸을 쓰는 채로 사라지지 않게 기다린다
    }

    expected<void> PasswordHashQueue::initialize( TaskManager& taskManager, IPasswordHasher& hasher, const PasswordHashQueueConfig& settings )
    {
        if ( settings._maxConcurrentPasswordHashes < 1 || settings._maxQueuedPasswordHashes < 1 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the password hash queue needs _maxConcurrentPasswordHashes and _maxQueuedPasswordHashes of at least 1 (the account kit settings set them)" } );
        _pTaskManager = &taskManager;
        _pHasher      = &hasher;
        _settings     = settings;
        return {};
    }

    optional<PasswordHashTicket> PasswordHashQueue::tryReserve()
    {
        const int32 capacity = _settings._maxConcurrentPasswordHashes + _settings._maxQueuedPasswordHashes;
        if ( _pTaskManager == nullptr || _reservedCount >= capacity )
            return nullopt;
        ++_reservedCount;
        return PasswordHashTicket{ this };
    }

    uint64 PasswordHashQueue::submit( PasswordHashTicket&& ticket, PasswordHashRequest&& request )
    {
        SW_ASSERT( ticket._pQueue == this && "a password hash ticket goes back to the queue that issued it" );
        ticket.release(); // 자리는 이제 일의 것이다 — 완료를 거둘 때 돌려준다
        unique_ptr<HashJob> pJob = make_unique<HashJob>();
        pJob->_pOwner            = this;
        pJob->_request           = std::move( request );
        pJob->_completion._jobID = _nextJobID++;
        const uint64 jobID       = pJob->_completion._jobID;
        if ( _runningCount < _settings._maxConcurrentPasswordHashes )
            launch( std::move( pJob ) );
        else
            _listWaiting.push_back( std::move( pJob ) );
        return jobID;
    }

    void PasswordHashQueue::launch( unique_ptr<HashJob> pJob )
    {
        HashJob* const pRaw = pJob.get();
        _listRunning.push_back( std::move( pJob ) );
        ++_runningCount;
        TaskManager& taskManager = *_pTaskManager;
        TaskHandle   task        = taskManager.emplaceTask( PasswordHashQueueInternal::kStageName, SW_DELEGATE_METHOD( TaskDelegate, &HashJob::run, pRaw ) );
        taskManager.addToStage( _stage, task, PasswordHashQueueInternal::kStageName );
        taskManager.submit( task );
    }

    void PasswordHashQueue::markFinished( HashJob* pJob )
    {
        ScopedLock<mutex> lock{ _finishedMutex };
        _listFinished.push_back( pJob );
    }

    void PasswordHashQueue::returnSeat()
    {
        SW_ASSERT( _reservedCount > 0 );
        --_reservedCount;
    }

    int32 PasswordHashQueue::pollCompletions( vector<PasswordHashCompletion>& outListCompletion )
    {
        vector<HashJob*> listFinished;
        {
            ScopedLock<mutex> lock{ _finishedMutex };
            listFinished.swap( _listFinished );
        }
        for ( HashJob* pFinished : listFinished )
        {
            for ( size_t index = 0; index < _listRunning.size(); ++index )
            {
                if ( _listRunning[index].get() != pFinished )
                    continue;
                outListCompletion.push_back( pFinished->_completion );
                _listRunning.erase( _listRunning.begin() + static_cast<ptrdiff_t>( index ) );
                break;
            }
            --_runningCount;
            returnSeat();
        }
        // 빈 돌 자리에 기다리던 일을 맡긴 순서대로 올린다.
        while ( _listWaiting.empty() == false && _runningCount < _settings._maxConcurrentPasswordHashes )
        {
            unique_ptr<HashJob> pNext = std::move( _listWaiting.front() );
            _listWaiting.pop_front();
            launch( std::move( pNext ) );
        }
        return static_cast<int32>( listFinished.size() );
    }

    bool PasswordHashQueue::waitForRunning( int64 timeoutMs )
    {
        const int64 deadline = MonotonicClock::nowNanoseconds() + timeoutMs * constant::time::kNanosecondsPerMillisecond;
        while ( true )
        {
            size_t finishedCount = 0;
            {
                ScopedLock<mutex> lock{ _finishedMutex };
                finishedCount = _listFinished.size();
            }
            if ( finishedCount >= _listRunning.size() )
                return true;
            if ( MonotonicClock::nowNanoseconds() >= deadline )
                return false;
            MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + PasswordHashQueueInternal::kWaitStepNanoseconds );
        }
    }

    void PasswordHashQueue::shutdown( vector<PasswordHashCompletion>& outListCompletion )
    {
        if ( _pTaskManager != nullptr && _stage.isValid() )
            _pTaskManager->waitStage( _stage, "PasswordHashQueue::shutdown" ); // 도는 해시는 끝까지 — 워커가 쓰는 칸을 먼저 놓지 않는다
        // 기다리던 일은 돌리지 않는다 — 취소 완료로 돌려준다(먼저 빼야 아래 거두기가 올리지 않는다).
        deque<unique_ptr<HashJob>> listWaiting;
        listWaiting.swap( _listWaiting );
        (void)pollCompletions( outListCompletion );
        for ( unique_ptr<HashJob>& pWaiting : listWaiting )
        {
            StringUtil::wipeSecret( pWaiting->_request._password );
            pWaiting->_completion._bCancelled = SW_TRUE;
            outListCompletion.push_back( pWaiting->_completion );
            returnSeat();
        }
        _pTaskManager = nullptr;
        _pHasher      = nullptr;
    }
} // namespace sw
