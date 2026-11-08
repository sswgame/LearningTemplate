#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModResolver.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Expected.h"
#include "Core/Log/Logger.h"

namespace sw
{
    namespace
    {
        struct ModResolverInternal
        {
            static optional<size_t> findIndex( const vector<ModManifest>& listManifest, string_view id )
            {
                for ( size_t index = 0; index < listManifest.size(); ++index )
                {
                    if ( listManifest[index]._id == id )
                        return index;
                }
                return nullopt;
            }

            /** @brief 한 모드만 보는 검사(판 범위 · 덮어쓰기 허락 · 의존 · 충돌)입니다. */
            [[nodiscard]] static bool checkMod( const vector<ModManifest>& listManifest, size_t modIndex, const ModSupportSettings& settings )
            {
                const ModManifest& manifest = listManifest[modIndex];
                if ( settings._contentVersion < manifest._minContentVersion || settings._contentVersion > manifest._maxContentVersion )
                {
                    SW_LOG_ERROR_ID( "ModResolver.ContentVersion", "Mod '%#' supports content versions %# to %#, but the game is %#", manifest._id, manifest._minContentVersion,
                                     manifest._maxContentVersion, settings._contentVersion );
                    return false;
                }
                for ( const string& path : manifest._listOverride )
                {
                    if ( settings.isOverridable( path ) == false )
                    {
                        SW_LOG_ERROR_ID( "ModResolver.OverrideNotAllowed", "Mod '%#' overrides '%#', which the game does not let mods override (ModSupportSettings <Overridable>)", manifest._id,
                                         path );
                        return false;
                    }
                }
                // 층도 기본 리소스를 바꾸므로 같은 허락을 받는다. 여러 모드가 한 리소스에 층을 얹는 것은 충돌이 아니다(차이가 차례로 얹힌다).
                for ( const string& path : manifest._listLayer )
                {
                    if ( settings.isOverridable( path ) == false )
                    {
                        SW_LOG_ERROR_ID( "ModResolver.LayerNotAllowed", "Mod '%#' layers '%#', which the game does not let mods change (ModSupportSettings <Overridable>)", manifest._id, path );
                        return false;
                    }
                }
                for ( const ModDependency& dependency : manifest._listDependency )
                {
                    const optional<size_t> foundDependency = findIndex( listManifest, dependency._id );
                    if ( foundDependency.has_value() == false )
                    {
                        if ( dependency._bOptional )
                            continue;
                        SW_LOG_ERROR_ID( "ModResolver.MissingDependency", "Mod '%#' needs mod '%#' %# or later, which is not enabled", manifest._id, dependency._id,
                                         dependency._minVersion.toString() );
                        return false;
                    }
                    const size_t dependencyIndex = *foundDependency;
                    if ( listManifest[dependencyIndex]._version < dependency._minVersion )
                    {
                        SW_LOG_ERROR_ID( "ModResolver.DependencyTooOld", "Mod '%#' needs mod '%#' %# or later, but %# is enabled", manifest._id, dependency._id,
                                         dependency._minVersion.toString(), listManifest[dependencyIndex]._version.toString() );
                        return false;
                    }
                }
                for ( const string& conflictID : manifest._listConflict )
                {
                    if ( findIndex( listManifest, conflictID ).has_value() )
                    {
                        SW_LOG_ERROR_ID( "ModResolver.Conflict", "Mod '%#' cannot be enabled together with mod '%#'", manifest._id, conflictID );
                        return false;
                    }
                }
                return true;
            }

            /** @brief @p left 가 @p right 보다 먼저(아래) 서야 하면 true 입니다 — 의존이 정하지 않는 두 모드의 순서(우선순위 · id). */
            static bool isLower( const ModManifest& left, const ModManifest& right )
            {
                return left._priority != right._priority ? left._priority < right._priority : left._id < right._id;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ModResolver::resolveOrder( const vector<ModManifest>& listManifest, const ModSupportSettings& settings, vector<size_t>& outListOrder )
    {
        using Internal = ModResolverInternal;
        outListOrder.clear();
        if ( listManifest.size() > settings._maxModCount )
        {
            SW_LOG_ERROR_ID( "ModResolver.TooManyMods", "%# mods are enabled, but the game allows at most %#", listManifest.size(), settings._maxModCount );
            return false;
        }
        for ( size_t index = 0; index < listManifest.size(); ++index )
        {
            if ( Internal::findIndex( listManifest, listManifest[index]._id ).value() != index ) // 자기 자신은 늘 찾는다 — 앞에 같은 id 가 있으면 그 번호다
            {
                SW_LOG_ERROR_ID( "ModResolver.DuplicateID", "Mod '%#' is enabled twice", listManifest[index]._id );
                return false;
            }
            if ( Internal::checkMod( listManifest, index, settings ) == false )
                return false;
        }

        // Kahn 위상 정렬 — 남은 의존이 없는 모드 가운데 가장 낮은 것(`isLower`)을 차례로 세운다. 못 세운 모드가 남으면 순환이다.
        vector<uint8> listPlaced( listManifest.size(), 0 );
        while ( outListOrder.size() < listManifest.size() )
        {
            optional<size_t> nextIndex;
            for ( size_t index = 0; index < listManifest.size(); ++index )
            {
                if ( listPlaced[index] != 0 )
                    continue;
                bool bReady = true;
                for ( const ModDependency& dependency : listManifest[index]._listDependency )
                {
                    const optional<size_t> dependencyIndex = Internal::findIndex( listManifest, dependency._id );
                    if ( dependencyIndex.has_value() && listPlaced[*dependencyIndex] == 0 )
                        bReady = false;
                }
                if ( bReady && ( nextIndex.has_value() == false || Internal::isLower( listManifest[index], listManifest[*nextIndex] ) ) )
                    nextIndex = index;
            }
            if ( nextIndex.has_value() == false )
            {
                for ( size_t index = 0; index < listManifest.size(); ++index )
                {
                    if ( listPlaced[index] == 0 )
                    {
                        SW_LOG_ERROR_ID( "ModResolver.DependencyCycle", "Mod '%#' is part of a dependency cycle", listManifest[index]._id );
                        break;
                    }
                }
                outListOrder.clear();
                return false;
            }
            listPlaced[*nextIndex] = 1;
            outListOrder.push_back( *nextIndex );
        }

        // 같은 리소스를 덮어쓰는 두 모드 — 뒤(위) 모드가 앞 모드에 직접 의존할 때만 받는다. 아니면 어느 쪽이 이길지가 우선순위 숫자의 우연이 된다.
        for ( size_t upper = 0; upper < outListOrder.size(); ++upper )
        {
            const ModManifest& upperMod = listManifest[outListOrder[upper]];
            for ( size_t lower = 0; lower < upper; ++lower )
            {
                const ModManifest& lowerMod = listManifest[outListOrder[lower]];
                if ( upperMod.findDependency( lowerMod._id ) != nullptr )
                    continue;
                for ( const string& path : upperMod._listOverride )
                {
                    if ( VectorUtil::contains( lowerMod._listOverride, path ) )
                    {
                        SW_LOG_ERROR_ID( "ModResolver.OverrideCollision", "Mods '%#' and '%#' both override '%#' - one must depend on the other to decide which wins", lowerMod._id,
                                         upperMod._id, path );
                        outListOrder.clear();
                        return false;
                    }
                }
            }
        }
        return true;
    }
} // namespace sw
