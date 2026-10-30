/**
 * @file DomainAssetID.h
 * @brief 도메인을 품은 리소스 id(`common/textures/rock.dds`)입니다. 쓰기 자리를 정하는 입구(`AssetPaths::resolveWritePath`)는 이 형식만 받습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"

namespace sw
{
    class AssetPaths;

    /**
     * @class DomainAssetID
     * @brief 어느 도메인(`engine` · `common` · `game/<pack>` · `editor` · `dlc/<이름>` · 붙인 디스크 도메인)의 파일인지 정해진 소문자 리소스 id 입니다. 만드는 쪽은 `AssetPaths` 하나입니다.
     * @details 읽기는 상대 키(`models/x.mesh`)를 검색 순서로 겹쳐 찾지만, 쓰기는 어느 도메인에 쓸지가 정해져야 한다. 쓰기 입구가 문자열을 받으면 아직 없는 상대 키가
     *          도메인 없이 흘러가 작업 폴더에 떨어졌다(`getWritePath` 가 못 푼 인자를 그대로 돌려줬다). 그래서 쓰기 입구는 이 형식만 받고, 이 형식은
     *          `AssetPaths::toWriteID` 가 전역 id · 리소스 트리 안의 절대 경로 · 이미 있는 파일의 상대 키(이긴 파일)에서만 만든다(언리얼 pak 의 마운트 지점, Godot `res://`).
     */
    class DomainAssetID
    {
    public:
        /** @brief 전역 id 문자열입니다(예: `game/empty/maps/x.scene.xml`). */
        const string& str() const { return _id; }
        const utf8*   c_str() const { return _id.c_str(); }

    private:
        friend class AssetPaths;

        explicit DomainAssetID( string id )
            : _id{ std::move( id ) }
        {
        }

        string _id;
    };
} // namespace sw
