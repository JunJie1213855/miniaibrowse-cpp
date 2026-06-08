#include "../include/handlers/ChatLoginHandler.h"
#include "../include/handlers/ChatRegisterHandler.h"
#include "../include/handlers/ChatLogoutHandler.h"
#include"../include/handlers/ChatHandler.h"
#include"../include/handlers/ChatEntryHandler.h"
#include"../include/handlers/ChatSendHandler.h"
#include"../include/handlers/ChatHistoryHandler.h"


#include"../include/handlers/ChatSessionsHandler.h"
#include"../include/handlers/ChatRenameSessionHandler.h"
#include"../include/handlers/ChatStreamHandler.h"
#include"../include/handlers/ChatDeleteSessionHandler.h"

#include "../include/ChatServer.h"
#include "../../../HttpServer/include/http/HttpRequest.h"
#include "../../../HttpServer/include/http/HttpResponse.h"
#include "../../../HttpServer/include/http/HttpServer.h"



using namespace http;


ChatServer::ChatServer(int port,
    const std::string& name,
    muduo::net::TcpServer::Option option)
    : httpServer_(port, name, option)
{
    initialize();
}

void ChatServer::initialize() {
    std::cout << "ChatServer initialize start  ! " << std::endl;
	http::MysqlUtil::init("tcp://127.0.0.1:3307", "root", "123456", "ChatHttpServer", 5);

    initializeSession();

    initializeMiddleware();

    initializeRouter();
}

void ChatServer::initChatMessage() {

    std::cout << "initChatMessage start ! " << std::endl;
    readDataFromMySQL();
    std::cout << "initChatMessage success ! " << std::endl;
}

void ChatServer::readDataFromMySQL() {


    std::string sql = "SELECT id, username, session_id, is_user, content, ts FROM chat_message ORDER BY ts ASC, id ASC";

    sql::ResultSet* res;
    try {
        res = mysqlUtil_.executeQuery(sql);
    }
    catch (const std::exception& e) {
        std::cerr << "MySQL query failed: " << e.what() << std::endl;
        return;
    }

    while (res->next()) {
        long long user_id = 0;
        std::string session_id ;  
        std::string username, content;
        long long ts = 0;
        int is_user = 1;

        try {
            user_id    = res->getInt64("id");       
            session_id = res->getString("session_id");  
            username   = res->getString("username");
            content    = res->getString("content");
            ts         = res->getInt64("ts");
            is_user    = res->getInt("is_user");
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to read row: " << e.what() << std::endl;
            continue; 
        }

        auto& userSessions = chatInformation[user_id];

        std::shared_ptr<AIHelper> helper;
        auto itSession = userSessions.find(session_id);
        if (itSession == userSessions.end()) {
            helper = std::make_shared<AIHelper>();
            userSessions[session_id] = helper;
			sessionsIdsMap[user_id].push_back(session_id);
        } else {
            helper = itSession->second;
        }

        helper->restoreMessage(content, ts);
    }

    std::cout << "readDataFromMySQL finished" << std::endl;
}



