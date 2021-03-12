// this file is generated by program automatically,don't modify it!

/**
 *
 *  NotFound.h
 *  An Tao
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  https://github.com/an-tao/drogon
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Drogon
 *
 */
#pragma once

#include <drogon/exports.h>
#include <drogon/DrTemplate.h>
namespace drogon
{
/**
 * @brief This class is used by the drogon to generate the 404 page. Users don't
 * use this class directly.
 */
class NotFound : public drogon::DrTemplate<NotFound>
{
  public:
    NotFound();
    DROGON_EXPORT virtual std::string genText(
        const drogon::HttpViewData &) override;
};
}  // namespace drogon
