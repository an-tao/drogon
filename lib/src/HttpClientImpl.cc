/**
 *
 *  HttpClientImpl.cc
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

#include "HttpClientImpl.h"
#include "HttpRequestImpl.h"
#include "HttpResponseParser.h"
#include "HttpAppFrameworkImpl.h"
#include <stdlib.h>
#include <algorithm>

using namespace drogon;
using namespace std::placeholders;

HttpClientImpl::HttpClientImpl(trantor::EventLoop *loop,
                               const trantor::InetAddress &addr,
                               bool useSSL)
    : _loop(loop),
      _server(addr),
      _useSSL(useSSL)
{
}

HttpClientImpl::HttpClientImpl(trantor::EventLoop *loop,
                               const std::string &hostString)
    : _loop(loop)
{
    auto lowerHost = hostString;
    std::transform(lowerHost.begin(), lowerHost.end(), lowerHost.begin(), tolower);
    if (lowerHost.find("https://") != std::string::npos)
    {
        _useSSL = true;
        lowerHost = lowerHost.substr(8);
    }
    else if (lowerHost.find("http://") != std::string::npos)
    {
        _useSSL = false;
        lowerHost = lowerHost.substr(7);
    }
    else
    {
        return;
    }
    auto pos = lowerHost.find(":");
    if (pos != std::string::npos)
    {
        _domain = lowerHost.substr(0, pos);
        auto portStr = lowerHost.substr(pos + 1);
        pos = portStr.find("/");
        if (pos != std::string::npos)
        {
            portStr = portStr.substr(0, pos);
        }
        auto port = atoi(portStr.c_str());
        if (port > 0 && port < 65536)
        {
            _server = InetAddress(port);
        }
    }
    else
    {
        _domain = lowerHost;
        pos = _domain.find("/");
        if (pos != std::string::npos)
        {
            _domain = _domain.substr(0, pos);
        }
        if (_useSSL)
        {
            _server = InetAddress(443);
        }
        else
        {
            _server = InetAddress(80);
        }
    }
    LOG_TRACE << "userSSL=" << _useSSL << " domain=" << _domain;
}

HttpClientImpl::~HttpClientImpl()
{
    LOG_TRACE << "Deconstruction HttpClient";
}

void HttpClientImpl::sendRequest(const drogon::HttpRequestPtr &req, const drogon::HttpReqCallback &callback)
{
    auto thisPtr = shared_from_this();
    _loop->runInLoop([thisPtr, req, callback]() {
        thisPtr->sendRequestInLoop(req, callback);
    });
}

void HttpClientImpl::sendRequest(const drogon::HttpRequestPtr &req, drogon::HttpReqCallback &&callback)
{
    auto thisPtr = shared_from_this();
    _loop->runInLoop([thisPtr, req, callback = std::move(callback)]() {
        thisPtr->sendRequestInLoop(req, callback);
    });
}

void HttpClientImpl::sendRequestInLoop(const drogon::HttpRequestPtr &req,
                                       const drogon::HttpReqCallback &callback)
{
    _loop->assertInLoopThread();
    req->addHeader("Connection", "Keep-Alive");
    // req->addHeader("Accept", "*/*");
    if (!_domain.empty())
    {
        req->addHeader("Host", _domain);
    }
    req->addHeader("User-Agent", "DrogonClient");

    if (!_tcpClient)
    {
        bool hasIpv6Address = false;
        if (_server.isIpV6())
        {
            auto ipaddr = _server.ip6NetEndian();
            for (int i = 0; i < 4; i++)
            {
                if (ipaddr[i] != 0)
                {
                    hasIpv6Address = true;
                    break;
                }
            }
        }

        if (_server.ipNetEndian() == 0 && !hasIpv6Address &&
            !_domain.empty() &&
            _server.portNetEndian() != 0)
        {
            //dns
            //TODO: timeout should be set by user
            if (InetAddress::resolve(_domain, &_server) == false)
            {
                callback(ReqResult::BadServerAddress,
                         HttpResponse::newHttpResponse());
                return;
            }
            LOG_TRACE << "dns:domain=" << _domain << ";ip=" << _server.toIp();
        }

        if ((_server.ipNetEndian() != 0 || hasIpv6Address) && _server.portNetEndian() != 0)
        {
            LOG_TRACE << "New TcpClient," << _server.toIpPort();
            _tcpClient = std::make_shared<trantor::TcpClient>(_loop, _server, "httpClient");

#ifdef USE_OPENSSL
            if (_useSSL)
            {
                _tcpClient->enableSSL();
            }
#endif
            std::weak_ptr<HttpClientImpl> weakPtr = shared_from_this();
            assert(_reqAndCallbacks.empty());
            _reqAndCallbacks.push(std::make_pair(req, callback));
            _tcpClient->setConnectionCallback([weakPtr](const trantor::TcpConnectionPtr &connPtr) {
                auto thisPtr = weakPtr.lock();
                if (!thisPtr)
                    return;
                if (connPtr->connected())
                {
                    connPtr->setContext(HttpResponseParser(connPtr));
                    //send request;
                    LOG_TRACE << "Connection established!";
                    auto req = thisPtr->_reqAndCallbacks.front().first;
                    thisPtr->sendReq(connPtr, req);
                }
                else
                {
                    LOG_TRACE << "connection disconnect";
                    while (!(thisPtr->_reqAndCallbacks.empty()))
                    {
                        auto reqCallback = thisPtr->_reqAndCallbacks.front().second;
                        thisPtr->_reqAndCallbacks.pop();
                        reqCallback(ReqResult::NetworkFailure, HttpResponse::newHttpResponse());
                    }
                    thisPtr->_tcpClient.reset();
                }
            });
            _tcpClient->setConnectionErrorCallback([weakPtr]() {
                auto thisPtr = weakPtr.lock();
                if (!thisPtr)
                    return;
                //can't connect to server
                while (!(thisPtr->_reqAndCallbacks.empty()))
                {
                    auto reqCallback = thisPtr->_reqAndCallbacks.front().second;
                    thisPtr->_reqAndCallbacks.pop();
                    reqCallback(ReqResult::BadServerAddress, HttpResponse::newHttpResponse());
                }
                thisPtr->_tcpClient.reset();
            });
            _tcpClient->setMessageCallback([weakPtr](const trantor::TcpConnectionPtr &connPtr, trantor::MsgBuffer *msg) {
                auto thisPtr = weakPtr.lock();
                if (thisPtr)
                {
                    thisPtr->onRecvMessage(connPtr, msg);
                }
            });
            _tcpClient->connect();
        }
        else
        {
            callback(ReqResult::BadServerAddress,
                     HttpResponse::newHttpResponse());
            return;
        }
    }
    else
    {
        //send request;
        auto connPtr = _tcpClient->connection();
        if (connPtr && connPtr->connected())
        {
            if (_reqAndCallbacks.empty())
            {
                sendReq(connPtr, req);
            }
        }
        auto thisPtr = shared_from_this();
        _reqAndCallbacks.push(std::make_pair(req, [thisPtr, callback](ReqResult result, const HttpResponsePtr &response) {
            //thisPtr.reset();
            callback(result, response);
        }));
    }
}

