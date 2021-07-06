#include <drogon/drogon.h>
#include <drogon/nosql/RedisClient.h>

int main()
{
    // Set HTTP listener address and port
    drogon::app().addListener("0.0.0.0", 8080);
    // Load config file
    // drogon::app().loadConfigFile("../config.json");
    // Run HTTP framework,the method will block in the internal event loop
    drogon::app().createRedisClient("127.0.0.1", 6379);
    drogon::app().run();
    return 0;
}
