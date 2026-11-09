#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneProfile.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    namespace
    {
        struct ZoneProfileInternal
        {
            /** @brief @p over 가 적은 칸만 @p inoutBase 에 얹습니다(종류 · 모듈 에셋 · 조건 · 대상, 매개변수는 이름으로, 경우는 이름으로). 경우의 바꾸기 · 빼기가 없는 이름이면 false 입니다. */
            static bool mergeModule( ZoneModuleDef& inoutBase, const ZoneModuleDef& over, string_view sourceName )
            {
                if ( over._kind.empty() == false )
                    inoutBase._kind = over._kind;
                if ( over._modulePath.isSet() )
                    inoutBase._modulePath = over._modulePath;
                if ( over._condition.empty() == false )
                    inoutBase._condition = over._condition;
                if ( over._target != ZoneTargetRole::Inherit )
                    inoutBase._target = over._target;
                for ( const ZoneParamDef& param : over._listParam )
                {
                    ZoneParamDef* pExisting = VectorUtil::findBy<&ZoneParamDef::_name>( inoutBase._listParam, param._name );
                    if ( pExisting != nullptr )
                        pExisting->_value = param._value;
                    else
                        inoutBase._listParam.push_back( param );
                }
                bool bOk = true;
                for ( const ZoneCaseDef& caseDef : over._listCase )
                {
                    const auto found  = std::find_if( inoutBase._listCase.begin(), inoutBase._listCase.end(), [&caseDef]( const ZoneCaseDef& existing )
                     { return existing._name == caseDef._name; } );
                    const bool bFound = found != inoutBase._listCase.end();
                    if ( caseDef._op != ZoneOverrideOp::Merge && bFound == false )
                    {
                        SW_LOG_ERROR_ID( "Zone.OverrideCaseMissing", "%#: module '%#' has no case '%#' to replace or remove", sourceName, inoutBase._name.c_str(), caseDef._name.c_str() );
                        bOk = false;
                        continue;
                    }
                    if ( caseDef._op == ZoneOverrideOp::Remove )
                    {
                        inoutBase._listCase.erase( found );
                        continue;
                    }
                    ZoneCaseDef merged = caseDef;
                    merged._op         = ZoneOverrideOp::Merge;
                    if ( bFound == false )
                    {
                        inoutBase._listCase.push_back( merged );
                        continue;
                    }
                    if ( caseDef._op == ZoneOverrideOp::Merge )
                    {
                        merged._condition = caseDef._condition.empty() ? found->_condition : caseDef._condition;
                        merged._do        = caseDef._do.empty() ? found->_do : caseDef._do;
                    }
                    *found = merged;
                }
                return bOk;
            }
        };
    } // namespace

    SW_MODULE_SHARED_ASSET_CACHE( ZoneProfiles, ZoneProfile, SW_GF_API, ZoneProfile::kAssetType.withLoader( AssetTypeLoader::of<ZoneProfile>( MemoryTag::Game ) ) );
    SW_MODULE_SHARED_ASSET_CACHE( ZoneModules, ZoneModuleDef, SW_GF_API, ZoneModuleDef::kAssetType.withLoader( AssetTypeLoader::of<ZoneModuleDef>( MemoryTag::Game ) ) );
} // namespace sw

namespace sw
{
    bool ZoneModuleDef::loadFromAsset( const AssetID& assetID )
    {
        *this = ZoneModuleDef{};
        if ( ReflectedXMLFile::loadDesc( assetID, *this ) == false )
        {
            *this = ZoneModuleDef{};
            return false;
        }
        // 모듈 에셋은 끝이다 — 다른 모듈 에셋을 가리키면 사슬이 프로필 상속과 겹친다(변형은 모듈 에셋의 `_base` 로 한다).
        if ( _modulePath.isSet() || _kind.empty() )
        {
            SW_LOG_ERROR_ID( "Zone.ModuleAssetInvalid", "%#: a zone module asset needs _kind and cannot name another _modulePath", assetID.view() );
            *this = ZoneModuleDef{};
            return false;
        }
        return true;
    }

    const ZoneParamDef* ZoneModuleDef::findParam( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&ZoneParamDef::_name>( _listParam, name );
    }

