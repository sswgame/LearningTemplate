#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/BindlessIndexValidator.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Hash/HashUtil.h"

#include "Engine/Graphics/Debug/RenderDiagnostics.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 보고한 위치의 키입니다 — 패스 · 필드 이름의 비교 인덱스(`hashed_string` 이 같은 이름을 가리는 값)와 원소 번호를 **그대로** 둡니다.
         * @details 해시 하나로 접으면 서로 다른 위치가 충돌했을 때 두 번째 위치의 오류가 조용히 사라진다. 해시는 버킷만 고르고 같은 위치인지는 세 값이 가린다.
         */
        struct BindlessSiteKey
        {
            uint32 _passIndex{ 0 };
            uint32 _fieldIndex{ 0 };
            uint32 _element{ 0 };

            [[nodiscard]] uint64 computeHash() const { return HashUtil::makeHash( _passIndex, _fieldIndex, _element ); }
            [[nodiscard]] bool   operator==( const BindlessSiteKey& other ) const { return _passIndex == other._passIndex && _fieldIndex == other._fieldIndex && _element == other._element; }
        };

        struct BindlessIndexValidatorInternal
        {
            static const utf8* getSpaceName( RHIBindlessIndexSpace space )
            {
                return ( space == RHIBindlessIndexSpace::Texture ) ? "texture" : "buffer";
            }

            static BindlessSiteKey makeSiteKey( const hashed_string& passName, const hashed_string& fieldName, uint32 element )
            {
                return BindlessSiteKey{ passName.getIndex(), fieldName.getIndex(), element };
            }
        };
    } // namespace

    struct BindlessIndexReportLog::State
    {
        mutable mutex                                      _mutex;
        unordered_set<BindlessSiteKey> _uniqueReportedSite SW_GUARDED_BY( _mutex );
        unordered_set<BindlessSiteKey> _uniqueAbsentSite   SW_GUARDED_BY( _mutex );
        unordered_set<BindlessSiteKey> _uniqueStrideSite   SW_GUARDED_BY( _mutex );

        mutable shared_mutex                                       _strideMutex;                  ///< 병렬 기록 스레드가 바인드마다 읽으므로 보고 기록과 잠금을 나눈다
        unordered_map<RHIDescriptorIndex, uint32> _mapBufferStride SW_GUARDED_BY( _strideMutex ); ///< 구조 버퍼 SRV → 원소 바이트 수
    };

    bool BindlessIndexValidator::isEnabled()
    {
        return RenderDiagnostics::get()._bindlessIndexCheck >= BindlessIndexCheck::DeadIndex; // 배포본은 상수 기본값(끔)이라 검사가 접힌다
    }

    bool BindlessIndexValidator::isTracingAbsent()
    {
        return RenderDiagnostics::get()._bindlessIndexCheck >= BindlessIndexCheck::TraceAbsent;
    }

    bool BindlessIndexValidator::validate( const BindlessIndexCheckSite& site, RHIBindlessIndexSpace space, RHIDescriptorIndex index,
                                           const hashed_string& fieldName, uint32 element )
    {
        if ( site._pDevice == nullptr )
            return true;
        if ( index == kInvalidDescriptorIndex )
        {
            if ( isTracingAbsent() )
                (void)site._pDevice->getBindlessIndexReportLog().recordAbsent( site._passName, fieldName, element ); // 위치마다 한 번만 남긴다
            return true;
        }
        if ( site._pDevice->isBindlessIndexLive( space, index ) )
            return true;
        (void)site._pDevice->getBindlessIndexReportLog().report( space, index, site._passName, fieldName, element ); // 같은 위치를 두 번째 본 것이면 이미 로그가 있다
        return false;
    }

    bool BindlessIndexValidator::validateStride( const IRHIDevice* pDevice, const hashed_string& passName, const hashed_string& fieldName, uint32 shaderStride, RHIDescriptorIndex buffer )
    {
        if ( pDevice == nullptr || shaderStride == 0 )
            return true;
        const uint32 bufferStride = pDevice->getBindlessIndexReportLog().getBufferStride( buffer );
        if ( bufferStride == 0 || shaderStride == bufferStride )
            return true;
        (void)pDevice->getBindlessIndexReportLog().reportStride( passName, fieldName, shaderStride, bufferStride ); // 같은 자리는 한 번만 남긴다
        return false;
    }

    BindlessIndexReportLog::BindlessIndexReportLog()
        : _pState{ make_unique<State>() }
    {
    }

    BindlessIndexReportLog::~BindlessIndexReportLog() = default;

    bool BindlessIndexReportLog::report( RHIBindlessIndexSpace space, RHIDescriptorIndex index, const hashed_string& passName,
                                         const hashed_string& fieldName, uint32 element )
    {
        const BindlessSiteKey siteKey = BindlessIndexValidatorInternal::makeSiteKey( passName, fieldName, element );
        {
            ScopedLock<mutex> lock{ _pState->_mutex };
            if ( _pState->_uniqueReportedSite.insert( siteKey ).second == false )
                return false;
        }
        const utf8* pPassName  = passName.empty() ? "<no pass>" : passName.c_str();
        const utf8* pFieldName = fieldName.empty() ? "<unnamed>" : fieldName.c_str();
        SW_LOG_ERROR_ID( "RHI.BindlessIndexDead", "[BindlessIndex] pass '%#' field '%#'[%#] = %# is not a live %# slot — the shader would read a freed or never-written descriptor",
                         pPassName, pFieldName, element, index, BindlessIndexValidatorInternal::getSpaceName( space ) );
        return true;
    }

    bool BindlessIndexReportLog::recordAbsent( const hashed_string& passName, const hashed_string& fieldName, uint32 element )
    {
        const BindlessSiteKey siteKey = BindlessIndexValidatorInternal::makeSiteKey( passName, fieldName, element );
        {
            ScopedLock<mutex> lock{ _pState->_mutex };
            if ( _pState->_uniqueAbsentSite.insert( siteKey ).second == false )
                return false;
        }
        SW_LOG_INFO_ID( "BindlessIndexValidator.PassFieldAbsentKInvalidDescriptorIndex", "[BindlessIndex] pass '%#' field '%#'[%#] = absent (kInvalidDescriptorIndex) — the shader must not form a descriptor address from it",
                        passName.empty() ? "<no pass>" : passName.c_str(), fieldName.empty() ? "<unnamed>" : fieldName.c_str(), element );
        return true;
    }

    bool BindlessIndexReportLog::reportStride( const hashed_string& passName, const hashed_string& fieldName, uint32 shaderStride, uint32 bufferStride )
    {
        const BindlessSiteKey siteKey = BindlessIndexValidatorInternal::makeSiteKey( passName, fieldName, 0 );
        {
            ScopedLock<mutex> lock{ _pState->_mutex };
            if ( _pState->_uniqueStrideSite.insert( siteKey ).second == false )
                return false;
        }
        SW_LOG_ERROR_ID( "RHI.StructuredStrideMismatch", "[BindlessIndex] pass '%#' buffer '%#' was created with %#-byte elements but the shader declares %#-byte elements — element reads are misaligned",
                         passName.empty() ? "<no pass>" : passName.c_str(), fieldName.empty() ? "<unnamed>" : fieldName.c_str(), bufferStride, shaderStride );
        return true;
    }

    void BindlessIndexReportLog::recordBufferStride( RHIDescriptorIndex buffer, uint32 stride )
    {
        ScopedLock<shared_mutex> lock{ _pState->_strideMutex };
        _pState->_mapBufferStride[buffer] = stride;
    }

    void BindlessIndexReportLog::removeBufferStride( RHIDescriptorIndex buffer )
    {
        ScopedLock<shared_mutex> lock{ _pState->_strideMutex };
        _pState->_mapBufferStride.erase( buffer );
    }

    uint32 BindlessIndexReportLog::getBufferStride( RHIDescriptorIndex buffer ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _pState->_strideMutex };
        const auto                     it = _pState->_mapBufferStride.find( buffer );
        return it != _pState->_mapBufferStride.end() ? it->second : 0u;
    }

    uint32 BindlessIndexReportLog::getStrideReportCount() const
    {
        ScopedLock<mutex> lock{ _pState->_mutex };
        return static_cast<uint32>( _pState->_uniqueStrideSite.size() );
    }

    uint32 BindlessIndexReportLog::getReportCount() const
    {
        ScopedLock<mutex> lock{ _pState->_mutex };
        return static_cast<uint32>( _pState->_uniqueReportedSite.size() );
    }

    uint32 BindlessIndexReportLog::getAbsentNoteCount() const
    {
        ScopedLock<mutex> lock{ _pState->_mutex };
        return static_cast<uint32>( _pState->_uniqueAbsentSite.size() );
    }

    void BindlessIndexReportLog::clear()
    {
        ScopedLock<mutex> lock{ _pState->_mutex };
        // 저장소까지 놓는다 — clear 는 버킷을 남긴다.
        _pState->_uniqueReportedSite = unordered_set<BindlessSiteKey>{};
        _pState->_uniqueAbsentSite   = unordered_set<BindlessSiteKey>{};
        _pState->_uniqueStrideSite   = unordered_set<BindlessSiteKey>{};
    }
} // namespace sw
