#include <drogon/WebSocketClient.h>
#include <drogon/HttpAppFramework.h>
#include <drogon/drogon_test.h>

#include <iostream>

using namespace drogon;
using namespace std::chrono_literals;

static std::vector<WebSocketClientPtr> wsClients_;

DROGON_TEST(MultipleWsTest)
{
    // NOTE: The original test was for 1000 clients. But that seems to be
    //       causing memory leak.
    wsClients_.reserve(20);
    for (size_t i = 0; i < 20; i++)
    {
        auto wsPtr = WebSocketClient::newWebSocketClient("127.0.0.1", 8848);
        auto req = HttpRequest::newHttpRequest();
        req->setPath("/chat");
        wsPtr->setMessageHandler(
            [TEST_CTX, i](const std::string &message,
                       const WebSocketClientPtr &wsPtr,
                       const WebSocketMessageType &type) mutable {
                CHECK((type == WebSocketMessageType::Text ||
                       type == WebSocketMessageType::Pong));
                if (type == WebSocketMessageType::Pong)
                {
                    // Check if the correct connection got the result
                    CHECK(message == std::to_string(i));
                    wsPtr->getConnection()->shutdown();
                    TEST_CTX = {};
                }
            });

        wsPtr->connectToServer(
            req,
            [TEST_CTX, i](ReqResult r,
                          const HttpResponsePtr &resp,
                          const WebSocketClientPtr &wsPtr) mutable {
                CHECK(r == ReqResult::Ok);
                if (r != ReqResult::Ok)
                    app().getLoop()->queueInLoop([i]() { wsClients_[i] = {}; });
                REQUIRE(wsPtr != nullptr);
                REQUIRE(resp != nullptr);

                wsPtr->getConnection()->setPingMessage(std::to_string(i), 1s);
                wsPtr->getConnection()->send("hello!");
                CHECK(wsPtr->getConnection()->connected());

                TEST_CTX = {};
            });
        wsClients_.emplace_back(std::move(wsPtr));
    }
}