void HttpClientImpl::sendReq(const trantor::TcpConnectionPtr &connPtr, const HttpRequestPtr &req)
{

    trantor::MsgBuffer buffer;
    auto implPtr = std::dynamic_pointer_cast<HttpRequestImpl>(req);
    assert(implPtr);
    implPtr->appendToBuffer(&buffer);
    LOG_TRACE << "Send request:" << std::string(buffer.peek(), buffer.readableBytes());
    connPtr->send(std::move(buffer));
}

void HttpClientImpl::onRecvMessage(const trantor::TcpConnectionPtr &connPtr, trantor::MsgBuffer *msg)
{
    HttpResponseParser *responseParser = any_cast<HttpResponseParser>(connPtr->getMutableContext());

    //LOG_TRACE << "###:" << msg->readableBytes();
    if (!responseParser->parseResponse(msg))
    {
        assert(!_reqAndCallbacks.empty());
        auto cb = _reqAndCallbacks.front().second;
        cb(ReqResult::BadResponse, HttpResponse::newHttpResponse());
        _reqAndCallbacks.pop();

        _tcpClient.reset();
        return;
    }

    if (responseParser->gotAll())
    {
        auto resp = responseParser->responseImpl();
        responseParser->reset();

        assert(!_reqAndCallbacks.empty());

        auto &type = resp->getHeaderBy("content-type");
        if (type.find("application/json") != std::string::npos)
        {
            resp->parseJson();
        }

        if (resp->getHeaderBy("content-encoding") == "gzip")
        {
            resp->gunzip();
        }
        auto &cb = _reqAndCallbacks.front().second;
        cb(ReqResult::Ok, resp);
        _reqAndCallbacks.pop();

        LOG_TRACE << "req buffer size=" << _reqAndCallbacks.size();
        if (!_reqAndCallbacks.empty())
        {
            auto req = _reqAndCallbacks.front().first;
            sendReq(connPtr, req);
        }
        else
        {
            if (resp->closeConnection())
            {
                _tcpClient.reset();
            }
        }
    }
}

HttpClientPtr HttpClient::newHttpClient(const std::string &ip, uint16_t port, bool useSSL, trantor::EventLoop *loop)
{
    bool isIpv6 = ip.find(":") == std::string::npos ? false : true;
    return std::make_shared<HttpClientImpl>(loop == nullptr ? HttpAppFrameworkImpl::instance().getLoop() : loop, trantor::InetAddress(ip, port, isIpv6), useSSL);
}

HttpClientPtr HttpClient::newHttpClient(const std::string &hostString, trantor::EventLoop *loop)
{
    return std::make_shared<HttpClientImpl>(loop == nullptr ? HttpAppFrameworkImpl::instance().getLoop() : loop, hostString);
}
