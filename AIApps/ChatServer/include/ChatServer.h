#pragma once

#include <atomic>
#include <memory>
#include <set>
#include <tuple>
#include <unordered_map>
#include <mutex>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>


#include "../../../HttpServer/include/http/HttpServer.h"
#include "../../../HttpServer/include/utils/MysqlUtil.h"
#include "../../../HttpServer/include/utils/FileUtil.h"
#include "../../../HttpServer/include/utils/JsonUtil.h"
#include"AIUtil/AIHelper.h"
#include"AIUtil/MQManager.h"


class ChatLoginHandler;
class ChatRegisterHandler;
class ChatLogoutHandler;
class ChatHandler;
class ChatEntryHandler;
class ChatSendHandler;
class ChatHistoryHandler;


class ChatSessionsHandler;

class ChatServer {
public:
	ChatServer(int port,
		const std::string& name,
		muduo::net::TcpServer::Option option = muduo::net::TcpServer::kNoReusePort);

	void setThreadNum(int numThreads);
	void start();
	void initChatMessage();
	void ensureUserDataLoaded(int userId);
private:
	friend class ChatLoginHandler;
	friend class ChatRegisterHandler;
	friend  ChatLogoutHandler;
	friend class ChatHandler;
	friend class ChatEntryHandler;
	friend class ChatSendHandler;
	friend class ChatHistoryHandler;

	friend class ChatSessionsHandler;
	friend class ChatStreamHandler;
	friend class ChatDeleteSessionHandler;
	friend class ChatRenameSessionHandler;

private:
	void initialize();
	void initializeSession();
	void initializeRouter();
	void initializeMiddleware();
	

	void readDataFromMySQL();

	void packageResp(const std::string& version, http::HttpResponse::HttpStatusCode statusCode,
		const std::string& statusMsg, bool close, const std::string& contentType,
		int contentLen, const std::string& body, http::HttpResponse* resp);

	void setSessionManager(std::unique_ptr<http::session::SessionManager> manager)
	{
		httpServer_.setSessionManager(std::move(manager));
	}
	http::session::SessionManager* getSessionManager() const
	{
		return httpServer_.getSessionManager();
	}

	http::HttpServer	httpServer_;

	http::MysqlUtil		mysqlUtil_;

	std::unordered_map<int, bool>	onlineUsers_;
	std::mutex	mutexForOnlineUsers_;

	

	// std::unordered_map<int, std::shared_ptr<AIHelper>> chatInformation;

	std::unordered_map<int, std::unordered_map<std::string,std::shared_ptr<AIHelper> > > chatInformation;
	std::mutex	mutexForChatInformation;

	std::unordered_map<int,std::vector<std::string> > sessionsIdsMap;
	std::mutex mutexForSessionsId;

	std::set<int> loadedUsers_;       // 已懒加载完成的用户集合
	std::mutex mutexForLoadedUsers_;  // 保护 loadedUsers_ 的 check-and-insert 原子性

};