// 按需懒加载单个用户的消息历史，避免启动时全量加载所有用户数据
// 只在用户首次访问（登录后加载会话列表）时触发一次，后续直接从内存读取
void ChatServer::ensureUserDataLoaded(int userId) {
	// 已加载过则跳过
	{
		std::lock_guard<std::mutex> lock(mutexForChatInformation);
		if (chatInformation.find(userId) != chatInformation.end()) return;
	}

	std::string sql = "SELECT id, username, session_id, is_user, content, ts "
		"FROM chat_message WHERE id = " + std::to_string(userId) +
		" ORDER BY ts ASC, id ASC";

	sql::ResultSet* res;
	try {
		res = mysqlUtil_.executeQuery(sql);
	} catch (const std::exception& e) {
		std::cerr << "ensureUserDataLoaded MySQL query failed for user "
				  << userId << ": " << e.what() << std::endl;
		return;
	}

	while (res->next()) {
		std::string session_id;
		std::string username, content;
		long long ts = 0;
		int is_user = 1;

		try {
			session_id = res->getString("session_id");
			username   = res->getString("username");
			content    = res->getString("content");
			ts         = res->getInt64("ts");
			is_user    = res->getInt("is_user");
		} catch (const std::exception& e) {
			std::cerr << "ensureUserDataLoaded row read failed: " << e.what() << std::endl;
			continue;
		}

		std::lock_guard<std::mutex> lockChat(mutexForChatInformation);
		auto& userSessions = chatInformation[userId];
		std::shared_ptr<AIHelper> helper;
		auto it = userSessions.find(session_id);
		if (it == userSessions.end()) {
			helper = std::make_shared<AIHelper>();
			userSessions[session_id] = helper;
			std::lock_guard<std::mutex> lockSid(mutexForSessionsId);
			sessionsIdsMap[userId].push_back(session_id);
		} else {
			helper = it->second;
		}
		helper->restoreMessage(content, ts);
	}

	std::cout << "ensureUserDataLoaded: user " << userId
			  << " loaded" << std::endl;
}

void ChatServer::setThreadNum(int numThreads) {
    httpServer_.setThreadNum(numThreads);
}


void ChatServer::start() {
    httpServer_.start();
}


void ChatServer::initializeRouter() {

    httpServer_.Get("/", std::make_shared<ChatEntryHandler>(this));
    httpServer_.Get("/entry", std::make_shared<ChatEntryHandler>(this));

    httpServer_.Post("/login", std::make_shared<ChatLoginHandler>(this));

    httpServer_.Post("/register", std::make_shared<ChatRegisterHandler>(this));

    httpServer_.Post("/user/logout", std::make_shared<ChatLogoutHandler>(this));

    httpServer_.Get("/chat", std::make_shared<ChatHandler>(this));

    httpServer_.Post("/chat/send", std::make_shared<ChatSendHandler>(this));

    httpServer_.Post("/chat/stream", std::make_shared<ChatStreamHandler>(this));

    httpServer_.Post("/chat/history", std::make_shared<ChatHistoryHandler>(this));


    httpServer_.Get("/chat/sessions", std::make_shared<ChatSessionsHandler>(this));
    httpServer_.Post("/chat/delete-session", std::make_shared<ChatDeleteSessionHandler>(this));
    httpServer_.Post("/chat/rename", std::make_shared<ChatRenameSessionHandler>(this));
}

void ChatServer::initializeSession() {

    auto sessionStorage = std::make_unique<http::session::MemorySessionStorage>();

    auto sessionManager = std::make_unique<http::session::SessionManager>(std::move(sessionStorage));

    setSessionManager(std::move(sessionManager));
}

void ChatServer::initializeMiddleware() {

    auto corsMiddleware = std::make_shared<http::middleware::CorsMiddleware>();

    httpServer_.addMiddleware(corsMiddleware);
}


void ChatServer::packageResp(const std::string& version,
    http::HttpResponse::HttpStatusCode statusCode,
    const std::string& statusMsg,
    bool close,
    const std::string& contentType,
    int contentLen,
    const std::string& body,
    http::HttpResponse* resp)
{
    if (resp == nullptr)
    {
        LOG_ERROR << "Response pointer is null";
        return;
    }

    try
    {
        resp->setVersion(version);
        resp->setStatusCode(statusCode);
        resp->setStatusMessage(statusMsg);
        resp->setCloseConnection(close);
        resp->setContentType(contentType);
        resp->setContentLength(contentLen);
        resp->setBody(body);

        LOG_INFO << "Response packaged successfully";
    }
    catch (const std::exception& e)
    {
        LOG_ERROR << "Error in packageResp: " << e.what();

        resp->setStatusCode(http::HttpResponse::k500InternalServerError);
        resp->setStatusMessage("Internal Server Error");
        resp->setCloseConnection(true);
    }
}
