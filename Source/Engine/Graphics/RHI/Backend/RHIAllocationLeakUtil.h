/**
 * @file RHIAllocationLeakUtil.h
 * @brief 장치 종료 때 할당기(VMA · D3D12MA)에 남은 할당을 이름과 함께 보고하는 도우미입니다(Vulkan · DX12 가 같이 쓴다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"

namespace sw
{
    /**
     * @struct RHIAllocationLeakUtil
     * @brief 두 라이브러리의 통계 JSON(`vmaBuildStatsString` · `D3D12MA::Allocator::BuildStatsString`, 자세한 지도)은 할당마다 `"Name": "..."` 을 같은 모양으로 적습니다 — 그 이름만 모읍니다.
     */
    struct RHIAllocationLeakUtil
    {
        /** @brief 통계 JSON @p pStatsJSON(nullptr 이면 빈 것)의 할당 이름을 나온 차례로 모읍니다(이름 없는 할당은 들지 않는다). */
        static vector<string> collectNames( const utf8* pStatsJSON )
        {
            vector<string>        listName;
            constexpr string_view kNameKey = "\"Name\"";
            const string_view     stats    = pStatsJSON != nullptr ? string_view{ pStatsJSON } : string_view{};
            size_t                position = stats.find( kNameKey );
            while ( position != string_view::npos )
            {
                const size_t valueBegin = stats.find( '"', position + kNameKey.size() );
                const size_t valueEnd   = valueBegin != string_view::npos ? stats.find( '"', valueBegin + 1 ) : string_view::npos;
                if ( valueEnd == string_view::npos )
                    break;
                listName.push_back( string{ stats.substr( valueBegin + 1, valueEnd - valueBegin - 1 ) } );
                position = stats.find( kNameKey, valueEnd + 1 );
            }
            return listName;
        }

        /** @brief 남은 할당 @p liveCount 개를 오류로 남깁니다. 이름은 통계 JSON 에서 모은다. */
        static void report( const utf8* pBackendName, uint32 liveCount, const utf8* pStatsJSON )
        {
            SW_LOG_ERROR_ID( "RHI.AllocationLeak", "%# device memory allocations are still alive while %# shuts down - their owners were not released before the device", liveCount, pBackendName );
            for ( const string& name : collectNames( pStatsJSON ) )
            {
                SW_LOG_ERROR_ID( "RHI.AllocationLeakName", "  leaked %# allocation: '%#'", pBackendName, name.c_str() );
            }
        }
    };
} // namespace sw
