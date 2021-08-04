#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <unordered_map>
#include <functional>
#include <mutex>
using namespace std;
using namespace placeholders;

#include <muduo/net/TcpConnection.h>
using namespace muduo;
using namespace muduo::net;

#include "redis.hpp"
#include "json.hpp"
#include "usermodel.hpp"
#include "offlinemessagemodel.hpp"
#include "friendmodel.hpp"
#include "groupmodel.hpp"
using json = nlohmann::json;

using MsgHandler = std::function<void(const TcpConnectionPtr& conn, json& js, Timestamp)>;

class ChatService {
public:
    static ChatService* instance();

    void login(const TcpConnectionPtr& conn, json& js, Timestamp time);

    void reg(const TcpConnectionPtr& conn, json& js, Timestamp time);

    void oneChat(const TcpConnectionPtr& conn, json& js, Timestamp time);

    void addFriend(const TcpConnectionPtr& conn, json& js, Timestamp time);

    void createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);

    void addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);

    void groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time);

    void loginout(const TcpConnectionPtr &conn, json &js, Timestamp time);

    void clientCloseException(const TcpConnectionPtr& conn);

    void handleRedisSubscribeMessage(int userid, string msg);

    void reset();

    MsgHandler getHandler(int msgid);
private:
    ChatService();

    unordered_map<int, MsgHandler> _msgHandleMap;
    
    unordered_map<int, TcpConnectionPtr> _userConnMap;

    mutex _connMutex;

    UserModel _userModel;
    OfflineMsgModel _offlineMsgModel;
    FriendModel _friendModel;
    GroupModel _groupModel;

    Redis _redis;
};

#endif