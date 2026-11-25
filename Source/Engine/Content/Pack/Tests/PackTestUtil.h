/**
 * @file PackTestUtil.h
 * @brief 테스트가 작은 팩을 쓰는 도우미입니다 — 항목을 경로 해시 순으로 세워 `PackStreamWriter` 로 씁니다.
 * @details 스트림 쓰기는 순서가 틀린 `append` 를 오류로 막으므로, 순서 없는 목록으로 팩을 짓는 테스트가 세우는 일을 한 곳에서 한다.
 *          블록 크기는 형식의 최소값, 창은 그 몇 배다 — 작은 항목도 블록 여럿 · 창 여럿을 지나게 하려는 값이 아니라 형식이 받는 가장 작은 값이다.
 */
#pragma once
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/Pack/PackWriter.h"

#include <utility>

namespace sw
{
    /** @struct PackTestUtil @brief 테스트용 팩 쓰기입니다. */
    struct PackTestUtil
    {
        static constexpr uint32 kBlockSize        = kPackMinBlockSize;
        static constexpr uint64 kWriteWindowBytes = 4ull * kPackMinBlockSize;

        /** @brief 압축 없는 테스트 팩의 쓰기 설정입니다. */
        static PackWriterDesc makeDesc( string_view mountDomain, PackCompressionKind compression = PackCompressionKind::None )
        {
            PackWriterDesc desc;
            desc._mountDomain      = string{ mountDomain };
            desc._compression      = compression;
            desc._blockSize        = kBlockSize;
            desc._writeWindowBytes = kWriteWindowBytes;
            return desc;
        }

        /** @brief @p listEntry 를 경로 해시 순으로 세워 @p packPath 에 씁니다. 실패하면 이유를 테스트 로그에 남기고 false 입니다. */
        static bool writePack( const AbsolutePath& packPath, const PackWriterDesc& desc, vector<std::pair<string, vector<uint8>>> listEntry )
        {
            VectorUtil::sort( listEntry, []( const std::pair<string, vector<uint8>>& left, const std::pair<string, vector<uint8>>& right )
            { return PackStreamWriter::makePathHash( left.first ) < PackStreamWriter::makePathHash( right.first ); } );
            PackStreamWriter writer;
            expected<void>   step = writer.open( packPath, desc );
            for ( size_t index = 0; index < listEntry.size() && step.has_value(); ++index )
            {
                step = writer.append( listEntry[index].first, PackEntryStorage::Blocks, std::move( listEntry[index].second ) );
            }
            const expected<PackWriteResult> closed = step.has_value() ? writer.close() : expected<PackWriteResult>( unexpected( step.error() ) );
            if ( closed.has_value() == false )
                SW_LOG_WARNING_ID( "PackTestUtil.WriteFailed", "Test pack '%#' was not written: %# '%#'", packPath.str(), closed.error().message(), closed.error().detail() );
            return closed.has_value();
        }

        /** @brief 글 항목으로 압축 없는 팩을 씁니다. @p mountDomain 이 비면 패치 팩이다(항목이 전체 경로). */
        static bool writeTextPack( const AbsolutePath& packPath, string_view mountDomain, const vector<std::pair<string, string>>& listEntry )
        {
            vector<std::pair<string, vector<uint8>>> listByteEntry;
            for ( const std::pair<string, string>& entry : listEntry )
            {
                listByteEntry.emplace_back( entry.first, vector<uint8>( entry.second.begin(), entry.second.end() ) );
            }
            return writePack( packPath, makeDesc( mountDomain ), std::move( listByteEntry ) );
        }
    };
} // namespace sw
