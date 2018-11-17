/**
 *
 *  SqlBinder.h
 *  An Tao
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *
 */

#pragma once

#include <drogon/orm/Row.h>
#include <drogon/orm/Field.h>
#include <drogon/orm/ResultIterator.h>
#include <drogon/orm/RowIterator.h>
#include <drogon/orm/FunctionTraits.h>
#include <drogon/orm/Exception.h>
#include <trantor/utils/Logger.h>
#include <string.h>
#include <string>
#include <iostream>
#include <functional>
#include <map>
#include <vector>
#include <memory>

namespace drogon
{
namespace orm
{

class DbClient;
typedef std::function<void(const Result &)> QueryCallback;
typedef std::function<void(const std::exception_ptr &)> ExceptPtrCallback;
enum class Mode
{
    NonBlocking,
    Blocking
};
namespace internal
{
template <typename T>
struct VectorTypeTraits
{
    static const bool isVector = false;
    static const bool isPtrVector = false;
    typedef T ItemsType;
};
template <typename T>
struct VectorTypeTraits<std::vector<std::shared_ptr<T>>>
{
    static const bool isVector = true;
    static const bool isPtrVector = true;
    typedef T ItemsType;
};
// template <typename T>
// struct VectorTypeTraits<std::vector<T>>
// {
//     static const bool isVector = true;
//     static const bool isPtrVector = false;
//     typedef T ItemsType;
// };
template <>
struct VectorTypeTraits<std::string>
{
    static const bool isVector = false;
    static const bool isPtrVector = false;
    typedef std::string ItemsType;
};

//we only accept value type or const lreference type or rreference type as handle method parameters type
template <typename T>
struct CallbackArgTypeTraits
{
    static const bool isValid = true;
};
template <typename T>
struct CallbackArgTypeTraits<T *>
{
    static const bool isValid = false;
};
template <typename T>
struct CallbackArgTypeTraits<T &>
{
    static const bool isValid = false;
};
template <typename T>
struct CallbackArgTypeTraits<T &&>
{
    static const bool isValid = true;
};
template <typename T>
struct CallbackArgTypeTraits<const T &>
{
    static const bool isValid = true;
};

class CallbackHolderBase
{
  public:
    virtual ~CallbackHolderBase()
    {
    }
    virtual void execCallback(const Result &result) = 0;
};
template <typename Function>
class CallbackHolder : public CallbackHolderBase
{
  public:
    virtual void execCallback(const Result &result)
    {
        run(result);
    }

    CallbackHolder(Function &&function) : _function(std::forward<Function>(function))
    {
        static_assert(traits::isSqlCallback, "Your sql callback function type is wrong!");
    }

  private:
    Function _function;
    typedef FunctionTraits<Function> traits;
    template <
        std::size_t Index>
    using NthArgumentType = typename traits::template argument<Index>;
    static const size_t argumentCount = traits::arity;

    template <bool isStep = traits::isStepResultCallback>
    typename std::enable_if<isStep, void>::type run(const Result &result)
    {
        if (result.size() == 0)
        {
            run(nullptr, true);
            return;
        }
        for (auto row : result)
        {
            run(&row, false);
        }
        run(nullptr, true);
    }
    template <bool isStep = traits::isStepResultCallback>
    typename std::enable_if<!isStep, void>::type run(const Result &result)
    {
        static_assert(argumentCount == 0, "Your sql callback function type is wrong!");
        _function(result);
    }
    template <
        typename... Values,
        std::size_t Boundary = argumentCount>
    typename std::enable_if<(sizeof...(Values) < Boundary), void>::type run(
        const Row *const row,
        bool isNull,
        Values &&... values)
    {
        //call this function recursively until parameter's count equals to the count of target function parameters
        static_assert(CallbackArgTypeTraits<NthArgumentType<sizeof...(Values)>>::isValid,
                      "your sql callback function argument type must be value type or const left-reference type");
        typedef typename std::remove_cv<typename std::remove_reference<NthArgumentType<sizeof...(Values)>>::type>::type ValueType;
        ValueType value = ValueType();
        if (row && row->size() > sizeof...(Values))
        {
            // if(!VectorTypeTraits<ValueType>::isVector)
            //     value = (*row)[sizeof...(Values)].as<ValueType>();
            // else
            //     ; // value = (*row)[sizeof...(Values)].asArray<VectorTypeTraits<ValueType>::ItemsType>();
            value = makeValue<ValueType>((*row)[sizeof...(Values)]);
        }

        run(row, isNull, std::forward<Values>(values)..., std::move(value));
    }
    template <
        typename... Values,
        std::size_t Boundary = argumentCount>
    typename std::enable_if<(sizeof...(Values) == Boundary), void>::type run(
        const Row *const row,
        bool isNull,
        Values &&... values)
    {
        _function(isNull, std::move(values)...);
    }
    template <typename ValueType>
    typename std::enable_if<VectorTypeTraits<ValueType>::isVector, ValueType>::type makeValue(const Field &field)
    {
        return field.asArray<typename VectorTypeTraits<ValueType>::ItemsType>();
    }
    template <typename ValueType>
    typename std::enable_if<!VectorTypeTraits<ValueType>::isVector, ValueType>::type makeValue(const Field &field)
    {
        return field.as<ValueType>();
    }
};
class SqlBinder
{
    typedef SqlBinder self;

