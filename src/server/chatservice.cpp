#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <vector>
#include <map>
using namespace std;
using namespace muduo;

ChatService* ChatService::instance() {
    static ChatService service;
    return &service;
}

ChatService::ChatService() {
    _msgHandleMap[LOGIN_MSG] = std::bind(&ChatService::login, this, _1, _2, _3);
    _msgHandleMap[LOGINOUT_MSG] = std::bind(&ChatService::loginout, this, _1, _2, _3);
    _msgHandleMap[REG_MSG] = std::bind(&ChatService::reg, this, _1, _2, _3);
    _msgHandleMap[ONE_CHAT_MSG] = std::bind(&ChatService::oneChat, this, _1, _2, _3);
    _msgHandleMap[ADD_FRIEND_MSG] = std::bind(&ChatService::addFriend, this, _1, _2, _3);
    _msgHandleMap[CREATE_GROUP_MSG] = std::bind(&ChatService::createGroup, this, _1, _2, _3);
    _msgHandleMap[ADD_GROUP_MSG] = std::bind(&ChatService::addGroup, this, _1, _2, _3);
    _msgHandleMap[GROUP_CHAT_MSG] = std::bind(&ChatService::groupChat, this, _1, _2, _3);

    if (_redis.connect()) {
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}

void ChatService::reset() {
    _userModel.resetState();
}

MsgHandler ChatService::getHandler(int msgid) {
    if (_msgHandleMap.find(msgid) == _msgHandleMap.end()) {
        return [=](const TcpConnectionPtr& conn, json& js, Timestamp){
                    LOG_ERROR << "msgid: " << msgid << " can not find handler!";
        };
    } else {
        return _msgHandleMap[msgid];
    }
}

void ChatService::login(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    int id = js["id"];
    string pwd = js["password"];

    User user = _userModel.query(id);
    if (user.getId() == id && user.getPwd() == pwd) {
        if (user.getState() != "online") {
            // 记录登录用户的连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            _redis.subscribe(id);

            // 更新登录状态
            user.setState("online");
            _userModel.updateState(user);

            // 返回基本信息
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();

            // 返回离线消息
            vector<string> vec = _offlineMsgModel.query(id);
            if (!vec.empty()) {
                response["offlinemsg"] = vec;
                _offlineMsgModel.remove(id);
            }

            // 返回好友列表信息
            vector<User> userVec = _friendModel.query(id);
            if (!userVec.empty()) {
                vector<string> vec2;
                for (User& user : userVec) {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }

            // 返回群组列表信息
            vector<Group> groupuserVec = _groupModel.queryGroups(id);
            if (!groupuserVec.empty()) {
                vector<string> groupV;
                for (auto& group : groupuserVec) {
                    json grpjson;
                    grpjson["id"] = group.getId();
                    grpjson["groupname"] = group.getName();
                    grpjson["groupdesc"] = group.getDesc();
                    vector<string> userV;
                    for (GroupUser& user : group.getUsers()) {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userV.push_back(js.dump());
                    }
                    grpjson["users"] = userV;
                    groupV.push_back(grpjson.dump());
                }
                response["groups"] = groupV;
            }

            conn->send(response.dump());
        } else {
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "该账号已登录";
            conn->send(response.dump());
        }
    } else {
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "用户名或密码错误";
        conn->send(response.dump());
    }
}

void ChatService::reg(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    bool state = _userModel.insert(user);
    if (state) {
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
    } else {
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }
}

void ChatService::loginout(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    int userid = js["id"];
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end()) {
            _userConnMap.erase(it);
        }
    }
    _redis.unsubscribe(userid);

    User user;
    user.setId(userid);
    user.setState("offline");
    _userModel.updateState(user);
}

void ChatService::clientCloseException(const TcpConnectionPtr& conn) {
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it) {
            if (it->second == conn) {
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }
    _redis.unsubscribe(user.getId());
    if (user.getId() != -1) {
        user.setState("offline");
        _userModel.updateState(user);
    }
}

void ChatService::oneChat(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    int toid = js["to"];

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end()) {
            it->second->send(js.dump());
            return ;
        }
    }
    User user = _userModel.query(toid);
    if (user.getState() == "online") {
        _redis.publish(toid, js.dump());
        return ;
    }

    _offlineMsgModel.insert(toid, js.dump());

}

void ChatService::addFriend(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    int userid = js["id"];
    int friendid = js["friendid"];
    
    _friendModel.insert(userid, friendid);
}


void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time) {
    int userid = js["id"];
    string name = js["groupname"];
    string desc = js["groupdesc"];

    Group group(-1, name, desc);
    json response;
    if (_groupModel.createGroup(group)) {
        _groupModel.addGroup(userid, group.getId(), "creator");
        response["msg"] = "创建群组成功！";
        conn->send(response.dump());
    } else {
        response["msg"] = "创建群组失败！！！";
        conn->send(response.dump());
    }
}

void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time) {
    int userid = js["id"];
    int groupid = js["groupid"];
    json response;
    if (_groupModel.addGroup(userid, groupid, "normal")) {
        response["msg"] = "添加群组成员成功！";
        conn->send(response.dump()); 
    } else {
        response["msg"] = "添加群组成员失败！！！";
        conn->send(response.dump());
    }
}

void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time) {
    int userid = js["id"];
    int groupid = js["groupid"];
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);
    lock_guard<mutex> lock(_connMutex);
    for (int id : useridVec) {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end()) {
            it->second->send(js.dump());
        } else {
            User user = _userModel.query(id);
            if (user.getState() == "online") {
                _redis.publish(id, js.dump());
            } else {
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}

void ChatService::handleRedisSubscribeMessage(int userid, string msg) {
    json js = json::parse(msg.c_str());

    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end()) {
        it->second->send(js.dump());
        return;
    }
    _offlineMsgModel.insert(userid, msg);
}


//{"msgid":1,"id":1,"password":"123456"}
//{"msgid":5,"id":1,"from":"zhang","to":2,"msg":"hello!"}
//{"msgid":6,"id":1,"friendid":2}
