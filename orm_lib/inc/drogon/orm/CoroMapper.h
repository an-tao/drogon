/**
 *
 *  @file CoroMapper.h
 *  @author An Tao
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

#ifdef __cpp_impl_coroutine
#include <drogon/utils/coroutine.h>
#include <drogon/orm/Mapper.h>
namespace drogon
{
namespace orm
{
namespace internal
{
template <typename ReturnType>
struct MapperAwaiter : public CallbackAwaiter<ReturnType>
{
    using MapperFunction =
        std::function<void(std::function<void(ReturnType result)> &&,
                           std::function<void(const DrogonDbException &)> &&)>;
    MapperAwaiter(MapperFunction &&function) : function_(std::move(function))
    {
    }
    void await_suspend(std::coroutine_handle<> handle)
    {
        function_(
            [handle, this](ReturnType result) {
                this->setValue(std::move(result));
                handle.resume();
            },
            [handle, this](const DrogonDbException &e) {
                this->setException(std::make_exception_ptr(e));
                handle.resume();
            });
    }

  private:
    MapperFunction function_;
};
}  // namespace internal

/**
 * @brief This template implements coroutine interfaces of ORM. All the methods
 * of this template are coroutine versions of the synchronous interfaces of the
 * orm::Mapper template.
 *
 * @tparam T The type of the model.
 */
template <typename T>
class CoroMapper : public Mapper<T>
{
  public:
    CoroMapper(const DbClientPtr &client) : Mapper<T>(client)
    {
    }
    using TraitsPKType = typename Mapper<T>::TraitsPKType;
    inline const Task<T> findByPrimaryKey(const TraitsPKType &key)
    {
        co_return co_await internal::MapperAwaiter<T>(
            [this, key](
                std::function<void(T)> &&callback,
                std::function<void(const DrogonDbException &)> &&errCallback) {
                Mapper<T>::findByPrimaryKey(key,
                                            std::move(callback),
                                            std::move(errCallback));
            });
    }
    inline const Task<std::vector<T>> findAll()
    {
        co_return co_await internal::MapperAwaiter<std::vector<T>>(
            [this](
                std::function<void(std::vector<T>)> &&callback,
                std::function<void(const DrogonDbException &)> &&errCallback) {
                Mapper<T>::findAll(std::move(callback), std::move(errCallback));
            });
    }
    inline const Task<size_t> count(const Criteria &criteria = Criteria())
    {
        co_return co_await internal::MapperAwaiter<size_t>(
            [this, criteria](
                std::function<void(const size_t)> &&callback,
                std::function<void(const DrogonDbException &)> &&errCallback) {
                Mapper<T>::count(criteria,
                                 std::move(callback),
                                 std::move(errCallback));
            });
    }
    inline const Task<T> findOne(const Criteria &criteria)
    {
        co_return co_await internal::MapperAwaiter<T>(
            [this, criteria](
                std::function<void(T)> &&callback,
                std::function<void(const DrogonDbException &)> &&errCallback) {
                Mapper<T>::findOne(criteria,
                                   std::move(callback),
                                   std::move(errCallback));
            });
    }
    inline const Task<std::vector<T>> findBy(const Criteria &criteria)
    {
        co_return co_await internal::MapperAwaiter<std::vector<T>>(
            [this, criteria](
                std::function<void(std::vector<T>)> &&callback,
                std::function<void(const DrogonDbException &)> &&errCallback) {
                Mapper<T>::findBy(criteria,
                                  std::move(callback),
                                  std::move(errCallback));
            });
    }
};
}  // namespace orm
}  // namespace drogon
#endif