    bool ZoneProfile::loadFromAsset( const AssetID& assetID )
    {
        *this = ZoneProfile{};
        return finishLoad( assetID.view(), ReflectedXMLFile::loadDesc( assetID, *this ) );
    }

    bool ZoneProfile::loadFromDiskFile( string_view diskPath )
    {
        *this = ZoneProfile{};
        return finishLoad( diskPath, ReflectedXMLFile::loadDescFromDiskFile( diskPath, *this ) );
    }

    bool ZoneProfile::finishLoad( string_view path, bool bRead )
    {
        bool bOk = bRead;
        for ( size_t moduleIndex = 0; bOk && moduleIndex < _listModule.size(); ++moduleIndex )
        {
            ZoneModuleDef& module = _listModule[moduleIndex];
            // 파일 층의 `_op` 는 합치기가 먼저 소비한다 — 남아 있으면 부모 없는 파일에 적은 것이다(뺄 것이 없다).
            if ( module._op != ZoneOverrideOp::Merge )
            {
                SW_LOG_ERROR_ID( "Zone.OpWithoutBase", "%#: module '%#' has _op but the profile has no _base to merge into", path, module._name.c_str() );
                bOk = false;
                break;
            }
            if ( module._modulePath.isSet() )
            {
                shared_ptr<const ZoneModuleDef> pAsset = SharedAssetCache<ZoneModuleDef>::acquire( module._modulePath.get() );
                if ( pAsset == nullptr )
                {
                    SW_LOG_ERROR_ID( "Zone.ModuleAssetMissing", "%#: module '%#' names '%#', which could not be read", path, module._name.c_str(), module._modulePath.c_str() );
                    bOk = false;
                    break;
                }
                // 에셋이 출발이고 프로필이 적은 칸이 그 위에 얹힌다 — 덮어쓰기 층과 같은 합치기다.
                ZoneModuleDef merged = *pAsset;
                merged._name         = module._name;
                merged._modulePath   = module._modulePath;
                bOk                  = ZoneProfileInternal::mergeModule( merged, module, path );
                module               = merged;
            }
            for ( size_t otherIndex = 0; bOk && otherIndex < moduleIndex; ++otherIndex )
            {
                if ( _listModule[otherIndex]._name == module._name )
                {
                    SW_LOG_ERROR_ID( "Zone.ModuleDuplicate", "%#: module '%#' appears twice", path, module._name.c_str() );
                    bOk = false;
                }
            }
            if ( bOk && module._kind.empty() )
            {
                SW_LOG_ERROR_ID( "Zone.ModuleKindMissing", "%#: module '%#' has no _kind (and no module asset that gives one)", path, module._name.c_str() );
                bOk = false;
            }
        }
        if ( bOk == false )
            *this = ZoneProfile{};
        return bOk;
    }

    bool ZoneModuleDef::isEmptyOverride() const
    {
        return _op == ZoneOverrideOp::Merge && _kind.empty() && _modulePath.isSet() == false && _condition.empty() && _target == ZoneTargetRole::Inherit && _listParam.empty() &&
               _listCase.empty();
    }

    bool ZoneModuleDef::setOverrideParam( vector<ZoneModuleDef>& inoutListOverride, const hashed_string& moduleName, const hashed_string& paramName, string_view value )
    {
        auto found = std::find_if( inoutListOverride.begin(), inoutListOverride.end(), [&moduleName]( const ZoneModuleDef& over )
        { return over._name == moduleName; } );
        if ( found == inoutListOverride.end() )
        {
            ZoneModuleDef over;
            over._name = moduleName;
            over._op   = ZoneOverrideOp::Merge;
            inoutListOverride.push_back( std::move( over ) );
            found = inoutListOverride.end() - 1;
        }
        if ( found->_op != ZoneOverrideOp::Merge )
            return false; // 통째로 바꾸기 · 빼기는 매개변수 하나를 얹는 항목이 아니다
        for ( ZoneParamDef& param : found->_listParam )
        {
            if ( param._name == paramName )
            {
                param._value = string( value );
                return true;
            }
        }
        ZoneParamDef param;
        param._name  = paramName;
        param._value = string( value );
        found->_listParam.push_back( std::move( param ) );
        return true;
    }

