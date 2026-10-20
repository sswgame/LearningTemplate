/**
 * @file GamePackDataUtil.h
 * @brief 게임 팩 안 데이터 파일의 경로를 데이터 종류 등록 줄(`SW_RESOURCE_DATA_KIND`)에서 만드는 도우미입니다.
 * @details 카탈로그 경로의 원본은 등록 줄 하나다. `"game/"` 접두와 `"/data/weapons.xml"` 접미를 가진 종류는 팩 루트(`game/shooter3d`)에 접미를 붙인 것이 그 팩의 파일이다.
 *          게임 코드와 에디터 확장은 경로를 적지 않고 카탈로그 타입의 `kDataKind` 와 팩 루트로 경로를 얻는다. 프레임워크는 게임 이름을 모르므로 팩 루트는 부르는 쪽이 넘긴다
 *          (게임 인스턴스는 자기 `BootstrapConfig::_packRoot`, 에디터 확장은 활성 게임의 `GameConfig::getActive()._packRoot.get()`).
 *          폴더 종류(접미가 확장자뿐이고 포함 규칙이 폴더인 것, 예를 들어 외형 `"/data/appearance/"`)는 팩 루트에 포함 규칙을 붙인 폴더다.
 */
#pragma once
#include "Core/Container/string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @struct GamePackDataUtil
     * @brief 팩 루트와 데이터 종류 이름으로 팩 안 데이터 경로를 만듭니다.
     */
    struct SW_GF_API GamePackDataUtil
    {
        /**
         * @brief 팩 @p packRoot 안의 데이터 종류 @p kindLabel 파일(또는 폴더) 경로를 @p outPath 에 만듭니다.
         * @return 종류가 등록되지 않았거나, 팩 루트가 비었거나 종류의 접두와 맞지 않거나, 종류에 팩 안 경로 규칙이 없으면 false 이고 @p outError 에 이유를 담습니다.
         *         파일이 있는지는 보지 않습니다(읽는 쪽이 판단한다).
         */
        [[nodiscard]] static bool tryMakeKindPath( string_view packRoot, string_view kindLabel, string& outPath, string& outError );
    };
} // namespace sw
