#include "pch.h"

#include "Core/File/FileWritePolicy.h"

#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileSystem.h"

namespace sw
{
    namespace
    {
        /** @brief 프로세스에 하나인 쓰기 정책 칸 묶음입니다. 모두 상수 초기화된다(정적 초기화 중의 쓰기도 읽는다). */
        struct FileWritePolicySlots
        {
            ObserverSlot<FileWritePolicy::RuleSignature>         _writeFilter{};  ///< `bindWriteFilter`
            ObserverSlot<FileWritePolicy::RuleSignature>         _createRule{};   ///< `bindCreateRule`
            ObserverSlot<FileWritePolicy::CheckoutRuleSignature> _checkoutRule{}; ///< `bindCheckoutRule`
        };

        struct FileWritePolicyInternal
        {
            static inline FileWritePolicySlots s_policySlots{};

            /** @brief @p path 에 체크아웃하지 않은 파일이 있으면 경로와 이유를 오류로 남기고 true 입니다. */
            [[nodiscard]] static bool refuseNotOpenForEdit( const AbsolutePath& path )
            {
                const optional<AbsolutePath> lockedPath = FileWritePolicy::findNotOpenForEdit( path );
                if ( lockedPath.has_value() == false )
                    return false;
                SW_LOG_ERROR_ID( "File.NotOpenForEdit", "Refused to change %# - '%#' is read-only (not checked out), check it out first", path.str(), lockedPath->str() );
                return true;
            }
        };
    } // namespace

    FileWritePolicy::RuleSubscription FileWritePolicy::bindWriteFilter( RuleSignature* filter )
    {
        return FileWritePolicyInternal::s_policySlots._writeFilter.bind( filter );
    }

    FileWritePolicy::RuleSubscription FileWritePolicy::bindCreateRule( RuleSignature* rule )
    {
        return FileWritePolicyInternal::s_policySlots._createRule.bind( rule );
    }

    FileWritePolicy::CheckoutRuleSubscription FileWritePolicy::bindCheckoutRule( CheckoutRuleSignature* rule )
    {
        return FileWritePolicyInternal::s_policySlots._checkoutRule.bind( rule );
    }

    FileWritePolicy::CheckoutRuleSuspension FileWritePolicy::suspendCheckoutRule()
    {
        return CheckoutRuleSuspension{ FileWritePolicyInternal::s_policySlots._checkoutRule };
    }

    bool FileWritePolicy::isWriteAllowed( const AbsolutePath& path )
    {
        if ( RuleSignature* const filter = FileWritePolicyInternal::s_policySlots._writeFilter.get(); filter != nullptr )
        {
            const expected<void> evaluation = filter( path.str() );
            if ( evaluation.has_value() == false )
            {
                SW_LOG_ERROR_ID( "File.WriteRefused", "Changing %# was refused: %# (%#)", path.str(), evaluation.error().message(), evaluation.error().detail() );
                return false;
            }
        }
        if ( FileWritePolicyInternal::refuseNotOpenForEdit( path ) )
            return false;
        return isCreateAllowed( path );
    }

    bool FileWritePolicy::isCreateAllowed( const AbsolutePath& path )
    {
        // 이름 규칙은 새로 생기는 자리만 본다. 있는 파일을 덮어쓰거나 지우는 것은 이름을 만들지 않는다.
        RuleSignature* const rule = FileWritePolicyInternal::s_policySlots._createRule.get();
        if ( rule == nullptr || FileUtil::exists( path ) )
            return true;
        const expected<void> evaluation = rule( path.str() );
        if ( evaluation.has_value() )
            return true;
        SW_LOG_ERROR_ID( "File.CreateRefused", "Creating %# was refused: %# (%#)", path.str(), evaluation.error().message(), evaluation.error().detail() );
        return false;
    }

    optional<AbsolutePath> FileWritePolicy::findNotOpenForEdit( const AbsolutePath& path )
    {
        CheckoutRuleSignature* const   rule = FileWritePolicyInternal::s_policySlots._checkoutRule.get();
        PlatformFileSystem::FileStatus status;
        if ( rule == nullptr || path.empty() || PlatformFileSystem::queryStatus( path.str(), status ) == false )
            return nullopt; // 없는 자리는 지우거나 옮길 것이 없다
        if ( status._kind == PlatformFileSystem::FileKind::File )
            return status._bReadOnly && rule( path.str() ) ? optional<AbsolutePath>{ path } : nullopt;
        if ( status._kind != PlatformFileSystem::FileKind::Directory || status._bLink )
            return nullopt; // 폴더 링크는 링크만 지운다(가리키는 폴더는 그대로다)
        optional<AbsolutePath> found;
        (void)FileUtil::forEachDirectoryEntry( path, true, [&found, rule]( const DirectoryEntry& entry )
        {
            if ( entry._bDirectory == false && FileUtil::isReadOnlyFile( entry.makePath() ) && rule( entry._path ) )
            {
                found = entry.makePath();
                return false;
            }
            return true;
        } ); // 폴더를 끝까지 읽지 못하면 본 데까지다. 지우기 자체가 못 지운 항목을 알린다
        return found;
    }
} // namespace sw