  public:
    friend class Dbclient;

    SqlBinder(const std::string &sql, DbClient &client) : _sql(sql), _client(client)
    {
    }
    ~SqlBinder();
    template <typename CallbackType,
              typename traits = FunctionTraits<CallbackType>>
    typename std::enable_if<traits::isExceptCallback && traits::isPtr, self>::type &operator>>(CallbackType &&callback)
    {
        //LOG_DEBUG << "ptr callback";
        _isExceptPtr = true;
        _exceptPtrCallback = std::forward<CallbackType>(callback);
        return *this;
    }

    template <typename CallbackType,
              typename traits = FunctionTraits<CallbackType>>
    typename std::enable_if<traits::isExceptCallback && !traits::isPtr, self>::type &operator>>(CallbackType &&callback)
    {
        _isExceptPtr = false;
        _exceptCallback = std::forward<CallbackType>(callback);
        return *this;
    }

    template <typename CallbackType,
              typename traits = FunctionTraits<CallbackType>>
    typename std::enable_if<traits::isSqlCallback, self>::type &operator>>(CallbackType &&callback)
    {
        _callbackHolder = std::shared_ptr<CallbackHolderBase>(new CallbackHolder<CallbackType>(std::forward<CallbackType>(callback)));
        return *this;
    }

    template <typename T>
    typename std::enable_if<!std::is_same<typename std::remove_cv<typename std::remove_reference<T>::type>::type,trantor::Date>::value,self &>::type
        operator<<(T &&parameter)
    {
        _paraNum++;
        typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type ParaType;
        std::shared_ptr<void> obj =
            std::make_shared<ParaType>(parameter);
        switch (sizeof(T))
        {
        case 2:
            *std::static_pointer_cast<short>(obj) = ntohs(parameter);
            break;
        case 4:
            *std::static_pointer_cast<int>(obj) = ntohl(parameter);
            break;
        case 8:
            *std::static_pointer_cast<long>(obj) = ntohll(parameter);
            break;
        case 1:
        default:

            break;
        }
        _objs.push_back(obj);
        _parameters.push_back((char *)obj.get());
        _length.push_back(sizeof(T));
        _format.push_back(1);
        //LOG_TRACE << "Bind parameter:" << parameter;
        return *this;
    }
    //template <>
    self &operator<<(const char str[])
    {
        _paraNum++;
        _parameters.push_back((char *)str);
        _length.push_back(strlen(str));
        _format.push_back(0);
        return *this;
    }
    self &operator<<(char str[])
    {
        return operator<<((const char *)str);
    }
    self &operator<<(const std::string &str)
    {
        _paraNum++;
        _parameters.push_back((char *)str.c_str());
        _length.push_back(str.length());
        _format.push_back(0);
        return *this;
    }
    self &operator<<(std::string &str)
    {
        return operator<<((const std::string &)str);
    }
    self &operator<<(std::string &&str)
    {
        std::shared_ptr<std::string> obj = std::make_shared<std::string>(std::move(str));
        _objs.push_back(obj);
        _paraNum++;
        _parameters.push_back((char *)obj->c_str());
        _length.push_back(obj->length());
        _format.push_back(0);
        return *this;
    }
    self &operator<<(trantor::Date &&date)
    {
        return operator<<(date.toCustomedFormattedStringLocal("%Y-%m-%d %H:%M:%S", true)); //for postgreSQL
    }
    self &operator<<(const trantor::Date &date)
    {
        return operator<<(date.toCustomedFormattedStringLocal("%Y-%m-%d %H:%M:%S", true)); //for postgreSQL
    }
    self &operator<<(std::nullptr_t nullp)
    {
        _paraNum++;
        _parameters.push_back(NULL);
        _length.push_back(0);
        _format.push_back(0);
        return *this;
    }
    self &operator<<(const Mode &mode)
    {
        _mode = mode;
        return *this;
    }
    self &operator<<(Mode &&mode)
    {
        _mode = mode;
        return *this;
    }
    void exec() noexcept(false);

  private:
    std::string _sql;
    DbClient &_client;
    size_t _paraNum = 0;
    std::vector<const char *> _parameters;
    std::vector<int> _length;
    std::vector<int> _format;
    std::vector<std::shared_ptr<void>> _objs;
    Mode _mode = Mode::NonBlocking;
    std::shared_ptr<CallbackHolderBase> _callbackHolder;
    DrogonDbExceptionCallback _exceptCallback;
    ExceptPtrCallback _exceptPtrCallback;
    bool _execed = false;
    bool _destructed = false;
    bool _isExceptPtr = false;
};

} // namespace internal
} // namespace orm
} // namespace drogon
