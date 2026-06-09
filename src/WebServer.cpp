#include "WebServer.h"
#include "ofMain.h"
#include "Poco/Net/HTTPServer.h"
#include "Poco/Net/HTTPRequestHandlerFactory.h"
#include "Poco/Net/HTTPRequestHandler.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/ServerSocket.h"
#include "Poco/Exception.h"
#include <fstream>
#include <sstream>

using Poco::Net::HTTPServer;
using Poco::Net::HTTPRequestHandler;
using Poco::Net::HTTPRequestHandlerFactory;
using Poco::Net::HTTPServerRequest;
using Poco::Net::HTTPServerResponse;
using Poco::Net::ServerSocket;

class APIRequestHandler : public HTTPRequestHandler {
  public:
	APIRequestHandler(WebServer* server) : webServer(server) {}

	void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) override {
		response.setContentType("application/json");
		response.add("Access-Control-Allow-Origin", "*");
		response.add("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
		response.add("Access-Control-Allow-Headers", "Content-Type");

		std::string path = request.getURI();
		std::string method = request.getMethod();

		if (method == "OPTIONS") {
			response.setStatus(HTTPServerResponse::HTTP_OK);
			response.send();
			return;
		}

		if (path == "/api/pages" && method == "GET") {
			try {
				if (webServer->pagesFetcher) {
					ofJson pages = webServer->pagesFetcher();
					response.setStatus(HTTPServerResponse::HTTP_OK);
					std::string jsonStr = pages.dump();
					response.setContentLength(jsonStr.size());
					response.send() << jsonStr;
				} else {
					response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
					response.send() << R"({"error":"pagesFetcher not set"})";
				}
			} catch (const std::exception& e) {
				ofLogError() << "Exception in /api/pages GET: " << e.what();
				response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
				std::stringstream ss;
				ss << R"({"error":")" << e.what() << R"("})";
				response.send() << ss.str();
			}
		} else if (path == "/api/pages" && method == "POST") {
			std::istream& is = request.stream();
			std::stringstream buffer;
			buffer << is.rdbuf();
			std::string bodyStr = buffer.str();

			try {
				ofJson updated = ofJson::parse(bodyStr);
				const bool hasPages = updated.contains("pages") && updated["pages"].is_array();
				const bool hasCurrentPage = updated.contains("currentPage") && updated["currentPage"].is_string();
				if (!hasPages && !hasCurrentPage) {
					response.setStatus(HTTPServerResponse::HTTP_BAD_REQUEST);
					response.send() << R"({"error":"missing pages or currentPage"})";
					return;
				}
				if (webServer->pagesSaver && hasPages) {
					webServer->pagesSaver(updated);
				}
				if (webServer->pageActivator && hasCurrentPage) {
					webServer->pageActivator(updated["currentPage"].get<std::string>());
				}
				response.setStatus(HTTPServerResponse::HTTP_OK);
				response.send() << R"({"status":"saved"})";
			} catch (const std::exception& e) {
				response.setStatus(HTTPServerResponse::HTTP_BAD_REQUEST);
				std::stringstream ss;
				ss << R"({"error":")" << e.what() << R"("})";
				response.send() << ss.str();
			}
		} else if (path == "/api/parameters" && method == "GET") {
			try {
				if (webServer->parametersFetcher) {
					ofJson params = webServer->parametersFetcher();
					response.setStatus(HTTPServerResponse::HTTP_OK);
					std::string jsonStr = params.dump();
					response.setContentLength(jsonStr.size());
					response.send() << jsonStr;
				} else {
					response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
					response.send() << R"({"error":"parametersFetcher not set"})";
				}
			} catch (const std::exception& e) {
				ofLogError() << "Exception in /api/parameters: " << e.what();
				response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
				std::stringstream ss;
				ss << R"({"error":")" << e.what() << R"("})";
				response.send() << ss.str();
			}
		} else if (path == "/api/config" && method == "GET") {
			try {
				if (webServer->configFetcher) {
					ofJson config = webServer->configFetcher();
					response.setStatus(HTTPServerResponse::HTTP_OK);
					std::string jsonStr = config.dump();
					response.setContentLength(jsonStr.size());
					response.send() << jsonStr;
				} else {
					response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
					response.send() << R"({"error":"configFetcher not set"})";
				}
			} catch (const std::exception& e) {
				ofLogError() << "Exception in /api/config: " << e.what();
				response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
				std::stringstream ss;
				ss << R"({"error":")" << e.what() << R"("})";
				response.send() << ss.str();
			}
		} else if (path == "/api/config" && method == "POST") {
			std::istream& is = request.stream();
			std::stringstream buffer;
			buffer << is.rdbuf();
			std::string bodyStr = buffer.str();

			try {
				ofJson updated = ofJson::parse(bodyStr);
				if (!updated.is_object() || !updated.contains("servers") || !updated["servers"].is_array()) {
					response.setStatus(HTTPServerResponse::HTTP_BAD_REQUEST);
					response.send() << R"({"error":"missing servers array"})";
					return;
				}
				if (!webServer->configSaver) {
					response.setStatus(HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR);
					response.send() << R"({"error":"configSaver not set"})";
					return;
				}
				webServer->configSaver(updated);
				response.setStatus(HTTPServerResponse::HTTP_OK);
				response.send() << R"({"status":"config update queued"})";
			} catch (const std::exception& e) {
				response.setStatus(HTTPServerResponse::HTTP_BAD_REQUEST);
				std::stringstream ss;
				ss << R"({"error":")" << e.what() << R"("})";
				response.send() << ss.str();
			}
		} else {
			response.setStatus(HTTPServerResponse::HTTP_NOT_FOUND);
			response.send() << R"({"error":"not found"})";
		}
	}

  private:
	WebServer* webServer;
};

