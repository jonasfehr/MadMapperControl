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

namespace {
	void sendJson(HTTPServerResponse& response, HTTPServerResponse::HTTPStatus status, const std::string& body) {
		response.setStatus(status);
		response.setContentLength(body.size());
		response.send() << body;
	}

	std::string errorBody(const std::string& message) {
		ofJson j;
		j["error"] = message;
		return j.dump();
	}

	std::string statusBody(const std::string& status) {
		ofJson j;
		j["status"] = status;
		return j.dump();
	}

	std::string readBody(HTTPServerRequest& request) {
		std::stringstream buffer;
		buffer << request.stream().rdbuf();
		return buffer.str();
	}
}

class APIRequestHandler : public HTTPRequestHandler {
  public:
	APIRequestHandler(WebServer* server) : webServer(server) {}

	void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) override {
		response.setContentType("application/json");
		response.add("Access-Control-Allow-Origin", "*");
		response.add("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
		response.add("Access-Control-Allow-Headers", "Content-Type");

		const std::string path = request.getURI();
		const std::string method = request.getMethod();

		if (method == "OPTIONS") {
			response.setStatus(HTTPServerResponse::HTTP_OK);
			response.send();
			return;
		}

		// GET <path> → dump fetcher() as JSON
		struct GetRoute {
			const char* path;
			WebServer::JsonFetcher WebServer::* fetcher;
			const char* name;
		};
		static const GetRoute getRoutes[] = {
			{"/api/pages",        &WebServer::pagesFetcher,       "pagesFetcher"},
			{"/api/parameters",   &WebServer::parametersFetcher,  "parametersFetcher"},
			{"/api/config",       &WebServer::configFetcher,      "configFetcher"},
			{"/api/mappings",     &WebServer::mappingsFetcher,    "mappingsFetcher"},
			{"/api/profiles",     &WebServer::profilesFetcher,    "profilesFetcher"},
			{"/api/profile",      &WebServer::profileFetcher,     "profileFetcher"},
			{"/api/learn/status", &WebServer::learnStatusFetcher, "learnStatusFetcher"},
		};

		// POST <path> → parse body, pass to handler, reply {"status": <ok>}
		struct PostRoute {
			const char* path;
			WebServer::JsonHandler WebServer::* handler;
			const char* okStatus;
		};
		static const PostRoute postRoutes[] = {
			{"/api/mappings",     &WebServer::mappingsSaver, "saved"},
			{"/api/profile",      &WebServer::profileSaver,  "saved"},
			{"/api/learn/inject", &WebServer::learnInjector, "ok"},
			{"/api/learn/assign", &WebServer::learnAssigner, "assigned"},
		};

		if (method == "GET") {
			for (const auto& route : getRoutes) {
				if (path != route.path) continue;
				handleGet(response, webServer->*(route.fetcher), route.name);
				return;
			}
		} else if (method == "POST") {
			if (path == "/api/pages") {
				handlePagesPost(request, response);
				return;
			}
			if (path == "/api/config") {
				handleConfigPost(request, response);
				return;
			}
			if (path == "/api/learn/start") {
				if (webServer->learnStarter) webServer->learnStarter();
				sendJson(response, HTTPServerResponse::HTTP_OK, statusBody("learning"));
				return;
			}
			if (path == "/api/learn/stop") {
				if (webServer->learnStopper) webServer->learnStopper();
				sendJson(response, HTTPServerResponse::HTTP_OK, statusBody("stopped"));
				return;
			}
			for (const auto& route : postRoutes) {
				if (path != route.path) continue;
				handlePost(request, response, webServer->*(route.handler), route.okStatus);
				return;
			}
		}

		sendJson(response, HTTPServerResponse::HTTP_NOT_FOUND, errorBody("not found"));
	}

  private:
	static void handleGet(HTTPServerResponse& response, const WebServer::JsonFetcher& fetcher, const char* name) {
		if (!fetcher) {
			sendJson(response, HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR, errorBody(std::string(name) + " not set"));
			return;
		}
		try {
			sendJson(response, HTTPServerResponse::HTTP_OK, fetcher().dump());
		} catch (const std::exception& e) {
			ofLogError() << "Exception in GET " << name << ": " << e.what();
			sendJson(response, HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR, errorBody(e.what()));
		}
	}

	static void handlePost(HTTPServerRequest& request, HTTPServerResponse& response,
	                       const WebServer::JsonHandler& handler, const char* okStatus) {
		try {
			ofJson body = ofJson::parse(readBody(request));
			if (handler) handler(body);
			sendJson(response, HTTPServerResponse::HTTP_OK, statusBody(okStatus));
		} catch (const std::exception& e) {
			sendJson(response, HTTPServerResponse::HTTP_BAD_REQUEST, errorBody(e.what()));
		}
	}

	void handlePagesPost(HTTPServerRequest& request, HTTPServerResponse& response) {
		try {
			ofJson updated = ofJson::parse(readBody(request));
			const bool hasPages = updated.contains("pages") && updated["pages"].is_array();
			const bool hasCurrentPage = updated.contains("currentPage") && updated["currentPage"].is_string();
			if (!hasPages && !hasCurrentPage) {
				sendJson(response, HTTPServerResponse::HTTP_BAD_REQUEST, errorBody("missing pages or currentPage"));
				return;
			}
			if (webServer->pagesSaver && hasPages) {
				webServer->pagesSaver(updated);
			}
			if (webServer->pageActivator && hasCurrentPage) {
				webServer->pageActivator(updated["currentPage"].get<std::string>());
			}
			sendJson(response, HTTPServerResponse::HTTP_OK, statusBody("saved"));
		} catch (const std::exception& e) {
			sendJson(response, HTTPServerResponse::HTTP_BAD_REQUEST, errorBody(e.what()));
		}
	}

	void handleConfigPost(HTTPServerRequest& request, HTTPServerResponse& response) {
		try {
			ofJson updated = ofJson::parse(readBody(request));
			if (!updated.is_object() || !updated.contains("servers") || !updated["servers"].is_array()) {
				sendJson(response, HTTPServerResponse::HTTP_BAD_REQUEST, errorBody("missing servers array"));
				return;
			}
			if (!webServer->configSaver) {
				sendJson(response, HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR, errorBody("configSaver not set"));
				return;
			}
			webServer->configSaver(updated);
			sendJson(response, HTTPServerResponse::HTTP_OK, statusBody("config update queued"));
		} catch (const std::exception& e) {
			sendJson(response, HTTPServerResponse::HTTP_BAD_REQUEST, errorBody(e.what()));
		}
	}

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