    bool ZoneModuleDef::revertOverrideParam( vector<ZoneModuleDef>& inoutListOverride, const hashed_string& moduleName, const hashed_string& paramName )
    {
        const auto found = std::find_if( inoutListOverride.begin(), inoutListOverride.end(), [&moduleName]( const ZoneModuleDef& over )
        { return over._name == moduleName; } );
        if ( found == inoutListOverride.end() )
            return false;
        vector<ZoneParamDef>& listParam = found->_listParam;
        const auto            param     = std::find_if( listParam.begin(), listParam.end(), [&paramName]( const ZoneParamDef& item )
                       { return item._name == paramName; } );
        if ( param == listParam.end() )
            return false;
        listParam.erase( param );
        if ( found->isEmptyOverride() )
            inoutListOverride.erase( found ); // 빈 합치기 항목은 남기지 않는다 — "바꿈" 으로 보이면 안 된다
        return true;
    }

    bool ZoneModuleDef::revertOverrideModule( vector<ZoneModuleDef>& inoutListOverride, const hashed_string& moduleName )
    {
        const auto found = std::find_if( inoutListOverride.begin(), inoutListOverride.end(), [&moduleName]( const ZoneModuleDef& over )
        { return over._name == moduleName; } );
        if ( found == inoutListOverride.end() )
            return false;
        inoutListOverride.erase( found );
        return true;
    }

    bool ZoneProfile::applyOverrides( const vector<ZoneModuleDef>& listOverride, string_view sourceName )
    {
        bool bOk = true;
        for ( const ZoneModuleDef& over : listOverride )
        {
            const auto found  = std::find_if( _listModule.begin(), _listModule.end(), [&over]( const ZoneModuleDef& module )
             { return module._name == over._name; } );
            const bool bFound = found != _listModule.end();
            if ( over._op != ZoneOverrideOp::Merge && bFound == false )
            {
                SW_LOG_ERROR_ID( "Zone.OverrideModuleMissing", "%#: the profile has no module '%#' to replace or remove", sourceName, over._name.c_str() );
                bOk = false;
                continue;
            }
            if ( over._op == ZoneOverrideOp::Remove )
            {
                _listModule.erase( found );
                continue;
            }
            ZoneModuleDef base = ( bFound && over._op == ZoneOverrideOp::Merge ) ? *found : ZoneModuleDef{};
            base._name         = over._name;
            // 바꾸는 모듈 · 더하는 모듈도 모듈 에셋을 가리킬 수 있다 — 에셋이 출발이고 적은 칸이 얹힌다.
            const DataPath& assetPath = over._modulePath.isSet() ? over._modulePath : base._modulePath;
            if ( over._modulePath.isSet() || ( bFound == false && assetPath.isSet() ) )
            {
                shared_ptr<const ZoneModuleDef> pAsset = SharedAssetCache<ZoneModuleDef>::acquire( assetPath.get() );
                if ( pAsset == nullptr )
                {
                    SW_LOG_ERROR_ID( "Zone.ModuleAssetMissing", "%#: module '%#' names '%#', which could not be read", sourceName, over._name.c_str(), assetPath.c_str() );
                    bOk = false;
                    continue;
                }
                base       = *pAsset;
                base._name = over._name;
            }
            bOk      = ZoneProfileInternal::mergeModule( base, over, sourceName ) && bOk;
            base._op = ZoneOverrideOp::Merge;
            if ( bFound )
                *found = base;
            else
                _listModule.push_back( base );
        }
        for ( const ZoneModuleDef& module : _listModule )
        {
            if ( module._kind.empty() )
            {
                SW_LOG_ERROR_ID( "Zone.ModuleKindMissing", "%#: module '%#' has no _kind after the overrides", sourceName, module._name.c_str() );
                bOk = false;
            }
        }
        return bOk;
    }

    const ZoneModuleDef* ZoneProfile::findModule( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&ZoneModuleDef::_name>( _listModule, name );
    }
} // namespace sw