class StaticFileHandler : public HTTPRequestHandler {
  public:
	StaticFileHandler(const std::string& baseDir) : basePath(baseDir) {}

	void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) override {
		std::string resourcePath = request.getURI();
		if (resourcePath == "/") resourcePath = "/index.html";

		std::string filePath = basePath + resourcePath;

		std::ifstream file(filePath, std::ios::binary);
		if (file.good()) {
			response.setStatus(HTTPServerResponse::HTTP_OK);
			if (filePath.find(".js") != std::string::npos)
				response.setContentType("application/javascript");
			else if (filePath.find(".css") != std::string::npos)
				response.setContentType("text/css");
			else if (filePath.find(".json") != std::string::npos)
				response.setContentType("application/json");
			else
				response.setContentType("text/html");
			response.send() << file.rdbuf();
		} else {
			std::string indexPath = basePath + "/index.html";
			std::ifstream indexFile(indexPath, std::ios::binary);
			if (indexFile.good()) {
				response.setStatus(HTTPServerResponse::HTTP_OK);
				response.setContentType("text/html");
				response.send() << indexFile.rdbuf();
			} else {
				ofLogError() << "StaticFileHandler: index.html not found at " << indexPath;
				response.setStatus(HTTPServerResponse::HTTP_NOT_FOUND);
				response.send() << "File not found";
			}
		}
	}

  private:
	std::string basePath;
};

class WebServerFactory : public HTTPRequestHandlerFactory {
  public:
	WebServerFactory(WebServer* server) : webServer(server) {
		basePath = ofToDataPath("web", true);
		ofLogNotice() << "WebServerFactory: Using web directory: " << basePath;
	}

	HTTPRequestHandler* createRequestHandler(const HTTPServerRequest& request) override {
		std::string path = request.getURI();
		if (path.find("/api/") == 0) {
			return new APIRequestHandler(webServer);
		} else {
			return new StaticFileHandler(basePath);
		}
	}

  private:
	WebServer* webServer;
	std::string basePath;
};

WebServer::WebServer(int port)
	: port(port), running(false), httpServer(nullptr) {}

WebServer::~WebServer() {
	stop();
}

void WebServer::start() {
	if (running) return;

	try {
		ServerSocket svs(port);
		WebServerFactory* factory = new WebServerFactory(this);
		Poco::Net::HTTPServerParams* params = new Poco::Net::HTTPServerParams();
		params->setMaxQueued(100);
		params->setMaxThreads(4);
		httpServer = std::make_unique<HTTPServer>(factory, svs, params);
		httpServer->start();
		running = true;
		ofLogNotice() << "WebServer started on port " << port;
	} catch (const Poco::Exception& e) {
		ofLogError() << "WebServer::start() Poco exception: " << e.what();
		running = false;
	} catch (const std::exception& e) {
		ofLogError() << "WebServer::start() exception: " << e.what();
		running = false;
	} catch (...) {
		ofLogError() << "WebServer::start() unknown exception";
		running = false;
	}
}

void WebServer::stop() {
	if (running && httpServer) {
		httpServer->stop();
		running = false;
		ofLogNotice() << "WebServer stopped";
	}
}

bool WebServer::isRunning() const {
	return running;
}

void WebServer::broadcastParameterUpdate(const std::string& path, float value, int serverId) {
	// TODO: implement WebSocket broadcasting
}
