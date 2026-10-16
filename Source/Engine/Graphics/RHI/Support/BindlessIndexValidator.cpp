#include "pch.h"

#include "Engine/Graphics/RHI/Support/BindlessIndexValidator.h"

#if !defined( SW_SHIPPING )
    #include "Core/Common/HashUtil.h"
    #include "Core/Concurrency/mutex.h"
    #include "Core/Container/unordered_set.h"
    #include "Core/GlobalVariable/GlobalVariableManager.h"

    #include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    SW_LOG_CALLER( "BindlessIndexValidator" );

    namespace
    {
        struct BindlessIndexValidatorInternal
        {
            // 검사 수준: 0 끔, 1 죽은 번호(오류), 2 죽은 번호와 "없음(kInvalidDescriptorIndex)" 이 셰이더로 가는 곳(정보).
            static constexpr int32 kLevelDeadIndex   = 1;
            static constexpr int32 kLevelTraceAbsent = 2;
            // Debug 는 항상 검사한다. Release 는 성능 측정에 쓰는 빌드라(검사는 드로우마다 읽기 잠금 하나) 끄고, 필요할 때 -gv_ 로 켠다.
    #if defined( SW_DEBUG )
            static constexpr int32 kDefaultLevel = kLevelDeadIndex;
    #else
            static constexpr int32 kDefaultLevel = 0;
    #endif

            static const utf8* getSpaceName( RHIBindlessIndexSpace space )
            {
                return ( space == RHIBindlessIndexSpace::Texture ) ? "texture" : "buffer";
            }

            static uint64 makeSiteKey( const hashed_string& passName, const hashed_string& fieldName, uint32 element )
            {
                const uint64 siteKey = HashUtil::combine( static_cast<uint64>( passName.getHash() ), static_cast<uint64>( fieldName.getHash() ) );
                return HashUtil::combine( siteKey, static_cast<uint64>( element ) );
            }
        };

        mutex                 s_reportMutex;
        unordered_set<uint64> s_uniqueReportedSite;
        unordered_set<uint64> s_uniqueAbsentSite;
    } // namespace

    /**
     * @brief 셰이더에 넘기기 직전의 bindless 인덱스를 검사합니다(Graphics README "함정과 주의").
     * @details 1 이면 죽은 번호를 `[BindlessIndex] pass '<패스>' field '<이름>'[<원소>] = <값> is not a live <공간> slot` 오류로 위치마다 한 번 남긴다.
     *          2 이면 "없음"(kInvalidDescriptorIndex)이 셰이더로 가는 위치도 `[BindlessIndex] ... = absent` 정보로 남긴다. 셰이더가 분기로 거르는
     *          번호인데 드라이버가 디스크립터 읽기를 분기 앞으로 끌어올리면 디바이스 손실이 나므로, 손실이 났을 때 후보 위치를 고르는 데 쓴다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_validateBindlessIndex, BindlessIndexValidatorInternal::kDefaultLevel,
                             "셰이더에 넘기는 bindless 인덱스 검사 — 0 끔, 1 죽은 번호(Debug 기본), 2 '없음' 번호가 가는 자리까지" );

    bool BindlessIndexValidator::isEnabled()
    {
        return gv_validateBindlessIndex >= BindlessIndexValidatorInternal::kLevelDeadIndex;
    }

    bool BindlessIndexValidator::isTracingAbsent()
    {
        return gv_validateBindlessIndex >= BindlessIndexValidatorInternal::kLevelTraceAbsent;
    }

    bool BindlessIndexValidator::validate( const BindlessIndexCheckSite& site, RHIBindlessIndexSpace space, RHIDescriptorIndex index,
                                           const hashed_string& fieldName, uint32 element )
    {
        if ( site._pDevice == nullptr )
            return true;
        if ( index == kInvalidDescriptorIndex )
        {
            if ( isTracingAbsent() )
                (void)noteAbsent( site._passName, fieldName, element ); // 위치마다 한 번만 남긴다
            return true;
        }
        if ( site._pDevice->isBindlessIndexLive( space, index ) )
            return true;
        (void)report( space, index, site._passName, fieldName, element ); // 같은 위치를 두 번째 본 것이면 이미 로그가 있다
        return false;
    }

    bool BindlessIndexValidator::report( RHIBindlessIndexSpace space, RHIDescriptorIndex index, const hashed_string& passName,
                                         const hashed_string& fieldName, uint32 element )
    {
        const uint64 siteKey = BindlessIndexValidatorInternal::makeSiteKey( passName, fieldName, element );
        {
            std::scoped_lock<mutex> lock{ s_reportMutex };
            if ( s_uniqueReportedSite.insert( siteKey ).second == false )
                return false;
        }
        const utf8* pPassName  = passName.empty() ? "<no pass>" : passName.c_str();
        const utf8* pFieldName = fieldName.empty() ? "<unnamed>" : fieldName.c_str();
        SW_LOG_ERROR( "[BindlessIndex] pass '%#' field '%#'[%#] = %# is not a live %# slot — the shader would read a freed or never-written descriptor",
                      pPassName, pFieldName, element, index, BindlessIndexValidatorInternal::getSpaceName( space ) );
        return true;
    }

    bool BindlessIndexValidator::noteAbsent( const hashed_string& passName, const hashed_string& fieldName, uint32 element )
    {
        const uint64 siteKey = BindlessIndexValidatorInternal::makeSiteKey( passName, fieldName, element );
        {
            std::scoped_lock<mutex> lock{ s_reportMutex };
            if ( s_uniqueAbsentSite.insert( siteKey ).second == false )
                return false;
        }
        SW_LOG_INFO( "[BindlessIndex] pass '%#' field '%#'[%#] = absent (kInvalidDescriptorIndex) — the shader must not form a descriptor address from it",
                     passName.empty() ? "<no pass>" : passName.c_str(), fieldName.empty() ? "<unnamed>" : fieldName.c_str(), element );
        return true;
    }

    uint32 BindlessIndexValidator::getReportCount()
    {
        std::scoped_lock<mutex> lock{ s_reportMutex };
        return static_cast<uint32>( s_uniqueReportedSite.size() );
    }

    uint32 BindlessIndexValidator::getAbsentNoteCount()
    {
        std::scoped_lock<mutex> lock{ s_reportMutex };
        return static_cast<uint32>( s_uniqueAbsentSite.size() );
    }

    void BindlessIndexValidator::clearReports()
    {
        std::scoped_lock<mutex> lock{ s_reportMutex };
        s_uniqueReportedSite.clear();
        s_uniqueAbsentSite.clear();
    }
} // namespace sw
#endif
